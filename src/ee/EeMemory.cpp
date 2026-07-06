#include "ee/EeMemory.hpp"
#include <cstdio>
#include <cstring>

namespace ps2ee {

bool EeMemory::loadImage(const std::string& path) {
    m_ram.assign(kRamSize, 0);
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    const size_t n = std::fread(m_ram.data(), 1, kRamSize, f);
    std::fclose(f);
    return n == kRamSize;
}

uint32_t EeMemory::read32(uint32_t vaddr) const {
    if (auto it = m_readOverrides.find(vaddr); it != m_readOverrides.end()) return it->second;
    if (isSpr(vaddr)) {
        const uint32_t off = vaddr - kSprBase;
        if (uint64_t(off) + 4 > kSprSize) { if (onMmio) onMmio({translate(vaddr), 0, 4, false}); return 0; }
        uint32_t v; std::memcpy(&v, &m_spr[off], 4); return v;
    }
    const uint32_t p = translate(vaddr);
    if (p >= kRamSize || uint64_t(p) + 4 > kRamSize) { if (onMmio) onMmio({p, 0, 4, false}); return 0; }
    if (m_ram.empty()) return 0;
    uint32_t v; std::memcpy(&v, &m_ram[p], 4); return v;
}

uint64_t EeMemory::read64(uint32_t vaddr) const {
    if (isSpr(vaddr)) {
        const uint32_t off = vaddr - kSprBase;
        if (uint64_t(off) + 8 > kSprSize) { if (onMmio) onMmio({translate(vaddr), 0, 8, false}); return 0; }
        uint64_t v; std::memcpy(&v, &m_spr[off], 8); return v;
    }
    const uint32_t p = translate(vaddr);
    if (p >= kRamSize || uint64_t(p) + 8 > kRamSize) { if (onMmio) onMmio({p, 0, 8, false}); return 0; }
    if (m_ram.empty()) return 0;
    uint64_t v; std::memcpy(&v, &m_ram[p], 8); return v;
}

void EeMemory::read128(uint32_t vaddr, uint32_t out[4]) const {
    if (isSpr(vaddr)) {
        const uint32_t off = vaddr - kSprBase;
        if (uint64_t(off) + 16 > kSprSize) { if (onMmio) onMmio({translate(vaddr), 0, 16, false}); return; }
        std::memcpy(out, &m_spr[off], 16);
        return;
    }
    const uint32_t p = translate(vaddr);
    if (p >= kRamSize || uint64_t(p) + 16 > kRamSize) { if (onMmio) onMmio({p, 0, 16, false}); return; }
    if (m_ram.empty()) { std::memset(out, 0, 16); return; }
    std::memcpy(out, &m_ram[p], 16);
}

void EeMemory::write8(uint32_t vaddr, uint8_t v) {
    if (isSpr(vaddr)) {
        const uint32_t off = vaddr - kSprBase;
        if (uint64_t(off) + 1 > kSprSize) { if (onMmio) onMmio({translate(vaddr), v, 1, true}); return; }
        std::memcpy(&m_spr[off], &v, 1);
        if (storeLogEnabled) storeLog.push_back({vaddr, 1});
        return;
    }
    const uint32_t p = translate(vaddr);
    if (p >= kRamSize || uint64_t(p) + 1 > kRamSize) { if (onMmio) onMmio({p, v, 1, true}); return; }
    if (m_ram.empty()) m_ram.assign(kRamSize, 0);
    std::memcpy(&m_ram[p], &v, 1);
    if (storeLogEnabled) storeLog.push_back({p, 1});
}

void EeMemory::write16(uint32_t vaddr, uint16_t v) {
    if (isSpr(vaddr)) {
        const uint32_t off = vaddr - kSprBase;
        if (uint64_t(off) + 2 > kSprSize) { if (onMmio) onMmio({translate(vaddr), v, 2, true}); return; }
        std::memcpy(&m_spr[off], &v, 2);
        if (storeLogEnabled) storeLog.push_back({vaddr, 2});
        return;
    }
    const uint32_t p = translate(vaddr);
    if (p >= kRamSize || uint64_t(p) + 2 > kRamSize) { if (onMmio) onMmio({p, v, 2, true}); return; }
    if (m_ram.empty()) m_ram.assign(kRamSize, 0);
    std::memcpy(&m_ram[p], &v, 2);
    if (storeLogEnabled) storeLog.push_back({p, 2});
}

void EeMemory::write32(uint32_t vaddr, uint32_t v) {
    if (isSpr(vaddr)) {
        const uint32_t off = vaddr - kSprBase;
        if (uint64_t(off) + 4 > kSprSize) { if (onMmio) onMmio({translate(vaddr), v, 4, true}); return; }
        std::memcpy(&m_spr[off], &v, 4);
        if (storeLogEnabled) storeLog.push_back({vaddr, 4});
        return;
    }
    const uint32_t p = translate(vaddr);
    if (p >= kRamSize || uint64_t(p) + 4 > kRamSize) { if (onMmio) onMmio({p, v, 4, true}); return; }
    if (m_ram.empty()) m_ram.assign(kRamSize, 0);
    std::memcpy(&m_ram[p], &v, 4);
    if (storeLogEnabled) storeLog.push_back({p, 4});
}

void EeMemory::write64(uint32_t vaddr, uint64_t v) {
    if (isSpr(vaddr)) {
        const uint32_t off = vaddr - kSprBase;
        if (uint64_t(off) + 8 > kSprSize) { if (onMmio) onMmio({translate(vaddr), v, 8, true}); return; }
        std::memcpy(&m_spr[off], &v, 8);
        if (storeLogEnabled) storeLog.push_back({vaddr, 8});
        return;
    }
    const uint32_t p = translate(vaddr);
    if (p >= kRamSize || uint64_t(p) + 8 > kRamSize) { if (onMmio) onMmio({p, v, 8, true}); return; }
    if (m_ram.empty()) m_ram.assign(kRamSize, 0);
    std::memcpy(&m_ram[p], &v, 8);
    if (storeLogEnabled) storeLog.push_back({p, 8});
}

void EeMemory::write128(uint32_t vaddr, const uint32_t v[4]) {
    if (isSpr(vaddr)) {
        const uint32_t off = vaddr - kSprBase;
        if (uint64_t(off) + 16 > kSprSize) { if (onMmio) onMmio({translate(vaddr), 0, 16, true}); return; }
        std::memcpy(&m_spr[off], v, 16);
        if (storeLogEnabled) storeLog.push_back({vaddr, 16});
        return;
    }
    const uint32_t p = translate(vaddr);
    if (p >= kRamSize || uint64_t(p) + 16 > kRamSize) { if (onMmio) onMmio({p, 0, 16, true}); return; }
    if (m_ram.empty()) m_ram.assign(kRamSize, 0);
    std::memcpy(&m_ram[p], v, 16);
    if (storeLogEnabled) storeLog.push_back({p, 16});
}

}  // namespace ps2ee
