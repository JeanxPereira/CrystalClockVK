#include "clock/Projection.hpp"

#include <cmath>

namespace clock {

Mat4 BuildRotationMatrix(const Vec3& forward, const Vec3& up) {
    // bc1t -> memclr overflow guard: degenerate inputs zero the matrix.
    Vec3 fwd = glm::normalize(forward);
    Vec3 rightRaw = glm::cross(fwd, up);          // VOPMSUB @ 0x273318
    float rl = glm::length(rightRaw);
    if (!(rl > 1e-6f) || std::isnan(fwd.x) || std::isnan(rl)) {
        return Mat4(0.0f);
    }
    Vec3 right = rightRaw / rl;
    Vec3 upOrtho = glm::cross(right, fwd);        // VOPMSUB @ 0x273370

    return Mat4(
        Vec4(right, 0.0f),      // col 0
        Vec4(upOrtho, 0.0f),    // col 1
        Vec4(-fwd, 0.0f),       // col 2 — negated so det(R)=+1 (right-handed basis)
        Vec4(0, 0, 0, 1.0f));   // col 3 — no translation
}

Mat4 BuildProjectionMatrix(const ProjectionParams& p) {
    // GS-native perspective: maps world XYZ -> GS pixel space [0..2048].
    // Camera looks toward +Z (PS2 left-handed convention, matches GS +Z into screen).
    // screen_x = cx - sx * world_x / world_z  (world -X maps to screen right of center)
    // screen_y = cy + sy * world_y / world_z  (world +Y maps to screen below center)
    // Embedded in a column-major matrix with clip.w = +world_z (W element of col2 = +1).
    const float f = 1.0f / std::tan(p.fov * 0.5f);
    const float qz = p.far / (p.far - p.near);
    const float tz = -(p.far * p.near) / (p.far - p.near);

    const float sx = f * p.halfWidth;
    const float sy = (f * p.halfWidth) / p.aspect;

    // Column-major (PS2 VU0 stores columns via SQC2).
    // col2 W=+1 → clip.w = +world_z → perspective divide lands in GS pixel space.
    // col0 uses -sx so that clip.x/clip.w = cx - sx*world_x/world_z.
    return Mat4(
        Vec4(-sx, 0, 0, 0),                                  // col 0
        Vec4(0, sy, 0, 0),                                   // col 1
        Vec4(p.screenCenterX, p.screenCenterY, qz, 1.0f),    // col 2
        Vec4(0, 0, tz, 0));                                  // col 3
}

glm::vec2 ProjectWorldToScreen(const Mat4& proj, const Vec3& world) {
    Vec4 clip = proj * Vec4(world, 1.0f);
    float invW = (clip.w != 0.0f) ? (1.0f / clip.w) : 0.0f;
    // Perspective divide already lands XY in GS pixel space (center embedded).
    return glm::vec2(clip.x * invW, clip.y * invW);
}

}  // namespace clock
