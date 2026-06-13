// Unit test for gsvk::translateBlend (Phase 1 W3).
// (1) asserts the clock's three GS blend modes map to the expected VK state;
// (2) proves the VK dual-source emulation matches the GS (A-B)*C/128+D formula
//     in 8-bit integer math, per spec section 7.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "gsvk/GsBlendTranslator.hpp"

namespace {

int g_fails = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("  FAIL: %s\n", what.c_str());
        g_fails++;
    }
}

enum class Mode { SrcOver, Additive, Subtractive };

int clamp8(long v) { return static_cast<int>(std::clamp<long>(v, 0, 255)); }

// GS reference: Cv = ((A - B) * C) >> 7 + D, clamped (COLCLAMP=1). C = As (0..128).
int gsBlend(Mode m, int Cs, int Cd, int As) {
    long t = 0;
    switch (m) {
        case Mode::SrcOver:     t = (static_cast<long>(Cs - Cd) * As >> 7) + Cd; break;
        case Mode::Additive:    t = (static_cast<long>(Cs) * As >> 7) + Cd; break;
        case Mode::Subtractive: t = (static_cast<long>(-Cs) * As >> 7) + Cd; break;
    }
    return clamp8(t);
}

// VK dual-source emulation: SRC1 = As/128 as the per-channel factor.
int vkBlend(Mode m, int Cs, int Cd, int As) {
    const double c = As / 128.0;
    double out = 0.0;
    switch (m) {
        case Mode::SrcOver:     out = Cs * c + Cd * (1.0 - c); break;  // ADD, SRC1, INV_SRC1
        case Mode::Additive:    out = Cs * c + Cd; break;             // ADD, SRC1, ONE
        case Mode::Subtractive: out = Cd - Cs * c; break;            // REV_SUBTRACT, SRC1, ONE
    }
    return clamp8(static_cast<long>(std::lround(out)));
}

int maxDiff(Mode m) {
    int worst = 0;
    for (int Cs = 0; Cs <= 255; Cs += 5)
        for (int Cd = 0; Cd <= 255; Cd += 5)
            for (int As = 0; As <= 128; As += 4)
                worst = std::max(worst, std::abs(gsBlend(m, Cs, Cd, As) - vkBlend(m, Cs, Cd, As)));
    return worst;
}

}  // namespace

int main() {
    using namespace gsvk;

    // ABE off => opaque passthrough.
    check(!translateBlend({0, 1, 0, 1, 128}, false).enable, "ABE off => no blend");

    // src-over (0101): ADD, SRC1_COLOR, INV_SRC1_COLOR.
    {
        auto r = translateBlend({0, 1, 0, 1, 128}, true);
        check(r.enable && r.dualSource, "src-over enabled + dual-source");
        check(r.colorOp == VK_BLEND_OP_ADD, "src-over op=ADD");
        check(r.srcFactor == VK_BLEND_FACTOR_SRC1_COLOR, "src-over src=SRC1");
        check(r.dstFactor == VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR, "src-over dst=INV_SRC1");
    }
    // additive (0201): ADD, SRC1_COLOR, ONE.
    {
        auto r = translateBlend({0, 2, 0, 1, 0}, true);
        check(r.colorOp == VK_BLEND_OP_ADD, "additive op=ADD");
        check(r.srcFactor == VK_BLEND_FACTOR_SRC1_COLOR, "additive src=SRC1");
        check(r.dstFactor == VK_BLEND_FACTOR_ONE, "additive dst=ONE");
    }
    // subtractive (2001): REVERSE_SUBTRACT, SRC1_COLOR, ONE.
    {
        auto r = translateBlend({2, 0, 0, 1, 0}, true);
        check(r.colorOp == VK_BLEND_OP_REVERSE_SUBTRACT, "subtractive op=REV_SUBTRACT");
        check(r.srcFactor == VK_BLEND_FACTOR_SRC1_COLOR, "subtractive src=SRC1");
        check(r.dstFactor == VK_BLEND_FACTOR_ONE, "subtractive dst=ONE");
    }
    // FIX coefficient (C=2) => constant color, no dual-source.
    {
        auto r = translateBlend({0, 1, 2, 1, 64}, true);
        check(!r.dualSource && r.coeff == BlendCoeff::Fix && r.fix == 64, "FIX => const color");
        check(r.srcFactor == VK_BLEND_FACTOR_CONSTANT_COLOR, "FIX src=CONST_COLOR");
    }

    // 8-bit math equivalence: VK emulation vs GS formula (tolerance 1 LSB rounding).
    for (auto [m, name] : {std::pair{Mode::SrcOver, "src-over"},
                           std::pair{Mode::Additive, "additive"},
                           std::pair{Mode::Subtractive, "subtractive"}}) {
        const int d = maxDiff(m);
        std::printf("  blend %-12s max |GS - VK| = %d LSB\n", name, d);
        check(d <= 1, std::string("8-bit match within 1 LSB: ") + name);
    }

    if (g_fails) {
        std::printf("gsvk blend test: %d FAILURES\n", g_fails);
        return 1;
    }
    std::printf("gsvk blend test: OK\n");
    return 0;
}
