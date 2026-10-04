#pragma once
#include <cstdint>

#include "scene/opening/Types.hpp"

namespace scene::opening {

struct HandOffInputs {
    uint32_t snapshot = 0;
    bool clockForced = false, hddReady = false;
    int32_t hddExec = 0;
    int32_t cdda = 0;
};

// facts/opening.md section 8, opening_transition_to_clock 0x0021AEE0 through jtbl_00364F60 (verify_opening3_handoff.mjs).
HandOff decide(const HandOffInputs&);

// facts/opening.md section 8, hddosd-110U-opening2-handoff: the last scene frame to the clock's first.
constexpr int32_t kFramesToClock = 34;

}
