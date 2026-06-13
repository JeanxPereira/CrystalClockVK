// Code-parity test: SwizzleEngine TEXA expansion must equal PCSX2's
// GSLocalMemory::Expand24To32 / Expand16To32 for every input over a sweep of
// TEXA configs (AEM, TA0, TA1).
//
// PCSX2 returns a u32 packed R(0..7) G(8..15) B(16..23) A(24..31); stored
// little-endian that is byte order R,G,B,A — our linear RGBA8 layout.
//
// Reference: pcsx2-ref/pcsx2/GS/GSLocalMemory.h (Expand24To32/Expand16To32).
// GPLv3 (logic reproduced for parity verification only).

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "gs/GsRegisterState.hpp"
#include "gs/SwizzleEngine.hpp"

namespace pcsx2 {

// GSLocalMemory.h Expand24To32 (exact).
static uint32_t expand24To32(uint32_t c, uint8_t ta0, bool aem) {
    return (((!aem | (c & 0xffffff)) ? ta0 : 0u) << 24) | (c & 0xffffff);
}

// GSLocalMemory.h Expand16To32 (exact). 5551 -> 8888, no low-bit replication.
static uint32_t expand16To32(uint16_t c, uint8_t ta0, uint8_t ta1, bool aem) {
    return (((c & 0x8000) ? ta1 : (!aem | c) ? ta0 : 0u) << 24)
        | ((c & 0x7c00) << 9)
        | ((c & 0x03e0) << 6)
        | ((c & 0x001f) << 3);
}

}  // namespace pcsx2

namespace {

int g_fails = 0;
long g_checked = 0;

struct TexaCfg { uint8_t ta0; uint8_t ta1; bool aem; };

const std::vector<TexaCfg> kConfigs = {
    {0x80, 0x80, false}, {0x80, 0x80, true},
    {0x00, 0xFF, false}, {0x00, 0xFF, true},
    {0xFF, 0x00, true},  {0x7F, 0x80, true},
};

// 24-bit RGB samples: include RGB==0 (the AEM branch) and several non-zero.
const std::vector<uint32_t> kRgb24 = {
    0x000000, 0x000001, 0x0000FF, 0x00FF00, 0xFF0000,
    0x123456, 0xABCDEF, 0xFFFFFF, 0x800000, 0x008000,
};

void check24() {
    for (const auto& cfg : kConfigs) {
        GsTexa texa{cfg.ta0, cfg.ta1, cfg.aem};
        for (uint32_t rgb : kRgb24) {
            // Stored word may carry stale alpha in bits 24-31; expansion must mask it.
            const uint32_t stored = rgb | 0x55000000u;
            const uint32_t ours = SwizzleEngine::expand24To32(stored, texa);
            const uint32_t ref = pcsx2::expand24To32(stored, cfg.ta0, cfg.aem);
            g_checked++;
            if (ours != ref) {
                if (g_fails < 16)
                    std::printf("  FAIL 24 c=%08X aem=%d ta0=%02X: ours=%08X ref=%08X\n",
                                stored, cfg.aem, cfg.ta0, ours, ref);
                g_fails++;
            }
        }
    }
}

void check16() {
    for (const auto& cfg : kConfigs) {
        GsTexa texa{cfg.ta0, cfg.ta1, cfg.aem};
        for (uint32_t c = 0; c <= 0xFFFF; c++) {  // exhaustive 16-bit sweep
            const uint32_t ours = SwizzleEngine::expand16To32(static_cast<uint16_t>(c), texa);
            const uint32_t ref = pcsx2::expand16To32(static_cast<uint16_t>(c), cfg.ta0, cfg.ta1, cfg.aem);
            g_checked++;
            if (ours != ref) {
                if (g_fails < 16)
                    std::printf("  FAIL 16 c=%04X aem=%d ta0=%02X ta1=%02X: ours=%08X ref=%08X\n",
                                c, cfg.aem, cfg.ta0, cfg.ta1, ours, ref);
                g_fails++;
            }
        }
    }
}

}  // namespace

int main() {
    check24();
    check16();

    if (g_fails) {
        std::printf("TEXA expand parity: %d/%ld MISMATCHES\n", g_fails, g_checked);
        return 1;
    }
    std::printf("TEXA expand parity: OK (%ld values match PCSX2)\n", g_checked);
    return 0;
}
