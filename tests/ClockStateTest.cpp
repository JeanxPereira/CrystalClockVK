// ClockState = the TIME -> VISUAL mapping of the crystal clock (US6693606 2nd
// embodiment + live PCSX2 reads, docs/ghidra_analysis/w2-rod-generation.md §7-8):
//   - group spin phase advances at -0.1 rad/s (live-measured)
//   - the hour picks ONE lit dial rod (12-hour wrap)
//   - minutes+seconds drive a 0..1 partial-fill fraction on the lit rod
//   - AM = blue, PM = red (patent time->colour)
// Pure logic, no Vulkan. Calibration of the exact index offset / colours is
// finalized against the render-diff at W2-3; these tests pin the STRUCTURE.

#include <cmath>
#include <cstdio>
#include <string>

#include "clock/ClockState.hpp"
#include "clock/RodField.hpp"

namespace {
int g_fails = 0;
void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("  FAIL: %s\n", what.c_str()); g_fails++; }
}
bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
}  // namespace

int main() {
    using namespace ps2clock;

    // --- lit hour rod: 12-hour dial, 12:00 -> rod 0 (top), clockwise ---
    check(ClockState::fromTime(12, 0, 0).litRod == 0, "12:00 lights rod 0 (top)");
    check(ClockState::fromTime(0, 0, 0).litRod == 0, "00:00 lights rod 0");
    check(ClockState::fromTime(3, 0, 0).litRod == 3, "03:00 lights rod 3");
    check(ClockState::fromTime(6, 30, 0).litRod == 6, "06:30 lights rod 6");
    check(ClockState::fromTime(17, 0, 0).litRod == 5, "17:00 -> 5 PM lights rod 5");
    check(ClockState::fromTime(23, 0, 0).litRod == 11, "23:00 -> 11 PM lights rod 11");

    // --- partial fill: minute+second within the hour, 0..1 ---
    check(near(ClockState::fromTime(4, 0, 0).fill, 0.0f), "hh:00:00 fill 0");
    check(near(ClockState::fromTime(4, 30, 0).fill, 0.5f), "hh:30:00 fill 0.5");
    check(near(ClockState::fromTime(4, 59, 59).fill, (59.0f * 60 + 59) / 3600.0f),
          "hh:59:59 fill ~1");
    check(ClockState::fromTime(4, 30, 0).fill > 0.49f &&
          ClockState::fromTime(4, 30, 0).fill < 0.51f, "fill monotone mid-hour");

    // --- AM/PM colour (patent): AM blue, PM red ---
    check(ClockState::fromTime(9, 0, 0).amPm == AmPm::AM, "09:00 = AM");
    check(ClockState::fromTime(0, 0, 0).amPm == AmPm::AM, "00:00 = AM (midnight)");
    check(ClockState::fromTime(12, 0, 0).amPm == AmPm::PM, "12:00 = PM (noon)");
    check(ClockState::fromTime(17, 57, 50).amPm == AmPm::PM, "17:57 = PM");

    // --- group spin: phase advances at -0.1 rad/s ---
    check(near(ClockState::spinPhase(0.0f), 0.0f), "spin phase at t=0 is 0");
    check(near(ClockState::spinPhase(10.0f), -1.0f, 1e-3f), "spin -0.1 rad/s over 10s = -1 rad");
    check(near(ClockState::spinPhase(1.0f), -0.1f, 1e-3f), "spin -0.1 rad/s");

    // --- per-rod pass angle offsets (glass double-surface): 0.20 / 0.40 rad ---
    check(near(ClockState::passAngleStep(RodPass::Additive), 0.20f), "additive pass step 0.20");
    check(near(ClockState::passAngleStep(RodPass::Refraction), 0.40f), "refraction pass step 0.40");

    // --- group spin rotates the whole dial rigidly (RodField integration) ---
    {
        // A quarter-turn CCW spin moves rod 0 (12 o'clock, +Y) toward 9 o'clock (-X).
        const float quarter = 3.14159265f / 2.0f;
        RodField spun = RodField::Generate({}, quarter);
        check(near(spun.rods[0].direction.x, -1.0f, 1e-4f) &&
              near(spun.rods[0].direction.y, 0.0f, 1e-4f), "spin +90deg: rod 0 -> 9 o'clock");
        // Spin preserves 30-degree spacing and unit length.
        const float d01 = spun.rods[0].direction.x * spun.rods[1].direction.x +
                          spun.rods[0].direction.y * spun.rods[1].direction.y;
        check(near(d01, std::cos(30.0f * 3.14159265f / 180.0f), 1e-4f), "spin keeps 30deg spacing");
        // Zero spin == the static dial.
        check(near(RodField::Generate({}, 0.0f).rods[3].direction.x, 1.0f, 1e-5f),
              "zero spin == static dial");
    }

    if (g_fails) { std::printf("FAILED (%d)\n", g_fails); return 1; }
    std::printf("OK\n");
    return 0;
}
