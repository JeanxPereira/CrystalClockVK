#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include "GsConstants.hpp"

// Pure PS2 GS math — NO Vulkan dependencies.
// Sources: decomp FUN_00232da0 → 0x00242630 (rod scale), live PCSX2 trace (MEMORY.md §7).

namespace GsCrystalMath {

inline glm::mat4 buildGsProjection(float fov, float aspect, float nearPlane, float farPlane) {
    return glm::perspective(fov, aspect, nearPlane, farPlane);
}

// ──────────────────────────────────────────────────────────────────────────
// Rod Scale Computation (FUN_00232da0 → 0x00242630)
//
// Per-rod scale based on index + widescreen divisors + screen ratio
// correction + hour countdown for active rod.
// ──────────────────────────────────────────────────────────────────────────
inline float computeRodScale(int rodIndex, float baseScale, bool isWidescreen,
                              int screenRatio, bool isSelected, int hourCounter) {
    float scale;

    if (rodIndex >= 2 && rodIndex < 10) {
        int maxRods = isWidescreen
            ? GsConstants::SCALE_MAX_RODS_MIDDLE_WIDE
            : GsConstants::SCALE_MAX_RODS_MIDDLE_STD;
        float divisor = isWidescreen
            ? GsConstants::SCALE_DIVISOR_MIDDLE_WIDE
            : GsConstants::SCALE_DIVISOR_MIDDLE_STD;
        scale = baseScale * static_cast<float>(maxRods - rodIndex) / divisor;
    } else {
        int maxRods = isWidescreen
            ? GsConstants::SCALE_MAX_RODS_EDGE_WIDE
            : GsConstants::SCALE_MAX_RODS_EDGE_STD;
        float divisor = isWidescreen
            ? GsConstants::SCALE_DIVISOR_EDGE_WIDE
            : GsConstants::SCALE_DIVISOR_EDGE_STD;
        scale = baseScale * static_cast<float>(maxRods - rodIndex) / divisor;
    }

    // Screen ratio correction
    if (!isWidescreen) {
        if (screenRatio != GsConstants::SCREEN_RATIO_16_9)
            scale *= static_cast<float>(screenRatio) * 0.0625f; // / 16.0
    } else {
        if (screenRatio != GsConstants::SCREEN_RATIO_4_3)
            scale *= static_cast<float>(screenRatio) / 14.0f;
    }

    // Hour countdown (active rod only)
    if (isSelected) {
        int countdown = isWidescreen
            ? GsConstants::COUNTDOWN_WIDE
            : GsConstants::COUNTDOWN_STD;
        float maxVal = isWidescreen
            ? GsConstants::COUNTDOWN_MAX_WIDE
            : GsConstants::COUNTDOWN_MAX_STD;
        float remaining = static_cast<float>(countdown - hourCounter);
        if (remaining < 0.0f) remaining = 0.0f;
        scale *= remaining / maxVal;
    }

    if (scale < 0.001f) scale = 0.001f;
    return scale;
}

// ──────────────────────────────────────────────────────────────────────────
// Per-pass angle computation
// OSDSYS: baseAngle + rodIndex * angleStep
// Pass 2: both rotation angles = same value
// Pass 3: angleA = angle + offsetX, angleB = angle + offsetY
// ──────────────────────────────────────────────────────────────────────────
inline float computePassAngle(float baseAngle, int rodIndex, float angleStep) {
    return baseAngle + static_cast<float>(rodIndex) * angleStep;
}

// Rod selection flag lives at rod+0xF0 (live trace, stride 0x140).
// Routes the active hour rod to the selected-rod passes.
struct RodState {
    bool selected;     // flag at +0xF0 (active hour rod)
    int screenRatio;   // value at +0xAC
    float yScale;      // computed at +0x60
};

} // namespace GsCrystalMath
