// Round-trip test for the GS swizzle (foundation of the VRAM texture-cache model).
// swizzle -> deswizzle must be identity, including write-back into a VRAM buffer at
// an offset (the GS unified-memory write-back/read path the model relies on).

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "gs/SwizzleEngine.hpp"

namespace {

int g_fails = 0;
void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("  FAIL: %s\n", what.c_str()); g_fails++; }
}

// Deterministic per-pixel gradient (distinct-ish RGBA so a wrong address shows).
std::vector<uint8_t> gradient(int w, int h) {
    std::vector<uint8_t> img(size_t(w) * h * 4);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t* p = &img[(size_t(y) * w + x) * 4];
            p[0] = uint8_t(x * 7 + 1);
            p[1] = uint8_t(y * 13 + 2);
            p[2] = uint8_t((x ^ y) * 3 + 5);
            p[3] = uint8_t((x + y) | 0x80);
        }
    return img;
}

void roundTrip(int w, int h) {
    const std::vector<uint8_t> src = gradient(w, h);

    // 1) swizzle -> deswizzle (contiguous buffer).
    auto sw = SwizzleEngine::swizzle(src.data(), w, h, w, GsPixelFormat::PSMCT32);
    auto de = SwizzleEngine::deswizzle(sw.data(), w, h, w, GsPixelFormat::PSMCT32, nullptr);
    check(de == src, "swizzle->deswizzle identity " + std::to_string(w) + "x" + std::to_string(h));

    // 2) write-back into a VRAM buffer at an offset, then read it back (the model path).
    std::vector<uint8_t> vram(size_t(w) * h * 4 + 0x4000, 0);
    const size_t base = 0x1800;  // arbitrary non-zero VRAM offset
    SwizzleEngine::swizzleInto(vram.data() + base, vram.size() - base, src.data(),
                               w, h, w, GsPixelFormat::PSMCT32);
    auto de2 = SwizzleEngine::deswizzle(vram.data() + base, w, h, w, GsPixelFormat::PSMCT32, nullptr);
    check(de2 == src, "swizzleInto->deswizzle identity " + std::to_string(w) + "x" + std::to_string(h));
}

}  // namespace

int main() {
    roundTrip(64, 64);
    roundTrip(128, 128);
    roundTrip(640, 224);  // GS framebuffer dims

    if (g_fails) { std::printf("swizzle round-trip: %d FAILURES\n", g_fails); return 1; }
    std::printf("swizzle round-trip: OK\n");
    return 0;
}
