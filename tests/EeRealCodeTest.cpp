#include "ee/EeInterpreter.hpp"
#include "ee/EeMemory.hpp"
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace ps2ee;

int main() {
    EeMemory mem;
    if (!mem.loadImage("re/ram/clock/eeMemory.bin")) {
        std::printf("SKIPPED: re/ram/clock/eeMemory.bin not present\n");
        return 0;
    }
    constexpr uint32_t kDst = 0x01FE0000, kM = 0x01FE0100, kSrc = 0x01FE0200;

    // sceVu0MulMatrix(dst, m, src): out column j = sum_k m.row(k)*src[k][j]
    float m[16], src[16];
    for (int i = 0; i < 16; i++) { m[i] = 0.25f * i - 1.0f; src[i] = 1.5f * i + 0.5f; }
    for (int i = 0; i < 16; i++) {
        mem.write32(kM + i * 4, std::bit_cast<uint32_t>(m[i]));
        mem.write32(kSrc + i * 4, std::bit_cast<uint32_t>(src[i]));
    }
    EeInterpreter cpu(mem);
    cpu.call(0x002738A0u, kDst, kM, kSrc);
    for (int col = 0; col < 4; col++)
        for (int j = 0; j < 4; j++) {
            float expect = 0.f;
            for (int k = 0; k < 4; k++) expect += m[k * 4 + j] * src[col * 4 + k];
            float got = std::bit_cast<float>(mem.read32(kDst + (col * 4 + j) * 4));
            assert(std::fabs(got - expect) <= 1e-4f * std::max(1.f, std::fabs(expect)));
        }

    // sceVu0ApplyMatrix(out, m, vec): one column of the above
    float v4[4] = {1.f, -2.f, 3.f, 1.f};
    for (int i = 0; i < 4; i++) mem.write32(kSrc + i * 4, std::bit_cast<uint32_t>(v4[i]));
    EeInterpreter cpu2(mem);
    cpu2.call(0x002738E8u, kDst, kM, kSrc);
    for (int j = 0; j < 4; j++) {
        float expect = 0.f;
        for (int k = 0; k < 4; k++) expect += m[k * 4 + j] * v4[k];
        float got = std::bit_cast<float>(mem.read32(kDst + j * 4));
        assert(std::fabs(got - expect) <= 1e-4f * std::max(1.f, std::fabs(expect)));
    }

    // memcpy @0x26753c (lq/sq loop): copy 64 bytes, verify byte-exact
    for (uint32_t i = 0; i < 64; i++) mem.write8(kSrc + i, uint8_t(i * 7 + 3));
    EeInterpreter cpu3(mem);
    cpu3.call(0x0026753Cu, kDst, kSrc, 64);
    for (uint32_t i = 0; i < 64; i++) {
        const uint8_t got = uint8_t(mem.read32(kDst + (i & ~3u)) >> ((i & 3u) * 8));
        assert(got == uint8_t(i * 7 + 3));
    }

    std::printf("ee_realcode (Gate A): all assertions passed\n");
    return 0;
}
