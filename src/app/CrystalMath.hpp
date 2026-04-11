#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace CrystalMath {

constexpr int ROD_COUNT = 12;

constexpr float PI = glm::pi<float>();
constexpr float TAU = glm::two_pi<float>();
constexpr float DEG2RAD = PI / 180.0f;

// Ring layout parameter
constexpr float ROD_RING_RADIUS = 2.5f;

// ──────────────────────────────────────────────────────────────────────────
// Globals extracted from Ghidra OSDSYS 
// ──────────────────────────────────────────────────────────────────────────
// Pass angle steps - creates the layout distribution and rotation over time
constexpr float ANGLE_STEP_P2 = 360.0f / 12.0f * DEG2RAD; 
constexpr float ANGLE_STEP_P3 = 360.0f / 12.0f * DEG2RAD; 

// Simulated system offsets (param_3[0x2c] and param_3[0x2d])
constexpr float SHIMMER_OFFSET_X = DEG2RAD * 0.2f; 
constexpr float SHIMMER_OFFSET_Y = DEG2RAD * 0.1f;

// ──────────────────────────────────────────────────────────────────────────
// Correct Hierarchical Model Matrix (Fixes the spiky ball error)
// 1. Position on 2D ring
// 2. Spin rod on its own axis
// 3. Tumble the entire clock group in 3D
// ──────────────────────────────────────────────────────────────────────────
inline glm::mat4 buildRodMatrix(float rodRingAngle, float tiltAngleOffset, float yScale, float selfRotation, float clockGroupPitch, float clockGroupYaw) {
    // Base object (assume rod is a cylinder aligned along Y axis)
    glm::mat4 model(1.0f);
    
    // 1. Master Group Tumble (from system clock/time)
    // The entire clock tilts and yaws in 3D space
    model = glm::rotate(model, clockGroupYaw, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, clockGroupPitch, glm::vec3(1.0f, 0.0f, 0.0f));
    
    // 2. Ring distribution placement
    // Move on the XY plane to the radius distance
    glm::vec3 radialDir = glm::vec3(std::sin(rodRingAngle), std::cos(rodRingAngle), 0.0f);
    model = glm::translate(model, radialDir * ROD_RING_RADIUS);
    
    // 3. Orient radially outward!
    // If it points UP (+Y), rotating around -Z aligns it with the radial vector
    model = glm::rotate(model, rodRingAngle, glm::vec3(0.0f, 0.0f, -1.0f));
    
    // 3.5 Shimmer Tilt (Pass 3 dual-angle ghosting offset)
    model = glm::rotate(model, tiltAngleOffset, glm::vec3(1.0f, 0.0f, 0.0f));
    
    // 4. Local continuous rotation (spinning on its length axis)
    model = glm::rotate(model, selfRotation, glm::vec3(0.0f, 1.0f, 0.0f));
    
    // 5. Active hour squeeze (Scale along Y)
    // To keep the bottom of the cylinder anchored at the ring radius while the top shrinks,
    // we translate along the Y axis by (yScale - 1.0) since the base is at Y = -1.0.
    model = glm::translate(model, glm::vec3(0.0f, yScale - 1.0f, 0.0f));
    model = glm::scale(model, glm::vec3(1.0f, yScale, 1.0f));
    
    return model;
}

// OSDSYS FUN_002730a8: Builds the custom GS-native projection matrix.
inline glm::mat4 buildProjectionMatrix(float fov, float halfWidth, float nearPlane, float aspect) {
    // Note: OSDSYS hardcodes far to 2048.0f (GS internal coordinate space max)
    float farPlane = 2048.0f; 
    
    // Construct base perspective mapping
    glm::mat4 proj = glm::perspective(fov, aspect, nearPlane, farPlane);
    
    // In the real hardware, scale = 65536.0f (Q16.16 GS fixed-point precision) is pushed
    // and combined with halfWidth. For Vulkan, we keep normalized device coordinates (-1 to 1)
    // but we can apply the structural modifications found in the VU code if needed.
    // For now, the standard projection matches the required visual output in modern APIs
    // without the 65536 GS clipping requirement.
    
    return proj;
}

// ──────────────────────────────────────────────────────────────────────────
// Scale Computation (from FUN_00232da0)
// ──────────────────────────────────────────────────────────────────────────

// Re-simplified scale logic matching the visual intent since we don't know the exact VU0 base mesh dimensions
inline float computeRodScale(int rodIndex, float baseScale, bool isWidescreen, 
                             bool isSelected, int hourCounter) {
    float scale = baseScale;
    
    // The PS2 index-based scale logic implies the original mesh was tiny (scaled x3)
    // Since our mesh is already 1.0, we just return baseScale for normal rods.
    
    // Hour Indicator Squeeze (Progress bar over 3600 seconds)
    if (isSelected) {
        float timeRatio = static_cast<float>(hourCounter) / 3600.0f;
        scale *= (1.0f - timeRatio);
        // clamp to avoid negative scaling crossing the ring
        if (scale < 0.001f) scale = 0.001f;
    }
    
    return scale;
}

// ──────────────────────────────────────────────────────────────────────────
// Prism Color Lerp (PS2 Global Color Cycle)
// Loops Deep Blue -> Violet -> Teal continuously every 10 seconds.
// ──────────────────────────────────────────────────────────────────────────
inline glm::vec3 lerpPrismColor(float secondsInMinute) {
    const glm::vec3 PRISM_COLORS[] = {
        { 0.04f, 0.23f, 0.46f },
        { 0.17f, 0.03f, 0.45f },
        { 0.03f, 0.39f, 0.45f }
    };

    float mod = std::fmod(secondsInMinute, 10.0f);
    int colorIdx = (static_cast<int>(secondsInMinute) - static_cast<int>(mod)) % 3;
    float t = mod / 10.0f;
    
    glm::vec3 c1 = PRISM_COLORS[colorIdx];
    glm::vec3 c2 = PRISM_COLORS[(colorIdx + 1) % 3];
    return glm::mix(c1, c2, t);
}

// Highlighted rod = current hour (0-11)
inline int getHighlightedRod(int hour) {
    return hour % 12;
}

} // namespace CrystalMath
