// 12.4 fixed-point conversion: GS encodes screen XY as trunc(value * 16).
// Oracle bit patterns from runtime-trace.md rod0: 1915.19 -> 0x77b3, 2118.19 -> 0x8463.

#include <cstdint>
#include <cstdio>
#include <string>

#include "clock/ClockMath.hpp"

namespace {

int g_fails = 0;
void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("  FAIL: %s\n", what.c_str()); g_fails++; }
}

}  // namespace

int main() {
    using clock::Fixed124;

    check(Fixed124::encode(1915.1875f) == 0x77b3, "encode 1915.1875 -> 0x77b3");
    check(Fixed124::encode(2118.1875f) == 0x8463, "encode 2118.1875 -> 0x8463");
    check(Fixed124::encode(0.0f) == 0, "encode 0.0 -> 0");
    check(Fixed124::encode(1.0f) == 16, "encode 1.0 -> 16");

    check(Fixed124::decode(0x77b3) == 30643.0f / 16.0f, "decode 0x77b3");
    check(Fixed124::decode(16) == 1.0f, "decode 16 -> 1.0");

    for (float x = 0.0f; x < 2200.0f; x += 0.37f) {
        float rt = Fixed124::decode(Fixed124::encode(x));
        check(x - rt < 1.0f / 16.0f + 1e-4f && rt - x <= 1e-4f,
              "round-trip within quantum at " + std::to_string(x));
    }

    if (g_fails) { std::printf("fixed12.4: %d FAILURES\n", g_fails); return 1; }
    std::printf("fixed12.4: OK\n");
    return 0;
}
