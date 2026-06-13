// Texture-read parity: PSMCT24 deswizzle (the GS ReadTexel24 path) must expand
// alpha via TEXA, not a hardcoded 0x80. With the clock's TEXA (TA0=0x7f, AEM=1)
// a black texel (RGB==0) is transparent (alpha 0) and any other texel is 0x7f.
//
// Reference: pcsx2-ref GSLocalMemory ReadTexel24 -> Expand24To32.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "gs/GsRegisterState.hpp"
#include "gs/SwizzleEngine.hpp"

namespace {

int g_fails = 0;
void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("  FAIL: %s\n", what.c_str()); g_fails++; }
}

}  // namespace

int main() {
    const int w = 64, h = 48;  // > one PSMCT32 page (64x32) in y

    // Linear RGBA source: a deterministic pattern with deliberate RGB==0 holes.
    // Stored alpha is intentionally non-zero to prove it is ignored (TEXA wins).
    std::vector<uint8_t> src(size_t(w) * h * 4);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t* p = &src[(size_t(y) * w + x) * 4];
            const bool hole = ((x + y) % 5) == 0;  // RGB==0 pixels
            p[0] = hole ? 0 : uint8_t(x * 5 + 1);
            p[1] = hole ? 0 : uint8_t(y * 3 + 2);
            p[2] = hole ? 0 : uint8_t((x ^ y) + 7);
            p[3] = 0x33;  // stale stored alpha — must be discarded
        }
    }

    auto sw = SwizzleEngine::swizzle(src.data(), w, h, w, GsPixelFormat::PSMCT24);

    // Clock TEXA: TA0=0x7f, TA1=0x81, AEM=1.
    const GsTexa texa{0x7f, 0x81, true};
    auto de = SwizzleEngine::deswizzle(sw.data(), w, h, w, GsPixelFormat::PSMCT24, nullptr, texa);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint8_t* s = &src[(size_t(y) * w + x) * 4];
            const uint8_t* d = &de[(size_t(y) * w + x) * 4];
            const bool rgbZero = (s[0] | s[1] | s[2]) == 0;
            const uint8_t wantA = rgbZero ? 0x00 : 0x7f;
            const std::string at = "(" + std::to_string(x) + "," + std::to_string(y) + ")";
            check(d[0] == s[0] && d[1] == s[1] && d[2] == s[2], "RGB preserved " + at);
            check(d[3] == wantA, "TEXA alpha " + at +
                  " got=" + std::to_string(d[3]) + " want=" + std::to_string(wantA));
        }
    }

    // Default TEXA must still behave as ReadFrame24 (constant 0x80).
    auto deDefault = SwizzleEngine::deswizzle(sw.data(), w, h, w, GsPixelFormat::PSMCT24, nullptr);
    check(deDefault[3] == 0x80, "default TEXA = 0x80 (ReadFrame24)");

    if (g_fails) { std::printf("PSMCT24 TEXA deswizzle: %d FAILURES\n", g_fails); return 1; }
    std::printf("PSMCT24 TEXA deswizzle: OK\n");
    return 0;
}
