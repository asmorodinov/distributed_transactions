#include "format_timestamp.h"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace NMiniYT::NUtil {
    TString FormatTimestamp(ui64 timestamp) {
        const auto duration = std::chrono::nanoseconds(timestamp);
        const auto sc_duration = std::chrono::duration_cast<std::chrono::system_clock::duration>(duration);
        const auto point = std::chrono::time_point<std::chrono::system_clock>(sc_duration);
        const auto t_c = std::chrono::system_clock::to_time_t(point);
        const auto nanoseconds = timestamp % 1000000000;

        std::stringstream ss;
        ss << std::put_time(std::localtime(&t_c), "%F %T") << "," << nanoseconds;
        return ss.str();
    }
} // namespace NMiniYT::NUtil
