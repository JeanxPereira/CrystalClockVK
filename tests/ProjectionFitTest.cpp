// Projection FIT gate. The custom GS-native projection (FUN_002730a8) maps
// world -> GS screen [0,2048]. Oracle from runtime-trace.md rod0:
//   world (-13.039, 14.666, 50.271) -> screen (1915.20, 2118.20)
//                                    -> 12.4 fixed (0x77b3, 0x8463).
// fov/near/halfWidth are runtime globals (still being resolved upstream); they
// are FITTED here against this single fully-decoded oracle row and locked as a
// regression. Tolerance is +/-3.0 px (free-parameter slack, vu0-math §Blockers 1&3).

#include <cstdio>
#include <string>
#include <cmath>

#include "clock/ClockMath.hpp"
#include "clock/Projection.hpp"

namespace {

int g_fails = 0;
void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("  FAIL: %s\n", what.c_str()); g_fails++; }
}

// Fitted parameters (locked). If a future ground-truth read changes fov/near/center,
// update these lines and the tolerance comment together.
constexpr float kFitFovRadians   = 0.296f;   // ~17 deg; fitted from rod0 oracle
constexpr float kFitNear         = 1.0f;
constexpr float kFitHalfWidth    = 512.0f;   // 4:3 path, fitted
constexpr float kFitScreenCenterX = 1024.0f; // GS OFSX (fitted; matches confirmed cx)
constexpr float kFitScreenCenterY = 1116.3f; // GS OFSY (fitted; cx != cy confirmed by rod0)

ps2clock::ProjectionParams fittedParams() {
    ps2clock::ProjectionParams p;
    p.fov = kFitFovRadians;
    p.nearPlane = kFitNear;
    p.halfWidth = kFitHalfWidth;
    p.screenCenterX = kFitScreenCenterX;
    p.screenCenterY = kFitScreenCenterY;
    return p;  // far=2048, aspect=1 from defaults
}

}  // namespace

int main() {
    const ps2clock::Mat4 proj = ps2clock::BuildProjectionMatrix(fittedParams());

    const ps2clock::Vec3 rod0World(-13.039f, 14.666f, 50.271f);
    const glm::vec2 screen = ps2clock::ProjectWorldToScreen(proj, rod0World);

    const float kOracleX = 1915.20f;
    const float kOracleY = 2118.20f;
    const float kTolPx = 3.0f;

    std::printf("  rod0 projected screen = (%.3f, %.3f), oracle = (%.3f, %.3f)\n",
                screen.x, screen.y, kOracleX, kOracleY);

    check(std::fabs(screen.x - kOracleX) <= kTolPx, "rod0 screen X within 3.0 px");
    check(std::fabs(screen.y - kOracleY) <= kTolPx, "rod0 screen Y within 3.0 px");

    // 12.4 fixed-point oracle bits (after the same projection).
    const int32_t fx = ps2clock::Fixed124::encode(screen.x);
    const int32_t fy = ps2clock::Fixed124::encode(screen.y);
    // Within the +/-3px*16 = +/-48 fixed-quantum band around the captured bits.
    check(std::abs(fx - 0x77b3) <= 48, "rod0 12.4 X near 0x77b3");
    check(std::abs(fy - 0x8463) <= 48, "rod0 12.4 Y near 0x8463");

    if (g_fails) { std::printf("projection fit: %d FAILURES\n", g_fails); return 1; }
    std::printf("projection fit: OK\n");
    return 0;
}
