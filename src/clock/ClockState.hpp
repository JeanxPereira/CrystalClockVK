#pragma once

#include "clock/ClockMath.hpp"

namespace ps2clock {

enum class AmPm { AM, PM };

// GS render passes for the crystal double-surface look: the additive pass and
// the refraction pass apply different per-rod angle offsets (0.20 vs 0.40 rad,
// 2x — the glass double-surface). Live-confirmed, w0-angle-steps.md.
enum class RodPass { Additive, Refraction };

// The time -> visual state of the crystal clock dial. Pure logic; the renderer
// turns this into geometry + colour. Anchored on US6693606 (2nd embodiment:
// one lit dial rod = the hour, partial fill = min/sec, AM blue / PM red) and
// the live PCSX2 reads in docs/ghidra_analysis/w2-rod-generation.md.
struct ClockState {
    int   litRod = 0;         // 0..11 dial index of the highlighted (hour) rod
    float fill = 0.0f;        // 0..1 partial fill along the lit rod (min+sec)
    AmPm  amPm = AmPm::AM;

    // Build from a 24-hour wall-clock time.
    static ClockState fromTime(int hour24, int minute, int second);

    // Group spin phase (radians) at time t seconds: -0.1 rad/s, live-measured.
    static float spinPhase(float tSeconds);

    // Per-rod angle offset applied in a given render pass (radians).
    static float passAngleStep(RodPass pass);
};

}  // namespace ps2clock
