// The 8 orbiting light spots at the clock centre (US6693606 element 308,
// live array 0x34c830). Pure logic: positions on a ring + fading-trail mesh.

#include <cmath>
#include <cstdio>
#include <string>

#include "clock/ClockOrb.hpp"

namespace {
int g_fails = 0;
void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("  FAIL: %s\n", what.c_str()); g_fails++; }
}
bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
}  // namespace

int main() {
    using namespace ps2clock;

    // 8 spots, 45deg apart on the ring, all at radius, in the dial plane (z=0).
    auto s0 = ClockOrb::lightSpots(0.0f, 1.25f);
    check(s0.size() == 8, "8 light spots");
    for (const Vec3& p : s0) {
        check(near(std::sqrt(p.x*p.x + p.y*p.y), 1.25f), "spot on the radius ring");
        check(near(p.z, 0.0f), "spot in the dial plane");
    }
    // Even 45deg spacing: consecutive spots' angular gap is 2pi/8.
    const float gap = std::cos(2.0f * 3.14159265f / 8.0f);
    float d = (s0[0].x*s0[1].x + s0[0].y*s0[1].y) / (1.25f * 1.25f);
    check(near(d, gap, 1e-4f), "spots 45deg apart");

    // Phase rotates the whole ring rigidly (spot 0 moves onto where the ring was).
    auto sp = ClockOrb::lightSpots(2.0f * 3.14159265f / 8.0f, 1.25f);
    check(near(sp[0].x, s0[1].x, 1e-4f) && near(sp[0].y, s0[1].y, 1e-4f),
          "phase = one step rotates spot 0 onto spot 1's place");

    if (g_fails) { std::printf("FAILED (%d)\n", g_fails); return 1; }
    std::printf("OK\n");
    return 0;
}
