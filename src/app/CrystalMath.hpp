#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include "TimeSync.hpp"
#include "gs/GsCrystalMath.hpp"
#include "gs/GsConstants.hpp"

namespace CrystalMath {

constexpr int ROD_COUNT = 12;

constexpr float PI = glm::pi<float>();
constexpr float TAU = glm::two_pi<float>();
constexpr float DEG2RAD = PI / 180.0f;

constexpr float ROD_RING_RADIUS = 6.5f;

constexpr float CAMERA_FOV = 70.0f;
constexpr float CAMERA_Z = 30.0f;
constexpr float CAMERA_NEAR = 0.1f;
constexpr float CAMERA_FAR = 100.0f;

// ──────────────────────────────────────────────────────────────────────────
// Tunnel transform — matches Raylib's TM matrix
// ──────────────────────────────────────────────────────────────────────────
inline glm::mat4 buildTunnelMatrix() {
    glm::mat4 M = glm::mat4(1.0f);
    M = glm::translate(M, glm::vec3(0.0f, 0.0f, 30.0f));
    M = glm::rotate(M, PI + PI / 2.0f, glm::vec3(1.0f, 0.0f, 0.0f));
    return M;
}

// ──────────────────────────────────────────────────────────────────────────
// Rod Matrix Builder — VU0-accurate Azimuth/Elevation rotation
//
// Replaces old Euler Z→Y→Z chain with OSDSYS-accurate axis-angle rotation
// built from cross-product orthogonalization (VOPMSUB).
//
// angleA and angleB are the two rotation inputs:
//   Pass 2: angleA == angleB (symmetric)
//   Pass 3: angleA != angleB (shimmer offset from param_3[0x2c]/[0x2d])
// ──────────────────────────────────────────────────────────────────────────
inline glm::mat4 buildRodMatrix(float angleA, float angleB) {
    return GsCrystalMath::buildRotation(angleA, angleB);
}

// ──────────────────────────────────────────────────────────────────────────
// Per-pass angle for a rod
// OSDSYS: baseAngle + rodIndex * passAngleStep
// ──────────────────────────────────────────────────────────────────────────
inline float computeRodAngle(float baseAngle, int rodIndex, float angleStep) {
    return GsCrystalMath::computePassAngle(baseAngle, rodIndex, angleStep);
}

// ──────────────────────────────────────────────────────────────────────────
// Build the full model matrix for a rod including translation and scale.
// ──────────────────────────────────────────────────────────────────────────
inline glm::mat4 buildFullRodModel(const glm::mat4& orbitMatrix, float yScale) {
    glm::mat4 localTransform = glm::mat4(1.0f);
    localTransform = glm::scale(localTransform, glm::vec3(1.0f, yScale, 1.0f));
    localTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, ROD_RING_RADIUS, 0.0f)) * localTransform;
    return orbitMatrix * localTransform;
}

// ──────────────────────────────────────────────────────────────────────────
// Rod Scale — delegates to gs/GsCrystalMath with OSDSYS constants
// ──────────────────────────────────────────────────────────────────────────
inline float computeRodScale(int rodIndex, float baseScale, bool isWidescreen,
                             int screenRatio, bool isSelected, int hourCounter) {
    return GsCrystalMath::computeRodScale(rodIndex, baseScale, isWidescreen,
                                           screenRatio, isSelected, hourCounter);
}

// ──────────────────────────────────────────────────────────────────────────
// Clock rotation helpers
// ──────────────────────────────────────────────────────────────────────────
inline float lerpClockRotation(float secondsInMinute) {
    return (secondsInMinute / 60.0f) * TAU;
}

inline float getClockRotationAngle(int hour) {
    float h = static_cast<float>(hour < 12 ? hour : hour - 12);
    return h * (-TAU / 12.0f);
}

// Highlighted rod — the OSDSYS uses a per-rod flag at +0x150.
// For now, rod 0 sits at the current hour position after rotation.
inline int getHighlightedRod(int) {
    return 0;
}

// ──────────────────────────────────────────────────────────────────────────
// Prism Color Lerp — Deep Blue → Violet → Teal every 10 seconds.
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

    return glm::mix(PRISM_COLORS[colorIdx], PRISM_COLORS[(colorIdx + 1) % 3], t);
}

// ──────────────────────────────────────────────────────────────────────────
// Prism scale — simple hour countdown (kept as fallback)
// Prefer GsCrystalMath::computeRodScale for OSDSYS accuracy.
// ──────────────────────────────────────────────────────────────────────────
inline float lerpPrismScale(float secondsInHour) {
    return 1.0f - secondsInHour / 3600.0f;
}

// ──────────────────────────────────────────────────────────────────────────
// Orb Math — ported from Raylib clock.cpp
// ──────────────────────────────────────────────────────────────────────────
constexpr int ORBS = 7;
constexpr int TRAIL_SEGMENTS = 120;
constexpr int TRAIL_POINTS = TRAIL_SEGMENTS + 1;
constexpr float TRAIL_WIDTH = 1.0f;
constexpr float ORB_SCALE = 2.5f;

constexpr float MAX_SPHERE_RADIUS = 6.0f;
constexpr float MIN_SPHERE_RADIUS = MAX_SPHERE_RADIUS / 2.0f;

constexpr float ORB_X_SPEED = PI / 2.0f;
constexpr float ORB_Z_SPEED = -PI;
constexpr float ORB_ANGLE_STEP = 360.0f / 60.0f * DEG2RAD;
const float ORB_ANGLES[] = { PI / 2.0f, PI + PI / 6.0f, 0.0f };

inline float getOrbRotationAngle(float time, int i) {
    return time * i * ORB_ANGLE_STEP;
}

inline float lerpXRotationAngle(const TimeInfo& t, float smoothedSeconds) {
    float angle1 = ORB_ANGLES[t.minute % 3];
    float angle2 = ORB_ANGLES[(t.minute + 1) % 3];
    float t_norm = smoothedSeconds / 60.0f;
    return glm::mix(angle1, angle2, t_norm);
}

inline glm::mat4 buildOrbRotationMatrix(const TimeInfo& timeInfo) {
    float smoothSeconds = timeInfo.minute * 60.0f + timeInfo.secondsInMinute;
    float hourRotRad = static_cast<float>(timeInfo.hour % 12) * -30.0f * DEG2RAD;

    float ax = ORB_X_SPEED * smoothSeconds + lerpXRotationAngle(timeInfo, smoothSeconds);
    float az = ORB_Z_SPEED * smoothSeconds;

    glm::mat4 rx = glm::rotate(glm::mat4(1.0f), ax, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::mat4 rz = glm::rotate(glm::mat4(1.0f), az, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 rHour = glm::rotate(glm::mat4(1.0f), hourRotRad, glm::vec3(0.0f, 0.0f, 1.0f));

    return rHour * rx * rz;
}

inline glm::vec3 getOrbPosition(float timeInMinuteSeconds, float radius, int orbIndex, const glm::mat4& rotation) {
    float angle = getOrbRotationAngle(timeInMinuteSeconds, orbIndex);
    glm::vec3 pos(radius * std::cos(angle), radius * std::sin(angle), 0.0f);
    return glm::vec3(rotation * glm::vec4(pos, 1.0f));
}

inline float lerpSphereRadius(float secondsInHour) {
    float t = secondsInHour / 3600.0f;
    return glm::mix(MIN_SPHERE_RADIUS, MAX_SPHERE_RADIUS, t);
}

} // namespace CrystalMath
