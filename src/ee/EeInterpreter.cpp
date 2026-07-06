#include "ee/EeInterpreter.hpp"

namespace ps2ee {

namespace {

int64_t s64(uint64_t v) { return int64_t(v); }
uint64_t sx32(uint32_t v) { return uint64_t(int64_t(int32_t(v))); }

}  // namespace

EeInterpreter::EeInterpreter(EeMemory& mem) : m_mem(mem) {
    vf[0][0] = 0.0f;
    vf[0][1] = 0.0f;
    vf[0][2] = 0.0f;
    vf[0][3] = 1.0f;
}

uint64_t EeInterpreter::call(uint32_t addr, uint64_t a0, uint64_t a1,
                              uint64_t a2, uint64_t a3) {
    gpr[4].lo = a0; gpr[4].hi = 0;
    gpr[5].lo = a1; gpr[5].hi = 0;
    gpr[6].lo = a2; gpr[6].hi = 0;
    gpr[7].lo = a3; gpr[7].hi = 0;
    gpr[29].lo = kDefaultStack; gpr[29].hi = 0;
    gpr[31].lo = kReturnSentinel; gpr[31].hi = 0;
    pc = addr;
    while (pc != kReturnSentinel) {
        if (++instructionsRetired > maxInstructions)
            throw EeError{pc, 0, "instruction budget exceeded"};
        step();
    }
    return gpr[2].lo;
}

void EeInterpreter::step() {
    const uint32_t curPc = pc;
    const uint32_t word = m_mem.read32(curPc);
    const uint32_t op = word >> 26;
    const uint32_t rs = (word >> 21) & 31u;
    const uint32_t rt = (word >> 16) & 31u;

    bool isBranch = false;
    bool likely = false;
    bool taken = false;
    uint32_t target = curPc + 4;

    const int32_t brOffset = int32_t(int16_t(word & 0xFFFFu)) * 4;

    if (op == 0 && (word & 63u) == 0x08) {  // jr
        isBranch = true;
        taken = true;
        target = uint32_t(gpr[rs].lo);
    } else if (op == 0 && (word & 63u) == 0x09) {  // jalr
        isBranch = true;
        taken = true;
        target = uint32_t(gpr[rs].lo);
        const uint32_t rd = (word >> 11) & 31u;
        gpr[rd].lo = curPc + 8;
        gpr[rd].hi = 0;
        if (traceCalls) trace.push_back(target);
    } else if (op == 2 || op == 3) {  // j / jal
        isBranch = true;
        taken = true;
        const uint32_t idx = word & 0x03FFFFFFu;
        target = (curPc & 0xF0000000u) | (idx << 2);
        if (op == 3) {
            gpr[31].lo = curPc + 8;
            gpr[31].hi = 0;
            if (traceCalls) trace.push_back(target);
        }
    } else if (op == 4 || op == 5 || op == 0x14 || op == 0x15) {  // beq/bne/beql/bnel
        const bool eq = (gpr[rs].lo == gpr[rt].lo) && (gpr[rs].hi == gpr[rt].hi);
        const bool res = (op == 4 || op == 0x14) ? eq : !eq;
        isBranch = true;
        likely = (op == 0x14 || op == 0x15);
        taken = res;
        target = curPc + 4 + brOffset;
    } else if (op == 6 || op == 7) {  // blez/bgtz
        const int64_t v = s64(gpr[rs].lo);
        const bool res = (op == 6) ? (v <= 0) : (v > 0);
        isBranch = true;
        taken = res;
        target = curPc + 4 + brOffset;
    } else if (op == 1 && (rt == 0 || rt == 1)) {  // bltz/bgez
        const int64_t v = s64(gpr[rs].lo);
        const bool res = (rt == 0) ? (v < 0) : (v >= 0);
        isBranch = true;
        taken = res;
        target = curPc + 4 + brOffset;
    }

    if (isBranch) {
        if (likely && !taken) {
            pc = curPc + 8;
        } else {
            const uint32_t slotPc = curPc + 4;
            const uint32_t slotWord = m_mem.read32(slotPc);
            executeOne(slotWord, slotPc);
            gpr[0] = U128{};
            pc = taken ? target : (curPc + 8);
        }
        return;
    }

    executeOne(word, curPc);
    gpr[0] = U128{};
    pc = curPc + 4;
}

void EeInterpreter::executeOne(uint32_t word, uint32_t atPc) {
    const uint32_t op = word >> 26;
    const uint32_t rs = (word >> 21) & 31u;
    const uint32_t rt = (word >> 16) & 31u;
    const uint32_t rd = (word >> 11) & 31u;
    const uint32_t sa = (word >> 6) & 31u;
    const uint32_t fn = word & 63u;
    const int32_t simm = int32_t(int16_t(word & 0xFFFFu));
    const uint64_t uimm = uint64_t(uint16_t(word & 0xFFFFu));
    const uint32_t addr = uint32_t(gpr[rs].lo) + uint32_t(simm);

    switch (op) {
    case 0: {  // SPECIAL
        switch (fn) {
        case 0x00:  // sll
            gpr[rd].lo = sx32(uint32_t(gpr[rt].lo) << (sa & 31u));
            gpr[rd].hi = 0;
            return;
        case 0x02:  // srl
            gpr[rd].lo = sx32(uint32_t(gpr[rt].lo) >> (sa & 31u));
            gpr[rd].hi = 0;
            return;
        case 0x03: {  // sra
            const int32_t v = int32_t(uint32_t(gpr[rt].lo));
            gpr[rd].lo = sx32(uint32_t(v >> (sa & 31u)));
            gpr[rd].hi = 0;
            return;
        }
        case 0x04:  // sllv
            gpr[rd].lo = sx32(uint32_t(gpr[rt].lo) << (uint32_t(gpr[rs].lo) & 31u));
            gpr[rd].hi = 0;
            return;
        case 0x06:  // srlv
            gpr[rd].lo = sx32(uint32_t(gpr[rt].lo) >> (uint32_t(gpr[rs].lo) & 31u));
            gpr[rd].hi = 0;
            return;
        case 0x07: {  // srav
            const int32_t v = int32_t(uint32_t(gpr[rt].lo));
            gpr[rd].lo = sx32(uint32_t(v >> (uint32_t(gpr[rs].lo) & 31u)));
            gpr[rd].hi = 0;
            return;
        }
        case 0x0A:  // movz
            if (gpr[rt].lo == 0 && gpr[rt].hi == 0) gpr[rd] = gpr[rs];
            return;
        case 0x0B:  // movn
            if (gpr[rt].lo != 0 || gpr[rt].hi != 0) gpr[rd] = gpr[rs];
            return;
        case 0x10:  // mfhi
            gpr[rd].lo = hi;
            gpr[rd].hi = 0;
            return;
        case 0x12:  // mflo
            gpr[rd].lo = lo;
            gpr[rd].hi = 0;
            return;
        case 0x18: {  // mult
            const int64_t a = int32_t(uint32_t(gpr[rs].lo));
            const int64_t b = int32_t(uint32_t(gpr[rt].lo));
            const int64_t result = a * b;
            lo = sx32(uint32_t(uint64_t(result) & 0xFFFFFFFFu));
            hi = sx32(uint32_t((uint64_t(result) >> 32) & 0xFFFFFFFFu));
            return;
        }
        case 0x1A: {  // div
            const int32_t a = int32_t(uint32_t(gpr[rs].lo));
            const int32_t b = int32_t(uint32_t(gpr[rt].lo));
            if (b == 0) {
                lo = 0;
                hi = 0;
            } else {
                lo = sx32(uint32_t(a / b));
                hi = sx32(uint32_t(a % b));
            }
            return;
        }
        case 0x21:  // addu
            gpr[rd].lo = sx32(uint32_t(gpr[rs].lo) + uint32_t(gpr[rt].lo));
            gpr[rd].hi = 0;
            return;
        case 0x23:  // subu
            gpr[rd].lo = sx32(uint32_t(gpr[rs].lo) - uint32_t(gpr[rt].lo));
            gpr[rd].hi = 0;
            return;
        case 0x24:  // and
            gpr[rd].lo = gpr[rs].lo & gpr[rt].lo;
            gpr[rd].hi = 0;
            return;
        case 0x25:  // or
            gpr[rd].lo = gpr[rs].lo | gpr[rt].lo;
            gpr[rd].hi = 0;
            return;
        case 0x26:  // xor
            gpr[rd].lo = gpr[rs].lo ^ gpr[rt].lo;
            gpr[rd].hi = 0;
            return;
        case 0x27:  // nor
            gpr[rd].lo = ~(gpr[rs].lo | gpr[rt].lo);
            gpr[rd].hi = 0;
            return;
        case 0x2A:  // slt
            gpr[rd].lo = (s64(gpr[rs].lo) < s64(gpr[rt].lo)) ? 1u : 0u;
            gpr[rd].hi = 0;
            return;
        case 0x2B:  // sltu
            gpr[rd].lo = (gpr[rs].lo < gpr[rt].lo) ? 1u : 0u;
            gpr[rd].hi = 0;
            return;
        case 0x2D:  // daddu
            gpr[rd].lo = gpr[rs].lo + gpr[rt].lo;
            gpr[rd].hi = 0;
            return;
        case 0x2F:  // dsubu
            gpr[rd].lo = gpr[rs].lo - gpr[rt].lo;
            gpr[rd].hi = 0;
            return;
        case 0x38:  // dsll
            gpr[rd].lo = gpr[rt].lo << sa;
            gpr[rd].hi = 0;
            return;
        case 0x3A:  // dsrl
            gpr[rd].lo = gpr[rt].lo >> sa;
            gpr[rd].hi = 0;
            return;
        case 0x3B:  // dsra
            gpr[rd].lo = uint64_t(s64(gpr[rt].lo) >> sa);
            gpr[rd].hi = 0;
            return;
        case 0x3C:  // dsll32
            gpr[rd].lo = gpr[rt].lo << (sa + 32);
            gpr[rd].hi = 0;
            return;
        case 0x3E:  // dsrl32
            gpr[rd].lo = gpr[rt].lo >> (sa + 32);
            gpr[rd].hi = 0;
            return;
        case 0x3F:  // dsra32
            gpr[rd].lo = uint64_t(s64(gpr[rt].lo) >> (sa + 32));
            gpr[rd].hi = 0;
            return;
        default:
            throw EeError{atPc, word, "unknown SPECIAL function"};
        }
    }
    case 0x09:  // addiu
        gpr[rt].lo = sx32(uint32_t(gpr[rs].lo) + uint32_t(simm));
        gpr[rt].hi = 0;
        return;
    case 0x0A:  // slti
        gpr[rt].lo = (s64(gpr[rs].lo) < int64_t(simm)) ? 1u : 0u;
        gpr[rt].hi = 0;
        return;
    case 0x0B:  // sltiu
        gpr[rt].lo = (gpr[rs].lo < uint64_t(int64_t(simm))) ? 1u : 0u;
        gpr[rt].hi = 0;
        return;
    case 0x0C:  // andi
        gpr[rt].lo = gpr[rs].lo & uimm;
        gpr[rt].hi = 0;
        return;
    case 0x0D:  // ori
        gpr[rt].lo = gpr[rs].lo | uimm;
        gpr[rt].hi = 0;
        return;
    case 0x0E:  // xori
        gpr[rt].lo = gpr[rs].lo ^ uimm;
        gpr[rt].hi = 0;
        return;
    case 0x0F:  // lui
        gpr[rt].lo = sx32(uint32_t(word & 0xFFFFu) << 16);
        gpr[rt].hi = 0;
        return;
    case 0x19:  // daddiu
        gpr[rt].lo = gpr[rs].lo + uint64_t(int64_t(simm));
        gpr[rt].hi = 0;
        return;
    case 0x1E: {  // lq
        const uint32_t a = addr & ~0xFu;
        uint32_t words[4];
        m_mem.read128(a, words);
        gpr[rt].lo = uint64_t(words[0]) | (uint64_t(words[1]) << 32);
        gpr[rt].hi = uint64_t(words[2]) | (uint64_t(words[3]) << 32);
        return;
    }
    case 0x1F: {  // sq
        const uint32_t a = addr & ~0xFu;
        uint32_t words[4] = {
            uint32_t(gpr[rt].lo), uint32_t(gpr[rt].lo >> 32),
            uint32_t(gpr[rt].hi), uint32_t(gpr[rt].hi >> 32)};
        m_mem.write128(a, words);
        return;
    }
    case 0x20:  // lb
        gpr[rt].lo = uint64_t(int64_t(int8_t(uint8_t(m_mem.read32(addr & ~3u) >> ((addr & 3u) * 8)))));
        gpr[rt].hi = 0;
        return;
    case 0x21:  // lh
        gpr[rt].lo = uint64_t(int64_t(int16_t(uint16_t(m_mem.read32(addr & ~3u) >> ((addr & 2u) * 8)))));
        gpr[rt].hi = 0;
        return;
    case 0x22:  // lwl
        throw EeError{atPc, word, "lwl not implemented"};
    case 0x23:  // lw
        gpr[rt].lo = sx32(m_mem.read32(addr));
        gpr[rt].hi = 0;
        return;
    case 0x24:  // lbu
        gpr[rt].lo = uint64_t(uint8_t(m_mem.read32(addr & ~3u) >> ((addr & 3u) * 8)));
        gpr[rt].hi = 0;
        return;
    case 0x25:  // lhu
        gpr[rt].lo = uint64_t(uint16_t(m_mem.read32(addr & ~3u) >> ((addr & 2u) * 8)));
        gpr[rt].hi = 0;
        return;
    case 0x26:  // lwr
        throw EeError{atPc, word, "lwr not implemented"};
    case 0x27:  // lwu
        gpr[rt].lo = uint64_t(m_mem.read32(addr));
        gpr[rt].hi = 0;
        return;
    case 0x28:  // sb
        m_mem.write8(addr, uint8_t(gpr[rt].lo));
        return;
    case 0x29:  // sh
        m_mem.write16(addr, uint16_t(gpr[rt].lo));
        return;
    case 0x2A:  // swl
        throw EeError{atPc, word, "swl not implemented"};
    case 0x2B:  // sw
        m_mem.write32(addr, uint32_t(gpr[rt].lo));
        return;
    case 0x2F:  // swr
        throw EeError{atPc, word, "swr not implemented"};
    case 0x37:  // ld
        gpr[rt].lo = m_mem.read64(addr);
        gpr[rt].hi = 0;
        return;
    case 0x3F:  // sd
        m_mem.write64(addr, gpr[rt].lo);
        return;
    default:
        throw EeError{atPc, word, "unknown opcode"};
    }
}

}  // namespace ps2ee
