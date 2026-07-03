#include "clock/ClockState.hpp"

namespace ps2clock {

ClockState ClockState::fromTime(int hour24, int minute, int second) {
    ClockState s;
    // 12-hour dial: 12 o'clock (and 24:00 / 00:00) light rod 0, clockwise.
    s.litRod = hour24 % 12;
    // Partial fill = how far through the current hour we are (min+sec / 3600).
    s.fill = (static_cast<float>(minute) * 60.0f + static_cast<float>(second)) / 3600.0f;
    // Patent time->colour: AM blue, PM red. Noon (12:00) is PM, midnight AM.
    s.amPm = (hour24 >= 12) ? AmPm::PM : AmPm::AM;
    return s;
}

float ClockState::spinPhase(float tSeconds) {
    // Live-measured group rotation: -0.1 rad/s (w2-rod-generation.md §7).
    return -0.1f * tSeconds;
}

float ClockState::passAngleStep(RodPass pass) {
    // Steady-state per-rod angle steps (w0-angle-steps.md): the refraction pass
    // is exactly 2x the additive pass, producing the double-surface glass look.
    return pass == RodPass::Refraction ? 0.40f : 0.20f;
}

}  // namespace ps2clock
