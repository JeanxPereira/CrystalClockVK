#include "ee/EeInterpreter.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace ps2ee {

namespace {

int64_t s64(uint64_t v) { return int64_t(v); }
uint64_t sx32(uint32_t v) { return uint64_t(int64_t(int32_t(v))); }

constexpr uint32_t kFcr31CondBit = 1u << 23;

}  // namespace

EeInterpreter::EeInterpreter(EeMemory& mem) : m_mem(mem) {
    vf[0][0] = 0.0f;
    vf[0][1] = 0.0f;
    vf[0][2] = 0.0f;
    vf[0][3] = 1.0f;
}

uint64_t EeInterpreter::call(uint32_t addr, uint64_t a0, uint64_t a1,
                              uint64_t a2, uint64_t a3) {
    instructionsRetired = 0;  // maxInstructions is a per-call runaway guard
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
    } else if (op == 6 || op == 7 || op == 0x16 || op == 0x17) {  // blez/bgtz/blezl/bgtzl
        const int64_t v = s64(gpr[rs].lo);
        const bool res = (op == 6 || op == 0x16) ? (v <= 0) : (v > 0);
        isBranch = true;
        likely = (op == 0x16 || op == 0x17);
        taken = res;
        target = curPc + 4 + brOffset;
    } else if (op == 1 && rt <= 3) {  // bltz/bgez/bltzl/bgezl
        const int64_t v = s64(gpr[rs].lo);
        const bool res = (rt == 0 || rt == 2) ? (v < 0) : (v >= 0);
        isBranch = true;
        likely = (rt == 2 || rt == 3);
        taken = res;
        target = curPc + 4 + brOffset;
    } else if (op == 0x11 && rs == 8 && (rt == 0 || rt == 1)) {  // bc1f/bc1t
        const bool cond = (fcr31 & kFcr31CondBit) != 0;
        isBranch = true;
        taken = (rt == 1) ? cond : !cond;
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
        case 0x0F:  // sync (memory barrier; no-op in this single-threaded interpreter)
            return;
        case 0x0C: {  // syscall: BIOS call number in $v1 (positive index, or
            // negative -N per the EE convention -- both resolve to the same
            // table slot). No kernel/thread/DMA/GS state is modeled here, so
            // this is a blanket no-op: the number is only recorded (not
            // acted on) so callers of the interpreter can report which BIOS
            // calls were actually hit, per the "read from evidence, don't
            // invent" rule -- if a syscall turns out to need real semantics
            // (e.g. a semaphore/thread wait), that will show up as a hang or
            // wrong-result finding downstream, not be silently masked here.
            const int32_t code = int32_t(uint32_t(gpr[3].lo));
            syscalls.push_back(code);
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
        case 0x18: {  // mult (R5900 3-operand form: rd, when nonzero, also
            // receives LO -- plain MIPS encodes rd=0 here. The Visor position
            // writer 0x2335E8 derives every rod pointer from the rd result.)
            const int64_t a = int32_t(uint32_t(gpr[rs].lo));
            const int64_t b = int32_t(uint32_t(gpr[rt].lo));
            const int64_t result = a * b;
            lo = sx32(uint32_t(uint64_t(result) & 0xFFFFFFFFu));
            hi = sx32(uint32_t((uint64_t(result) >> 32) & 0xFFFFFFFFu));
            if (rd != 0) { gpr[rd].lo = lo; gpr[rd].hi = 0; }
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
        case 0x1B: {  // divu
            const uint32_t a = uint32_t(gpr[rs].lo);
            const uint32_t b = uint32_t(gpr[rt].lo);
            if (b == 0) {
                lo = 0;
                hi = 0;
            } else {
                lo = sx32(a / b);
                hi = sx32(a % b);
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
    case 0x08:  // addi (overflow trap not modeled; same as addiu here)
        gpr[rt].lo = sx32(uint32_t(gpr[rs].lo) + uint32_t(simm));
        gpr[rt].hi = 0;
        return;
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
    case 0x1C: {  // MMI (EE multimedia extension): 128-bit rd = f(rs, rt).
        // sub-group selected by fn (word&0x3F), specific op within the
        // group selected by sa (word>>6 & 0x1F) -- table layout per
        // pcsx2/R5900OpcodeTables.cpp's tbl_MMI0..3 (reference for the
        // opcode encoding only, semantics cross-checked against
        // pcsx2/MMI.cpp's interpreter reference implementation). Only the
        // specific ops this project's clock code has actually hit are
        // implemented; anything else throws rather than guessing.
        if (fn == 0x08 && sa == 0x09) {  // MMI0 idx9: PSUBB (16 signed byte lanes)
            if (rd == 0) return;
            for (int i = 0; i < 8; i++) {
                const uint8_t a = uint8_t(gpr[rs].lo >> (i * 8));
                const uint8_t b = uint8_t(gpr[rt].lo >> (i * 8));
                gpr[rd].lo &= ~(uint64_t(0xFFu) << (i * 8));
                gpr[rd].lo |= uint64_t(uint8_t(a - b)) << (i * 8);
            }
            for (int i = 0; i < 8; i++) {
                const uint8_t a = uint8_t(gpr[rs].hi >> (i * 8));
                const uint8_t b = uint8_t(gpr[rt].hi >> (i * 8));
                gpr[rd].hi &= ~(uint64_t(0xFFu) << (i * 8));
                gpr[rd].hi |= uint64_t(uint8_t(a - b)) << (i * 8);
            }
            return;
        }
        if (fn == 0x09 && sa == 0x0E) {  // MMI2 idx14: PCPYLD (rd.hi=rs.lo, rd.lo=rt.lo)
            if (rd == 0) return;
            gpr[rd].hi = gpr[rs].lo;
            gpr[rd].lo = gpr[rt].lo;
            return;
        }
        if (fn == 0x09 && sa == 0x12) {  // MMI2 idx18: PAND (128-bit and)
            if (rd == 0) return;
            gpr[rd].lo = gpr[rs].lo & gpr[rt].lo;
            gpr[rd].hi = gpr[rs].hi & gpr[rt].hi;
            return;
        }
        if (fn == 0x09 && sa == 0x13) {  // MMI2 idx19: PXOR (128-bit xor)
            if (rd == 0) return;
            gpr[rd].lo = gpr[rs].lo ^ gpr[rt].lo;
            gpr[rd].hi = gpr[rs].hi ^ gpr[rt].hi;
            return;
        }
        if (fn == 0x29 && sa == 0x13) {  // MMI3 idx19: PNOR (128-bit nor)
            if (rd == 0) return;
            gpr[rd].lo = ~(gpr[rs].lo | gpr[rt].lo);
            gpr[rd].hi = ~(gpr[rs].hi | gpr[rt].hi);
            return;
        }
        if (fn == 0x29 && sa == 0x0E) {  // MMI3 idx14: PCPYUD (rd.lo=rs.hi, rd.hi=rt.hi)
            if (rd == 0) return;
            gpr[rd].lo = gpr[rs].hi;
            gpr[rd].hi = gpr[rt].hi;
            return;
        }
        if (fn == 0x29 && sa == 0x1B) {  // MMI3 idx27: PCPYH (replicate rt's low
            // halfword of each 64-bit half across all 4 halfword lanes of that half)
            if (rd == 0) return;
            const uint16_t loH = uint16_t(gpr[rt].lo);
            const uint16_t hiH = uint16_t(gpr[rt].hi);
            uint64_t lo = 0, hiv = 0;
            for (int i = 0; i < 4; i++) {
                lo |= uint64_t(loH) << (i * 16);
                hiv |= uint64_t(hiH) << (i * 16);
            }
            gpr[rd].lo = lo;
            gpr[rd].hi = hiv;
            return;
        }
        throw EeError{atPc, word, "unimplemented MMI op"};
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
    case 0x31:  // lwc1 ft, off(base): fpr[ft] = *(float*)(base+off)
        fpr[rt] = std::bit_cast<float>(m_mem.read32(addr));
        return;
    case 0x39:  // swc1 ft, off(base): *(float*)(base+off) = fpr[ft]
        m_mem.write32(addr, std::bit_cast<uint32_t>(fpr[rt]));
        return;
    case 0x3F:  // sd
        m_mem.write64(addr, gpr[rt].lo);
        return;
    case 0x11: {  // COP1 (single-precision FPU)
        // COP1 register format: rs=fmt/sub, rt=GPR, rd(here)=fs, sa(here)=fd.
        const uint32_t fs = rd;
        const uint32_t ft = rt;
        const uint32_t fd = sa;
        switch (rs) {
        case 0x00:  // mfc1
            gpr[rt].lo = sx32(std::bit_cast<uint32_t>(fpr[fs]));
            gpr[rt].hi = 0;
            return;
        case 0x04:  // mtc1
            fpr[fs] = std::bit_cast<float>(uint32_t(gpr[rt].lo));
            return;
        case 0x10:  // S fmt
            switch (fn) {
            case 0x00: fpr[fd] = fpr[fs] + fpr[ft]; return;         // add.s
            case 0x01: fpr[fd] = fpr[fs] - fpr[ft]; return;         // sub.s
            case 0x02: fpr[fd] = fpr[fs] * fpr[ft]; return;         // mul.s
            case 0x03: fpr[fd] = fpr[fs] / fpr[ft]; return;         // div.s
            case 0x04: fpr[fd] = std::sqrt(fpr[fs]); return;        // sqrt.s
            case 0x05: fpr[fd] = std::fabs(fpr[fs]); return;        // abs.s
            case 0x06: fpr[fd] = fpr[fs]; return;                   // mov.s
            case 0x07: fpr[fd] = -fpr[fs]; return;                  // neg.s
            case 0x24:  // cvt.w.s
                fpr[fd] = std::bit_cast<float>(uint32_t(int32_t(fpr[fs])));
                return;
            case 0x32:  // c.eq.s
                if (fpr[fs] == fpr[ft]) fcr31 |= kFcr31CondBit; else fcr31 &= ~kFcr31CondBit;
                return;
            case 0x34:  // c.olt.s (ordered less-than; NaN handling not modeled, same as c.lt.s)
                if (fpr[fs] < fpr[ft]) fcr31 |= kFcr31CondBit; else fcr31 &= ~kFcr31CondBit;
                return;
            case 0x3C:  // c.lt.s
                if (fpr[fs] < fpr[ft]) fcr31 |= kFcr31CondBit; else fcr31 &= ~kFcr31CondBit;
                return;
            case 0x3E:  // c.le.s
                if (fpr[fs] <= fpr[ft]) fcr31 |= kFcr31CondBit; else fcr31 &= ~kFcr31CondBit;
                return;
            default:
                throw EeError{atPc, word, "unknown COP1.S function"};
            }
        case 0x14:  // W fmt
            if (fn == 0x20) {  // cvt.s.w
                fpr[fd] = float(std::bit_cast<int32_t>(fpr[fs]));
                return;
            }
            throw EeError{atPc, word, "unknown COP1.W function"};
        default:
            throw EeError{atPc, word, "unknown COP1 rs"};
        }
    }
    case 0x10: {  // COP0 (system control): plain register storage, no
        // MMU/exception/interrupt semantics -- mfc0/mtc0 just move values,
        // matching the project rule of not inventing hardware behavior that
        // isn't evidenced. rd here is the COP0 register number.
        if (rs == 0x00) {  // mfc0 rt, cop0[rd]
            gpr[rt].lo = sx32(cop0[rd]);
            gpr[rt].hi = 0;
            return;
        }
        if (rs == 0x04) {  // mtc0 rt, cop0[rd]
            cop0[rd] = uint32_t(gpr[rt].lo);
            return;
        }
        if (rs == 0x10) {  // CO=1: privileged EE-specific ops, selected by fn
            switch (fn) {
            case 0x38:  // ei (enable interrupts) -- no interrupt model here, no-op
            case 0x39:  // di (disable interrupts) -- same
                return;
            default:
                throw EeError{atPc, word, "unimplemented COP0 CO function"};
            }
        }
        throw EeError{atPc, word, "unimplemented COP0 rs"};
    }
    case 0x36: {  // lqc2
        const uint32_t a = addr & ~0xFu;
        uint32_t words[4];
        m_mem.read128(a, words);
        for (int i = 0; i < 4; i++) vf[rt][i] = std::bit_cast<float>(words[i]);
        vf[0][0] = 0.0f; vf[0][1] = 0.0f; vf[0][2] = 0.0f; vf[0][3] = 1.0f;
        return;
    }
    case 0x3E: {  // sqc2
        uint32_t words[4];
        for (int i = 0; i < 4; i++) words[i] = std::bit_cast<uint32_t>(vf[rt][i]);
        m_mem.write128(addr & ~0xFu, words);
        return;
    }
    case 0x12: {  // COP2 macro (VU0 upper FMAC ops)
        // Scalar transfer form (cfc2/mfc2/mtc2/ctc2/bc2, rs in {0,1,2,4,5,6,8})
        // shares its raw rs/rd bit positions with the macro-arithmetic dest
        // mask + vfs fields below (both are 0-15, no reserved high bit
        // distinguishes them in this game's actual instruction stream --
        // confirmed empirically: real VADDx/VSUBx/etc words here always have
        // rs in 0-15 too, same range as a transfer op's rs). The reliable
        // tell is sa (would-be vfd): a genuine macro op targeting vfd=0
        // writes to VF0, which is hardwired to (0,0,0,1) and read-only on
        // real hardware, so compiled code never emits fn=0/sa=0 as a
        // meaningful macro instruction. Phase 2 task 2: this exact word
        // (pc=0026E7E4, `cfc2 $a2, $29`, rs=2/rt=6/rd=29/sa=0/fn=0) was
        // previously misrouted into the macro path (see the now-stale
        // sp1-interpreter-runs.md item 2 comment), silently no-op'ing on
        // gpr[rt] instead of returning VPU-STAT -- which kept a VU1-busy
        // poll loop spinning on garbage. cfc2 is the only transfer form
        // observed so far; only it is special-cased here.
        if (sa == 0 && fn == 0 && rs == 0x02) {  // cfc2 rt, id (id in rd)
            // No async VU0/VU1 microcode ever runs in this codebase (clock/
            // opening use zero VU1 microcode; VU0 macro-mode arithmetic
            // executes synchronously inline with the EE stream) -- every
            // control register therefore reads as idle/zero.
            gpr[rt].lo = 0; gpr[rt].hi = 0;
            return;
        }
        const uint32_t vft = rt;
        const uint32_t vfs = rd;
        const uint32_t vfd = sa;
        const uint32_t dest = rs & 0xFu;   // x8 y4 z2 w1
        const uint32_t bc = word & 3u;

        auto applyDest = [&](float (&target)[4], const float value[4]) {
            if (dest & 0x8) target[0] = value[0];
            if (dest & 0x4) target[1] = value[1];
            if (dest & 0x2) target[2] = value[2];
            if (dest & 0x1) target[3] = value[3];
        };

        if (fn >= 0x3C && fn <= 0x3F) {
            // Special2 sub-table: index = (word & 3) | ((word >> 4) & 0x7C).
            const uint32_t idx = (word & 3u) | ((word >> 4) & 0x7Cu);
            if (idx >= 24 && idx <= 27) {  // vmulax/y/z/w: ACC = vfs * vft.bc
                float result[4];
                for (int i = 0; i < 4; i++) result[i] = vf[vfs][i] * vf[vft][bc];
                applyDest(vacc, result);
                vf[0][0] = 0.0f; vf[0][1] = 0.0f; vf[0][2] = 0.0f; vf[0][3] = 1.0f;
                return;
            }
            if (idx >= 8 && idx <= 11) {  // vmaddax/y/z/w: ACC += vfs * vft.bc
                float result[4];
                for (int i = 0; i < 4; i++) result[i] = vacc[i] + vf[vfs][i] * vf[vft][bc];
                applyDest(vacc, result);
                vf[0][0] = 0.0f; vf[0][1] = 0.0f; vf[0][2] = 0.0f; vf[0][3] = 1.0f;
                return;
            }
            if (idx >= 16 && idx <= 23) {
                // Fixed-point conversion group (PCSX2 tbl_COP2_SPECIAL2
                // [16..23] = VITOF0/4/12/15 + VFTOI0/4/12/15, VUops.cpp
                // intToFloat<n>/floatToInt<n>). dest = vft, src = vfs (the
                // reverse of the arithmetic ops' vfd). FTOI results are raw
                // int32 BIT PATTERNS stored in the vf lanes (the GS XYZ path
                // reads them as 12.4 integers); truncation is toward zero and
                // the scaled magnitude saturates at 2^31. Hardware treats
                // vft=0 as a no-op sink (VF0 is read-only).
                static constexpr float kScale[4] = {1.0f, 16.0f, 4096.0f, 32768.0f};
                const float s = kScale[idx & 3u];
                if (vft != 0) {
                    float result[4];
                    if (idx >= 20) {  // VFTOIn: float -> scaled int bits
                        for (int i = 0; i < 4; i++) {
                            const float scaled = vf[vfs][i] * s;
                            const uint32_t bits = std::bit_cast<uint32_t>(scaled);
                            uint32_t out;
                            if ((bits & 0x7F800000u) >= 0x4F000000u)
                                out = (bits & 0x80000000u) ? 0x80000000u : 0x7FFFFFFFu;
                            else
                                out = uint32_t(int32_t(scaled));
                            result[i] = std::bit_cast<float>(out);
                        }
                    } else {  // VITOFn: int bits -> float / scale
                        for (int i = 0; i < 4; i++) {
                            const int32_t iv = std::bit_cast<int32_t>(vf[vfs][i]);
                            result[i] = float(iv) / s;
                        }
                    }
                    applyDest(vf[vft], result);
                }
                vf[0][0] = 0.0f; vf[0][1] = 0.0f; vf[0][2] = 0.0f; vf[0][3] = 1.0f;
                return;
            }
            if (idx >= 56 && idx <= 59) {
                // Q-pipeline group (PCSX2 R5900OpcodeTables.cpp
                // tbl_COP2_SPECIAL2[56..59] = VDIV/VSQRT/VRSQRT/VWAITQ,
                // semantics from VUops.cpp _vuDIV/_vuSQRT/_vuRSQRT). These
                // read single components selected by fsf/ftf, which occupy
                // the dest-field bits: fsf = (word>>21)&3, ftf = (word>>23)&3.
                // Macro-mode Q resolves synchronously here, so VWAITQ (59)
                // is a pure no-op and there is no in-flight Q latency model.
                const uint32_t fsf = (word >> 21) & 3u;
                const uint32_t ftf = (word >> 23) & 3u;
                const float fsv = vf[vfs][fsf];
                const float ftv = vf[vft][ftf];
                switch (idx) {
                case 56:  // VDIV: Q = vfs.fsf / vft.ftf
                    if (ftv == 0.0f) {
                        // Hardware saturates to +/-MAX float by the operands'
                        // sign xor (VUops.cpp _vuDIV ft==0 branch).
                        const bool neg = ((std::bit_cast<uint32_t>(fsv) ^
                                           std::bit_cast<uint32_t>(ftv)) >> 31) != 0;
                        vq = std::bit_cast<float>(neg ? 0xFF7FFFFFu : 0x7F7FFFFFu);
                    } else {
                        vq = fsv / ftv;
                    }
                    return;
                case 57:  // VSQRT: Q = sqrt(|vft.ftf|) (magnitude, per _vuSQRT)
                    vq = std::sqrt(std::fabs(ftv));
                    return;
                case 58:  // VRSQRT: Q = vfs.fsf / sqrt(|vft.ftf|)
                    if (ftv == 0.0f) {
                        if (fsv != 0.0f) {
                            const bool neg = ((std::bit_cast<uint32_t>(fsv) ^
                                               std::bit_cast<uint32_t>(ftv)) >> 31) != 0;
                            vq = std::bit_cast<float>(neg ? 0xFF7FFFFFu : 0x7F7FFFFFu);
                        } else {
                            const bool neg = ((std::bit_cast<uint32_t>(fsv) ^
                                               std::bit_cast<uint32_t>(ftv)) >> 31) != 0;
                            vq = std::bit_cast<float>(neg ? 0x80000000u : 0u);
                        }
                    } else {
                        vq = fsv / std::sqrt(std::fabs(ftv));
                    }
                    return;
                case 59:  // VWAITQ
                    return;
                }
            }
            throw EeError{atPc, word, "unimplemented COP2 SPECIAL2 op"};
        }
        if (fn <= 0x1B) {
            // Broadcast group: fn = (subop << 2) | bc, bc selects vft's component.
            // subop 0..6: VADDbc VSUBbc VMADDbc VMSUBbc VMAXbc VMINIbc VMULbc.
            const uint32_t subop = fn >> 2;
            float result[4];
            switch (subop) {
            case 0:  // vaddx/y/z/w: fd = vfs + vft.bc
                for (int i = 0; i < 4; i++) result[i] = vf[vfs][i] + vf[vft][bc];
                break;
            case 1:  // vsubx/y/z/w: fd = vfs - vft.bc
                for (int i = 0; i < 4; i++) result[i] = vf[vfs][i] - vf[vft][bc];
                break;
            case 2:  // vmaddx/y/z/w: fd = ACC + vfs * vft.bc
                for (int i = 0; i < 4; i++) result[i] = vacc[i] + vf[vfs][i] * vf[vft][bc];
                break;
            case 3:  // vmsubx/y/z/w: fd = ACC - vfs * vft.bc
                for (int i = 0; i < 4; i++) result[i] = vacc[i] - vf[vfs][i] * vf[vft][bc];
                break;
            case 4:  // vmaxx/y/z/w: fd = max(vfs, vft.bc)
                for (int i = 0; i < 4; i++) result[i] = std::max(vf[vfs][i], vf[vft][bc]);
                break;
            case 5:  // vminix/y/z/w: fd = min(vfs, vft.bc)
                for (int i = 0; i < 4; i++) result[i] = std::min(vf[vfs][i], vf[vft][bc]);
                break;
            case 6:  // vmulx/y/z/w: fd = vfs * vft.bc
                for (int i = 0; i < 4; i++) result[i] = vf[vfs][i] * vf[vft][bc];
                break;
            default:
                throw EeError{atPc, word, "unimplemented COP2 broadcast subop"};
            }
            applyDest(vf[vfd], result);
            vf[0][0] = 0.0f; vf[0][1] = 0.0f; vf[0][2] = 0.0f; vf[0][3] = 1.0f;
            return;
        }
        if (fn == 0x1C || fn == 0x20 || fn == 0x21 || fn == 0x24 || fn == 0x25) {
            // Q-broadcast group (PCSX2 tbl_COP2_SPECIAL1[28/32/33/36/37] =
            // VMULq/VADDq/VMADDq/VSUBq/VMSUBq, VUops.cpp _vuMULq etc.):
            // same field layout as the bc-broadcast group but the broadcast
            // operand is the Q register (set by VDIV/VSQRT/VRSQRT above),
            // not a vft component. Phase 2 Visor dial render: the transform
            // 0x00232DA0's VU0 callee moves VSQRT's result out via
            // VADDq.x vf5, vf0, Q (word 0x4B000160 at pc=00273838).
            // The I-register siblings (VMULi/VADDi/..., fn 0x1D/0x1E/0x22/
            // 0x23/0x26/0x27) are deliberately NOT included -- no I register
            // is modeled and none has been observed in this codebase yet.
            float result[4];
            switch (fn) {
            case 0x1C:  // VMULq: fd = vfs * Q
                for (int i = 0; i < 4; i++) result[i] = vf[vfs][i] * vq;
                break;
            case 0x20:  // VADDq: fd = vfs + Q
                for (int i = 0; i < 4; i++) result[i] = vf[vfs][i] + vq;
                break;
            case 0x21:  // VMADDq: fd = ACC + vfs * Q
                for (int i = 0; i < 4; i++) result[i] = vacc[i] + vf[vfs][i] * vq;
                break;
            case 0x24:  // VSUBq: fd = vfs - Q
                for (int i = 0; i < 4; i++) result[i] = vf[vfs][i] - vq;
                break;
            case 0x25:  // VMSUBq: fd = ACC - vfs * Q
                for (int i = 0; i < 4; i++) result[i] = vacc[i] - vf[vfs][i] * vq;
                break;
            }
            applyDest(vf[vfd], result);
            vf[0][0] = 0.0f; vf[0][1] = 0.0f; vf[0][2] = 0.0f; vf[0][3] = 1.0f;
            return;
        }
        if (fn >= 0x28 && fn <= 0x2F && fn != 0x2E) {
            // Phase 2 -- Visor dial render: the transform-prep helper
            // 0x00232DA0 (called per-rod before draw_crystal_rod on the
            // Visor's own driver 0x00233F60, unlike the menu's pre-baked
            // preview) hits VMUL (fn=0x2A) inside its VU0-macro callee
            // 0x00273820. This is the FULL-VECTOR sibling of the broadcast
            // group above -- same field layout (vft=rt, vfs=rd, vfd=sa,
            // dest=rs&0xF) but operates elementwise on vft directly (no
            // ".bc" component broadcast; the low 2 bits of the word are part
            // of the fixed funct encoding here, not a broadcast selector).
            // VOPMSUB (fn=0x2E, an outer-product op) is deliberately NOT
            // included -- not observed yet, meaningfully different shape.
            float result[4];
            switch (fn) {
            case 0x28:  // VADD: fd = vfs + vft
                for (int i = 0; i < 4; i++) result[i] = vf[vfs][i] + vf[vft][i];
                break;
            case 0x29:  // VMADD: fd = ACC + vfs * vft
                for (int i = 0; i < 4; i++) result[i] = vacc[i] + vf[vfs][i] * vf[vft][i];
                break;
            case 0x2A:  // VMUL: fd = vfs * vft
                for (int i = 0; i < 4; i++) result[i] = vf[vfs][i] * vf[vft][i];
                break;
            case 0x2B:  // VMAX: fd = max(vfs, vft)
                for (int i = 0; i < 4; i++) result[i] = std::max(vf[vfs][i], vf[vft][i]);
                break;
            case 0x2C:  // VSUB: fd = vfs - vft
                for (int i = 0; i < 4; i++) result[i] = vf[vfs][i] - vf[vft][i];
                break;
            case 0x2D:  // VMSUB: fd = ACC - vfs * vft
                for (int i = 0; i < 4; i++) result[i] = vacc[i] - vf[vfs][i] * vf[vft][i];
                break;
            case 0x2F:  // VMINI: fd = min(vfs, vft)
                for (int i = 0; i < 4; i++) result[i] = std::min(vf[vfs][i], vf[vft][i]);
                break;
            default:
                throw EeError{atPc, word, "unimplemented COP2 full-vector op"};
            }
            applyDest(vf[vfd], result);
            vf[0][0] = 0.0f; vf[0][1] = 0.0f; vf[0][2] = 0.0f; vf[0][3] = 1.0f;
            return;
        }
        throw EeError{atPc, word, "unimplemented COP2 macro op"};
    }
    default:
        throw EeError{atPc, word, "unknown opcode"};
    }
}

}  // namespace ps2ee
