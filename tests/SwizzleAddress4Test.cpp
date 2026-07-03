// PSMT4 (4-bit indexed) swizzle: address math + round-trip.
// Layout (PCSX2 GSTables.cpp): page 128x128 px, block 32x16 px, 4x8 blocks per
// page (blockTable4), 512 nibbles per 256-byte block (columnTable4). Addresses
// are in NIBBLES; even nibble = low 4 bits of the byte (PCSX2 ReadPixel4).
// The clock's text font is PSMT4: TBP0=12037, 256x512, TBW=4, CLUT at 0x2f00.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "gs/SwizzleEngine.hpp"

namespace {

int g_fails = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("  FAIL: %s\n", what.c_str());
        g_fails++;
    }
}

}  // namespace

int main() {
    // Invariants from GSTables.cpp columnTable4 row 0 / blockTable4:
    // pixel (0,0) -> nibble 0; (1,0) -> 8; (2,0) -> 32; (8,0) -> 2 (row 0 col 8).
    check(SwizzleEngine::psmt4NibbleAddress(0, 0, 256) == 0, "(0,0) nibble 0");
    check(SwizzleEngine::psmt4NibbleAddress(1, 0, 256) == 8, "(1,0) nibble 8");
    check(SwizzleEngine::psmt4NibbleAddress(2, 0, 256) == 32, "(2,0) nibble 32");
    check(SwizzleEngine::psmt4NibbleAddress(8, 0, 256) == 2, "(8,0) nibble 2");
    // Block step: (32,0) is blockTable4[0][1] = 2 -> nibble 2*512.
    check(SwizzleEngine::psmt4NibbleAddress(32, 0, 256) == 2 * 512, "(32,0) block 2");
    // Row-of-blocks step: (0,16) is blockTable4[1][0] = 1 -> nibble 512.
    check(SwizzleEngine::psmt4NibbleAddress(0, 16, 256) == 512, "(0,16) block 1");
    // Page step at stride 256 (2 pages/row): (128,0) -> page 1 -> +32*512 nibbles.
    check(SwizzleEngine::psmt4NibbleAddress(128, 0, 256) == 32 * 512, "(128,0) page 1");
    // Page-row step: (0,128) -> page 2 at stride 256.
    check(SwizzleEngine::psmt4NibbleAddress(0, 128, 256) == 2 * 32 * 512, "(0,128) page 2");

    // Round-trip: swizzle a 256x128 4bpp pattern, deswizzle, compare.
    {
        const int W = 256, H = 128;
        std::vector<uint8_t> indices(W * H);
        for (int i = 0; i < W * H; i++) indices[i] = uint8_t((i * 7 + (i >> 5)) & 0xf);

        std::vector<uint8_t> vram(W * H / 2);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                const uint32_t nib = SwizzleEngine::psmt4NibbleAddress(x, y, W);
                uint8_t& b = vram[nib >> 1];
                const uint8_t v = indices[y * W + x];
                b = (nib & 1) ? uint8_t((b & 0x0f) | (v << 4)) : uint8_t((b & 0xf0) | v);
            }

        // Identity CLUT: index i -> RGBA (i*17, i, 255-i, i*16).
        std::vector<uint8_t> clut(256 * 4, 0);
        for (int i = 0; i < 16; i++) {
            clut[i * 4] = uint8_t(i * 17); clut[i * 4 + 1] = uint8_t(i);
            clut[i * 4 + 2] = uint8_t(255 - i); clut[i * 4 + 3] = uint8_t(i * 16);
        }
        const std::vector<uint8_t> rgba = SwizzleEngine::deswizzle(
            vram.data(), W, H, W, GsPixelFormat::PSMT4, clut.data());

        int bad = 0;
        for (int i = 0; i < W * H && bad < 5; i++) {
            const uint8_t v = indices[i];
            if (rgba[i * 4] != uint8_t(v * 17) || rgba[i * 4 + 1] != v ||
                rgba[i * 4 + 2] != uint8_t(255 - v) || rgba[i * 4 + 3] != uint8_t(v * 16)) {
                std::printf("  FAIL: px %d idx %d got %d,%d,%d,%d\n", i, v,
                            rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2], rgba[i * 4 + 3]);
                bad++;
            }
        }
        check(bad == 0, "PSMT4 deswizzle round-trip");
    }

    if (g_fails) {
        std::printf("FAILED (%d)\n", g_fails);
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
