#pragma once

#include <cstdint>
#include <vector>
#include <span>

// 4MB virtual VRAM buffer emulating the PS2 Graphics Synthesizer's local memory.
// The GS has 4MB of unified VRAM used for framebuffers, textures, and Z-buffers.
// Addresses are in 32-bit words (matching GS page/block layout).
class VramBuffer {
public:
    static constexpr uint32_t VRAM_SIZE = 4 * 1024 * 1024; // 4MB

    VramBuffer();

    // Single-word access
    void write32(uint32_t address, uint32_t value);
    uint32_t read32(uint32_t address) const;

    // Block access
    void writeBlock(uint32_t base, std::span<const uint8_t> data);
    std::span<const uint8_t> readRegion(uint32_t base, size_t size) const;

    // Raw access for bulk operations
    uint8_t* data() { return m_vram.data(); }
    const uint8_t* data() const { return m_vram.data(); }
    size_t size() const { return m_vram.size(); }

    // Clear to zero
    void clear();

private:
    std::vector<uint8_t> m_vram;
};
