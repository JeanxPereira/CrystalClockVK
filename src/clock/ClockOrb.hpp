#pragma once

#include <vector>

#include "clock/ClockMath.hpp"
#include "clock/ClockState.hpp"

namespace ps2clock {

// The clock's centre: 8 light spots orbiting the hub (US6693606 element 308,
// live array 0x34c830, 8x0x10; FUN_002354c8 updates them). The spots ride a
// ring inside the dial hole and rotate with a phase (RESOLVED FUN_0020eda0
// orbit math). Rendering (billboards + trail glow) is not evidence-grounded
// yet — only the position formula is kept here.
class ClockOrb {
public:
    static constexpr int kSpots = 8;

    // The 8 light-spot centres at animation `phase` (radians): evenly 45deg
    // apart on a ring of `radius`, rotated by phase. (Quadratic-radius polar
    // spread is a later refinement; a fixed ring reads correctly for now.)
    static std::vector<Vec3> lightSpots(float phase, float radius = 1.25f);
};

}  // namespace ps2clock
