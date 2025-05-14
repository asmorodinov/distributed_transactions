#include "hybrid_logical_clock.h"

#include <util/generic/yexception.h>
#include <util/system/guard.h>

#include <chrono>

namespace NMiniYT {

    THybridLogicalClock::THybridLogicalClock(ui64 timestamp)
        : PhysicalTime_(timestamp >> 16)
        , LogicalTime_(timestamp & 0xFFFF)
    {}
    THybridLogicalClock::THybridLogicalClock(ui64 physicalTime, ui16 logicalTime)
        : PhysicalTime_(physicalTime)
        , LogicalTime_(logicalTime)
    {}

    ui64 THybridLogicalClock::GetTimestamp() {
        auto guard = Guard(Lock_);

        OnLocalEventUnsafe();
        return GetTimestampUnsafe();
    }

    ui64 THybridLogicalClock::GetTimestampUnsafe() const
    {
        return (PhysicalTime_ << 16) | LogicalTime_;
    }

    ui64 THybridLogicalClock::GetCurrentPhysicalTime() {
        const auto duration = std::chrono::system_clock::now().time_since_epoch();
        const ui64 nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        return (nanoseconds + 0xFF) >> 16; // divide by 2^16, round upwards => 48 bit timestamp
    }

    void THybridLogicalClock::OnLocalEvent() {
        auto guard = Guard(Lock_);

        OnLocalEventUnsafe();
    }

    // On local event or before send
    void THybridLogicalClock::OnLocalEventUnsafe()
    {
        const ui64 previousPhysicalTime = PhysicalTime_;
        const ui64 currentPhysicalTime = GetCurrentPhysicalTime();
        PhysicalTime_ = Max(PhysicalTime_, currentPhysicalTime);

        if (PhysicalTime_ == previousPhysicalTime) {
            ++LogicalTime_;
            YT_VERIFY(LogicalTime_ != 0);
        } else {
            LogicalTime_ = 0;
        }
    }

    void THybridLogicalClock::OnReceiveEvent(ui64 timestamp) {
        auto guard = Guard(Lock_);

        OnReceiveEventUnsafe(timestamp);
    }

    void THybridLogicalClock::OnReceiveEventUnsafe(ui64 timestamp) {
        const auto eventTime = THybridLogicalClock(timestamp);
        const ui64 previousPhysicalTime = PhysicalTime_;
        const ui64 currentPhysicalTime = GetCurrentPhysicalTime();

        PhysicalTime_ = Max(PhysicalTime_, eventTime.PhysicalTime_, currentPhysicalTime);
        if (PhysicalTime_ == previousPhysicalTime && previousPhysicalTime == eventTime.PhysicalTime_) {
            LogicalTime_ = Max(LogicalTime_, eventTime.LogicalTime_) + 1;
            YT_VERIFY(LogicalTime_ != 0);
        } else if (PhysicalTime_ == previousPhysicalTime) {
            ++LogicalTime_;
            YT_VERIFY(LogicalTime_ != 0);
        } else if (PhysicalTime_ == eventTime.PhysicalTime_) {
            LogicalTime_ = eventTime.LogicalTime_ + 1;
            YT_VERIFY(LogicalTime_ != 0);
        } else {
            LogicalTime_ = 0;
        }
    }

    void THybridLogicalClock::OnReceiveMessage(const google::protobuf::Message& message) {
        const auto descriptor = message.GetDescriptor();
        const auto timestamp_field = descriptor->FindFieldByName("HybridLocalClock");

        if (timestamp_field != nullptr) {
            Y_ENSURE(timestamp_field->type() == google::protobuf::FieldDescriptor::TYPE_UINT64);
            Y_ENSURE(timestamp_field->label() == google::protobuf::FieldDescriptor::LABEL_OPTIONAL);

            const auto reflection = message.GetReflection();

            Y_ENSURE(reflection->HasField(message, timestamp_field));

            const auto timestamp = reflection->GetUInt64(message, timestamp_field);
            OnReceiveEvent(timestamp);
        } else {
            OnLocalEvent();
        }
    }

    void THybridLogicalClock::OnSendMessage(google::protobuf::Message& message) {
        const auto descriptor = message.GetDescriptor();
        const auto timestamp_field = descriptor->FindFieldByName("HybridLocalClock");

        if (timestamp_field != nullptr) {
            Y_ENSURE(timestamp_field->type() == google::protobuf::FieldDescriptor::TYPE_UINT64);
            Y_ENSURE(timestamp_field->label() == google::protobuf::FieldDescriptor::LABEL_OPTIONAL);

            const auto reflection = message.GetReflection();

            const auto timestamp = GetTimestamp();
            reflection->SetUInt64(&message, timestamp_field, timestamp);
        } else {
            OnLocalEvent();
        }
    }

} // namespace NMiniYT
