#pragma once

#include <google/protobuf/message.h>

#include <library/cpp/yt/threading/spin_lock.h>
#include <library/cpp/yt/threading/public.h>

#include <library/cpp/yt/assert/assert.h>

#include <util/generic/utility.h>
#include <util/system/types.h>

namespace NMiniYT {

class THybridLogicalClock {
public:
    explicit THybridLogicalClock(ui64 timestamp = 0, i64 physicalTimeOffset = 0);
    THybridLogicalClock(ui64 physicalTime, ui16 logicalTime, i64 physicalTimeOffset = 0);

    ui64 GetTimestamp();

    ui64 GetCurrentPhysicalTime() const;

    // On local event or before send
    void OnLocalEvent();
    void OnReceiveEvent(ui64 timestamp);

    // update local HLC
    void OnReceiveMessage(const google::protobuf::Message& message);

    // send HLC timestamp along with a message
    void OnSendMessage(google::protobuf::Message& message);

private:
    ui64 GetTimestampUnsafe() const;
    void OnLocalEventUnsafe();
    void OnReceiveEventUnsafe(ui64 timestamp);

private:
    YT_DECLARE_SPIN_LOCK(NYT::NThreading::TSpinLock, Lock_);

    ui64 PhysicalTime_ = 0; // maximal observed physical time
    ui16 LogicalTime_ = 0;
    i64 PhysicalTimeOffset_ = 0;
};

} // namespace NMiniYT
