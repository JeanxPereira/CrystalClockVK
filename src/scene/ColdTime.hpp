#pragma once
#include <cstdint>

#include "scene/ClockState.hpp"
#include "scene/ColdTypes.hpp"
#include "scene/ProgramImage.hpp"

namespace scene {

struct ColdTimeOut {
    ClockTime time;
    int32_t day = 0, month = 0, year = 0;
    bool timeFilled = false;
    int32_t zone[2] = {0, 0};
    float framePeriod = 0;
};

template <class A>
ColdTimeOut coldTime(const ProgramImage& program, const RtcTime& rtc, int timeZone, bool summer, bool pal);

}
