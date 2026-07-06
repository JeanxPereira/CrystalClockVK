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

    // real image (skip if absent)
    EeMemory img;
    if (!img.loadImage("re/ram/clock/eeMemory.bin")) {
        std::printf("SKIPPED: re/ram/clock/eeMemory.bin not present\n");
        return 0;
    }
    // first word of sceVu0MulMatrix @0x2738a0 is an lqc2 (opcode 0x36 = LQC2,
    // top 6 bits 110110) — sp0-live-reads.md
    const uint32_t w = img.read32(0x002738A0u);
    assert((w >> 26) == 0x36u);

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

    std::printf("ee_memory: all assertions passed\n");
    return 0;
}
