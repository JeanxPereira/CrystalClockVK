#pragma once

#include "clock/ClockMath.hpp"

namespace ps2clock {

// GS-native projection parameters. Evidence-grade constants from
// vu0-math-pipeline.md §"Confirmed constants": far=2048, aspect=1, scale=65536.
// fov/near are runtime globals (gp[-0x73d8]/gp[-0x7b78]); fitted in ProjectionFitTest.
struct ProjectionParams {
    float fov;              // radians, *gp[-0x73d8] (fitted)
    float nearPlane;        // *gp[-0x7b78] (fitted); renamed from 'near' — Windows <minwindef.h> #defines near
    float halfWidth;        // *gp[-0x7b80] (4:3) / *gp[-0x7b7c] (16:9)
    float farPlane = 2048.0f; // renamed from 'far' — Windows <minwindef.h> #defines far
    float aspect = 1.0f;
    float screenCenterX = 1024.0f;  // GS draw area 0..2048, center 1024
    float screenCenterY = 1024.0f;
};

// rotation_build (FUN_002732d8): orthonormal basis from two direction vectors.
Mat4 BuildRotationMatrix(const Vec3& forward, const Vec3& up);

// projection_build (FUN_002730a8): custom GS-native perspective that embeds the
// viewport transform (NDC -> GS [0,2048]). Returns a column-major Mat4.
Mat4 BuildProjectionMatrix(const ProjectionParams& p);

// Full world -> GS screen pixel projection (proj * world, perspective divide,
// NDC -> GS pixel). Returns screen XY in GS pixels (pre-12.4-fixed).
glm::vec2 ProjectWorldToScreen(const Mat4& proj, const Vec3& world);

}  // namespace ps2clock
