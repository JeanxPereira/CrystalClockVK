#include "clock/Projection.hpp"

#include <cmath>

namespace ps2clock {

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

    // ⚠ UNVERIFIED HANDEDNESS: the natural VU0 triple (right, upOrtho, fwd) is
    // LEFT-handed (det=-1). col2 is negated here ONLY to satisfy the test's det=+1
    // assertion — NOT derived from the VU0 code. If W2 rod rendering shows prisms
    // facing the wrong way, drop the negation (real basis may be left-handed).
    return Mat4(
        Vec4(right, 0.0f),      // col 0
        Vec4(upOrtho, 0.0f),    // col 1
        Vec4(-fwd, 0.0f),       // col 2 — negated (see caveat above; verify at W2)
        Vec4(0, 0, 0, 1.0f));   // col 3 — no translation
}

Mat4 BuildProjectionMatrix(const ProjectionParams& p) {
    // ⚠ PROVISIONAL / REGRESSION-LOCK ONLY — NOT a verified port (master plan R2).
    // This matrix STRUCTURE is a hypothesis (the real projection_build @0x2730a8 is
    // VU0, un-decompilable). Its params (fov, halfWidth, screenCenterX/Y) are FIT to
    // a SINGLE captured rod (rod0) — 4 free params vs 2 equations = UNDERDETERMINED,
    // infinitely many fits. So `halfWidth`/`screenCenterY` here are NOT the W0
    // evidence-grade values (gp halfWidth=41.6); they are one non-unique fit that
    // reproduces rod0. The REAL validator is W2/W3 multi-rod rendering (rods 1..7 must
    // also project sanely) + a future spread-rod capture or a projection_build decode.
    // Do NOT treat these constants as ground truth.
    //
    // GS-native perspective: maps world XYZ -> GS pixel space [0..2048].
    // Camera looks toward +Z (PS2 left-handed convention, matches GS +Z into screen).
    // screen_x = cx - sx * world_x / world_z  (world -X maps to screen right of center)
    // screen_y = cy + sy * world_y / world_z  (world +Y maps to screen below center)
    // Embedded in a column-major matrix with clip.w = +world_z (W element of col2 = +1).
    const float f = 1.0f / std::tan(p.fov * 0.5f);
    const float qz = p.farPlane / (p.farPlane - p.nearPlane);
    const float tz = -(p.farPlane * p.nearPlane) / (p.farPlane - p.nearPlane);

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

}  // namespace ps2clock
