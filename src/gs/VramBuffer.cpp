#include "VramBuffer.hpp"
#include <cstring>
#include <stdexcept>
#include <algorithm>

VramBuffer::VramBuffer() : m_vram(VRAM_SIZE, 0) {}

void VramBuffer::write32(uint32_t address, uint32_t value) {
    if (address + 4 > VRAM_SIZE) {
        throw std::out_of_range("VRAM write32 out of bounds");
    }
    std::memcpy(&m_vram[address], &value, sizeof(uint32_t));
}

uint32_t VramBuffer::read32(uint32_t address) const {
    if (address + 4 > VRAM_SIZE) {
        throw std::out_of_range("VRAM read32 out of bounds");
    }
    uint32_t value;
    std::memcpy(&value, &m_vram[address], sizeof(uint32_t));
    return value;
}

void VramBuffer::writeBlock(uint32_t base, std::span<const uint8_t> data) {
    if (base + data.size() > VRAM_SIZE) {
        throw std::out_of_range("VRAM writeBlock out of bounds");
    }
    std::memcpy(&m_vram[base], data.data(), data.size());
}

std::span<const uint8_t> VramBuffer::readRegion(uint32_t base, size_t size) const {
    if (base + size > VRAM_SIZE) {
        throw std::out_of_range("VRAM readRegion out of bounds");
    }
    return std::span<const uint8_t>(&m_vram[base], size);
}

void VramBuffer::clear() {
    std::fill(m_vram.begin(), m_vram.end(), 0);
}
