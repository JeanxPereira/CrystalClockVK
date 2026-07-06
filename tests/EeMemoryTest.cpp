#include "ee/EeMemory.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

using ps2ee::EeMemory;

int main() {
    // translation: uncached window and kseg mirrors collapse to physical
    assert(EeMemory::translate(0x20297220u) == 0x00297220u);
    assert(EeMemory::translate(0x80232618u) == 0x00232618u);
    assert(EeMemory::translate(0xA0001000u) == 0x00001000u);
    assert(EeMemory::translate(0x00232618u) == 0x00232618u);
    assert(EeMemory::translate(0x1000A000u) == 0x1000A000u);  // VIF1 FIFO = MMIO

    EeMemory mem;
    assert(!mem.isRam(0x1000A000u));

    // fresh memory (no image): zeroed RAM read/write roundtrip + store log
    assert(mem.loadImage("__no_such_file__") == false);
    mem.storeLogEnabled = true;
    mem.write32(0x00100000u, 0xDEADBEEFu);
    assert(mem.read32(0x00100000u) == 0xDEADBEEFu);
    assert(mem.read32(0x20100000u) == 0xDEADBEEFu);  // uncached mirror sees it
    assert(mem.storeLog.size() == 1 && mem.storeLog[0].physAddr == 0x00100000u
           && mem.storeLog[0].size == 4);

    // MMIO write intercepted, not stored
    bool hit = false;
    mem.onMmio = [&](const ps2ee::MmioAccess& a) {
        hit = a.isWrite && a.addr == 0x1000A000u && a.value == 0x42 && a.size == 4;
    };
    mem.write32(0x1000A000u, 0x42u);
    assert(hit);
    assert(mem.storeLog.size() == 1);

    bool boundaryMmioDamageHit = false;
    mem.onMmio = [&](const ps2ee::MmioAccess& a) {
        if (a.addr == EeMemory::kRamSize - 1 && a.isWrite && a.size == 4) {
            boundaryMmioDamageHit = true;
        }
    };
    mem.storeLogEnabled = true;
    const size_t storeLogSizeBefore = mem.storeLog.size();
    mem.write32(0x00000000u + EeMemory::kRamSize - 1, 0x11223344u);
    assert(boundaryMmioDamageHit);
    assert(mem.storeLog.size() == storeLogSizeBefore);

    assert(mem.read32(0x00000000u + EeMemory::kRamSize - 2) == 0);

    // Scratchpad RAM (SPR): fixed window at 0x70000000, NOT translate()'d /
    // NOT aliased onto the 0x10000000 MMIO range the naive mask would produce.
    {
        EeMemory spr;
        bool sprHitMmio = false;
        spr.onMmio = [&](const ps2ee::MmioAccess&) { sprHitMmio = true; };
        spr.storeLogEnabled = true;

        const uint32_t payload[4] = {0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u};
        spr.write128(0x70000000u, payload);
        assert(!sprHitMmio);
        uint32_t out[4] = {};
        spr.read128(0x70000000u, out);
        assert(!sprHitMmio);
        assert(std::memcmp(out, payload, 16) == 0);
        // Visible directly via sprData(), at offset 0 of the 16KB window.
        assert(std::memcmp(spr.sprData(), payload, 16) == 0);
        assert(spr.storeLog.size() == 1 && spr.storeLog[0].physAddr == 0x70000000u
               && spr.storeLog[0].size == 16);

        // Regression: the real D2 (GIF) DMA kick register at 0x1000A000 must
        // still route to MMIO, not get swallowed as an SPR access.
        bool kickHitMmio = false;
        uint32_t kickAddr = 0;
        spr.onMmio = [&](const ps2ee::MmioAccess& a) { kickHitMmio = true; kickAddr = a.addr; };
        spr.write32(0x1000A000u, 0x101u);
        assert(kickHitMmio);
        assert(kickAddr == 0x1000A000u);
        assert(spr.storeLog.size() == 1);  // MMIO write did not add a store record

        // Straddling the SPR window's end (16KB = 0x4000, base 0x70000000):
        // an 8-byte write starting 4 bytes before the end overruns by 4 bytes
        // and must be guarded off to MMIO, not silently corrupt/wrap.
        bool straddleHitMmio = false;
        spr.onMmio = [&](const ps2ee::MmioAccess&) { straddleHitMmio = true; };
        const size_t storeLogBeforeStraddle = spr.storeLog.size();
        spr.write64(0x70003FFCu, 0x1122334455667788ull);
        assert(straddleHitMmio);
        assert(spr.storeLog.size() == storeLogBeforeStraddle);
    }

    // real image (skip if absent) -- must come after all image-independent
    // coverage above so that coverage always runs, even without the image.
    EeMemory img;
    if (!img.loadImage("re/ram/clock/eeMemory.bin")) {
        std::printf("SKIPPED: re/ram/clock/eeMemory.bin not present\n");
        return 0;
    }
    // first word of sceVu0MulMatrix @0x2738a0 is an lqc2 (opcode 0x36 = LQC2,
    // top 6 bits 110110) — sp0-live-reads.md
    const uint32_t w = img.read32(0x002738A0u);
    assert((w >> 26) == 0x36u);

    std::printf("ee_memory: all assertions passed\n");
    return 0;
}
