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
        throw EeError{atPc, word, "unimplemented COP2 macro op"};
    }
    default:
        throw EeError{atPc, word, "unknown opcode"};
    }
}

}  // namespace ps2ee
