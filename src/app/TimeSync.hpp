#pragma once

#include <cmath>

// Matches Raylib's Time + ElapsedSeconds structs.
// secondsInMinute: elapsed seconds within the current minute (0-59.999)
// secondsInHour: elapsed seconds within the current hour (0-3599.999)
struct TimeInfo {
    int hour;
    int minute;
    int second;
    int millisecond;
    float secondsInMinute; // second + millisecond/1000
    float secondsInHour;   // minute*60 + secondsInMinute
};

namespace TimeSync {

TimeInfo getCurrentTime();

} // namespace TimeSync
