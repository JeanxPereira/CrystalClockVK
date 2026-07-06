#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ps2ee {

struct MmioAccess {
    uint32_t addr;
    uint64_t value;
    int size;
    bool isWrite;
};

struct StoreRecord {
    uint32_t physAddr;
    uint32_t size;
};

class EeMemory {
public:
    static constexpr uint32_t kRamSize = 32 * 1024 * 1024;

    bool loadImage(const std::string& path);

    // EE virtual -> physical: mask 0x1FFFFFFF covers useg low, 0x2/0x3
    // uncached windows, kseg0 0x8..., kseg1 0xA... . phys >= kRamSize = MMIO.
    static uint32_t translate(uint32_t vaddr) { return vaddr & 0x1FFFFFFF; }

    bool isRam(uint32_t vaddr) const { return translate(vaddr) < kRamSize; }

    uint8_t*       ram()       { return m_ram.data(); }
    const uint8_t* ram() const { return m_ram.data(); }

    uint32_t read32(uint32_t vaddr) const;   // RAM only; MMIO read -> onMmio + 0
    uint64_t read64(uint32_t vaddr) const;
    void     read128(uint32_t vaddr, uint32_t out[4]) const;

    void write8 (uint32_t vaddr, uint8_t  v);   // logs to storeLog; MMIO -> onMmio
    void write16(uint32_t vaddr, uint16_t v);
    void write32(uint32_t vaddr, uint32_t v);
    void write64(uint32_t vaddr, uint64_t v);
    void write128(uint32_t vaddr, const uint32_t v[4]);

    std::function<void(const MmioAccess&)> onMmio;   // null = ignore reads, log-free
    std::vector<StoreRecord> storeLog;               // every RAM store, in order
    bool storeLogEnabled = false;

private:
    std::vector<uint8_t> m_ram;
};

}  // namespace ps2ee
