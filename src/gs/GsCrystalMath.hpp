#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include "GsConstants.hpp"

// Pure PS2 GS math — NO Vulkan dependencies.
// Reconstructed from VU0 microcode decode of OSDSYS.elf FUN_002732d8 / FUN_002730a8.
// See docs/ghidra_analysis/vu0_decode.md for full instruction trace.

namespace GsCrystalMath {

// ──────────────────────────────────────────────────────────────────────────
// VU0 Rotation Builder (FUN_002732d8 — 43 VU0 instructions)
//
// The OSDSYS builds a rotation matrix from two angles using:
//   1. sin/cos of both angles loaded into VU0 Q/I registers
//   2. VOPMSUB cross-product for orthogonalization
//   3. Column-by-column accumulator chain
//
// This is equivalent to an Azimuth/Elevation rotation:
//   angleA = azimuth (horizontal rotation)
//   angleB = elevation (vertical tilt)
//
// In Pass 2: angleA == angleB (same angle → symmetric rotation)
// In Pass 3: angleA != angleB (offset → creates shimmer/ghosting)
// ──────────────────────────────────────────────────────────────────────────
inline glm::mat4 buildRotation(float angleA, float angleB) {
    float sinA = std::sin(angleA);
    float cosA = std::cos(angleA);
    float sinB = std::sin(angleB);
    float cosB = std::cos(angleB);

    // Forward vector: direction the rod points
    glm::vec3 forward(sinA * cosB, sinB, cosA * cosB);

    // Up vector: perpendicular in the elevation plane
    glm::vec3 up(-sinA * sinB, cosB, -cosA * sinB);

    // Right vector: cross product (the VOPMSUB instruction)
    glm::vec3 right = glm::cross(forward, up);

    // Assemble rotation matrix (columns = basis vectors)
    return glm::mat4(
        glm::vec4(right, 0.0f),
        glm::vec4(up, 0.0f),
        glm::vec4(forward, 0.0f),
        glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)
    );
}

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

// ──────────────────────────────────────────────────────────────────────────
// Rod selection filter (OSDSYS checks rod+0x150)
// Group A (0x375250): renders rods where index > 7
// Group B (0x377e50): renders rods where (index - 8) > 1
//
// For our simplified 12-rod model, all rods are renderable.
// The selection flag determines Pass 1-3 vs Pass 4-5 routing.
// ──────────────────────────────────────────────────────────────────────────
struct RodState {
    bool selected;     // flag at +0x150 (active hour rod)
    int screenRatio;   // value at +0xAC
    float yScale;      // computed at +0x60
};

} // namespace GsCrystalMath
