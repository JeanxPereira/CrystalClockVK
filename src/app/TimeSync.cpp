#include "TimeSync.hpp"
#include <chrono>
#include <ctime>

TimeInfo TimeSync::getCurrentTime() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);

    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time_t_now);
#else
    localtime_r(&time_t_now, &local);
#endif

    auto epoch = now.time_since_epoch();
    int ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count() % 1000);

    TimeInfo t{};
    t.hour = local.tm_hour;
    t.minute = local.tm_min;
    t.second = local.tm_sec;
    t.millisecond = ms;
    t.secondsInMinute = static_cast<float>(t.second) + static_cast<float>(t.millisecond) / 1000.0f;
    t.secondsInHour = static_cast<float>(t.minute) * 60.0f + t.secondsInMinute;

    return t;
}
