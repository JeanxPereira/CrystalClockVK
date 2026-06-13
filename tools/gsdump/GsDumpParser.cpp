#include "GsDumpParser.hpp"

#include <array>
#include <cstring>

namespace {

// Little-endian readers over a bounds-checked cursor.
class Cursor {
public:
    Cursor(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}

    size_t pos() const { return m_pos; }
    size_t remaining() const { return m_size - m_pos; }
    bool atEnd() const { return m_pos >= m_size; }

    void require(size_t n) const {
        if (m_pos + n > m_size)
            throw GsDumpParser::ParseError("unexpected end of dump");
    }

    uint8_t u8() {
        require(1);
        return m_data[m_pos++];
    }
    uint32_t u32() {
        require(4);
        uint32_t v;
        std::memcpy(&v, m_data + m_pos, 4);
        m_pos += 4;
        return v;
    }
    void skip(size_t n) {
        require(n);
        m_pos += n;
    }
    const uint8_t* take(size_t n) {
        require(n);
        const uint8_t* p = m_data + m_pos;
        m_pos += n;
        return p;
    }

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_pos = 0;
};

uint32_t rdU32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
uint64_t rdU64(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}

constexpr uint32_t lo32(uint64_t v) { return static_cast<uint32_t>(v); }
constexpr uint32_t hi32(uint64_t v) { return static_cast<uint32_t>(v >> 32); }

// GS register bitfield decoders (canonical layout, cross-checked vs GSRegs.h).
GsAlpha decAlpha(uint64_t v) {
    uint32_t l = lo32(v);
    return {static_cast<uint8_t>(l & 3), static_cast<uint8_t>((l >> 2) & 3),
            static_cast<uint8_t>((l >> 4) & 3), static_cast<uint8_t>((l >> 6) & 3),
            static_cast<uint8_t>(hi32(v) & 0xff)};
}
GsTest decTest(uint64_t v) {
    uint32_t l = lo32(v);
    GsTest t{};
    t.ate = l & 1;
    t.atst = (l >> 1) & 7;
    t.aref = (l >> 4) & 0xff;
    t.afail = (l >> 12) & 3;
    t.date = (l >> 14) & 1;
    t.datm = (l >> 15) & 1;
    t.zte = (l >> 16) & 1;
    t.ztst = (l >> 17) & 3;
    return t;
}
GsTex0 decTex0(uint64_t v) {
    uint32_t l = lo32(v), h = hi32(v);
    GsTex0 t{};
    t.tbp0 = l & 0x3fff;
    t.tbw = (l >> 14) & 0x3f;
    t.psm = (l >> 20) & 0x3f;
    t.tw = (l >> 26) & 0xf;
    t.th = ((l >> 30) & 3) | ((h & 3) << 2);
    t.tcc = (h >> 2) & 1;
    t.tfx = (h >> 3) & 3;
    t.cbp = (h >> 5) & 0x3fff;
    t.cpsm = (h >> 19) & 0xf;
    t.csm = (h >> 23) & 1;
    t.csa = (h >> 24) & 0x1f;
    t.cld = (h >> 29) & 7;
    return t;
}
GsTex1 decTex1(uint64_t v) {
    uint32_t l = lo32(v);
    GsTex1 t{};
    t.lcm = l & 1;
    t.mxl = (l >> 2) & 7;
    t.mmag = (l >> 5) & 1;
    t.mmin = (l >> 6) & 7;
    t.mtba = (l >> 9) & 1;
    return t;
}
GsFrame decFrame(uint64_t v) {
    uint32_t l = lo32(v);
    GsFrame f{};
    f.fbp = l & 0x1ff;
    f.fbw = (l >> 16) & 0x3f;
    f.psm = (l >> 24) & 0x3f;
    f.fbmsk = hi32(v);
    return f;
}
GsZbuf decZbuf(uint64_t v) {
    uint32_t l = lo32(v);
    GsZbuf z{};
    z.zbp = l & 0x1ff;
    z.psm = (l >> 24) & 0x3f;
    z.zmsk = hi32(v) & 1;
    return z;
}
GsScissor decScissor(uint64_t v) {
    uint32_t l = lo32(v), h = hi32(v);
    return {static_cast<uint16_t>(l & 0x7ff), static_cast<uint16_t>((l >> 16) & 0x7ff),
            static_cast<uint16_t>(h & 0x7ff), static_cast<uint16_t>((h >> 16) & 0x7ff)};
}
GsXyOffset decXyOffset(uint64_t v) { return {lo32(v), hi32(v)}; }

// GIF A+D register addresses (GSRegs.h enum GIF_A_D_REG).
enum : uint8_t {
    REG_TEX0_1 = 0x06, REG_TEX0_2 = 0x07,
    REG_TEX1_1 = 0x14, REG_TEX1_2 = 0x15,
    REG_XYOFFSET_1 = 0x18, REG_XYOFFSET_2 = 0x19,
    REG_SCISSOR_1 = 0x40, REG_SCISSOR_2 = 0x41,
    REG_ALPHA_1 = 0x42, REG_ALPHA_2 = 0x43,
    REG_DTHE = 0x45, REG_COLCLAMP = 0x46,
    REG_TEST_1 = 0x47, REG_TEST_2 = 0x48,
    REG_PABE = 0x49, REG_FBA_1 = 0x4a, REG_FBA_2 = 0x4b,
    REG_FRAME_1 = 0x4c, REG_FRAME_2 = 0x4d,
    REG_ZBUF_1 = 0x4e, REG_ZBUF_2 = 0x4f,
};

}  // namespace

GsCommandStream GsDumpParser::parse(const uint8_t* data, size_t size) {
    GsCommandStream out;
    Cursor cur(data, size);

    // --- Header (GSLzma.cpp:74) ---
    const uint32_t leadCrc = cur.u32();
    const uint32_t headerSize = cur.u32();
    const uint8_t* stateBlob = cur.take(headerSize);
    if (headerSize < 36)
        throw ParseError("header too small");

    out.header.stateVersion = rdU32(stateBlob + 0);
    out.header.stateSize = rdU32(stateBlob + 4);
    const uint32_t serialOffset = rdU32(stateBlob + 8);
    const uint32_t serialSize = rdU32(stateBlob + 12);
    out.header.crc = (leadCrc == 0xffffffffu) ? rdU32(stateBlob + 16) : leadCrc;
    out.header.screenshotWidth = rdU32(stateBlob + 20);
    out.header.screenshotHeight = rdU32(stateBlob + 24);
    if (serialSize > 0 && serialOffset + serialSize <= headerSize)
        out.header.serial.assign(reinterpret_cast<const char*>(stateBlob + serialOffset), serialSize);

    // Real freeze state, then the 8192-byte GSPrivRegSet.
    const uint8_t* freezePtr = cur.take(out.header.stateSize);
    out.freeze.assign(freezePtr, freezePtr + out.header.stateSize);
    constexpr size_t kPrivRegsBytes = 0x2000;
    const uint8_t* privPtr = cur.take(kPrivRegsBytes);
    out.privRegs.assign(privPtr, privPtr + kPrivRegsBytes);

    // --- packet stream + GIF walk ---
    std::array<uint64_t, 256> gsState{};  // addr -> latest A+D value
    auto snapshot = [&](uint32_t primField) {
        const bool ctxt2 = (primField >> 9) & 1;
        auto pick = [&](uint8_t a1, uint8_t a2) { return ctxt2 ? a2 : a1; };
        GsPrimitive pr{};
        pr.index = static_cast<uint32_t>(out.prims.size());
        pr.prim = {static_cast<uint8_t>(primField & 7), bool((primField >> 3) & 1),
                   bool((primField >> 4) & 1), bool((primField >> 5) & 1),
                   bool((primField >> 6) & 1), bool((primField >> 7) & 1),
                   bool((primField >> 8) & 1), static_cast<uint8_t>(ctxt2 ? 1 : 0),
                   bool((primField >> 10) & 1)};
        pr.alpha = decAlpha(gsState[pick(REG_ALPHA_1, REG_ALPHA_2)]);
        pr.test = decTest(gsState[pick(REG_TEST_1, REG_TEST_2)]);
        pr.tex0 = decTex0(gsState[pick(REG_TEX0_1, REG_TEX0_2)]);
        pr.tex1 = decTex1(gsState[pick(REG_TEX1_1, REG_TEX1_2)]);
        pr.frame = decFrame(gsState[pick(REG_FRAME_1, REG_FRAME_2)]);
        pr.zbuf = decZbuf(gsState[pick(REG_ZBUF_1, REG_ZBUF_2)]);
        pr.scissor = decScissor(gsState[pick(REG_SCISSOR_1, REG_SCISSOR_2)]);
        pr.xyoffset = decXyOffset(gsState[pick(REG_XYOFFSET_1, REG_XYOFFSET_2)]);
        pr.dthe = gsState[REG_DTHE] & 1;
        pr.colclamp = gsState[REG_COLCLAMP] & 1;
        pr.pabe = gsState[REG_PABE] & 1;
        pr.fba = gsState[pick(REG_FBA_1, REG_FBA_2)] & 1;
        out.prims.push_back(pr);
    };

    auto walkGif = [&](const uint8_t* base, size_t len) {
        size_t p = 0;
        while (p + 16 <= len) {
            const uint32_t a = rdU32(base + p);
            const uint32_t b = rdU32(base + p + 4);
            const uint64_t regsDesc = rdU64(base + p + 8);
            p += 16;

            const uint32_t nloop = a & 0x7fff;
            const bool pre = (b >> 14) & 1;
            const uint32_t prim = (b >> 15) & 0x7ff;
            const uint32_t flg = (b >> 26) & 3;
            uint32_t nreg = (b >> 28) & 0xf;
            if (nreg == 0) nreg = 16;

            out.counts.giftags++;
            out.counts.nloopSum += nloop;
            switch (flg) {
                case 0: out.counts.flgPacked++; break;
                case 1: out.counts.flgReglist++; break;
                case 2: out.counts.flgImage++; break;
                case 3: out.counts.flgImage2++; break;
            }
            if (pre) {
                out.counts.prims++;
                snapshot(prim);
            }
            if (nloop == 0) continue;

            if (flg == 0) {
                // PACKED: nloop*nreg units of 16 bytes; reg descriptor 0x0e == A+D
                for (uint32_t l = 0; l < nloop; l++) {
                    for (uint32_t r = 0; r < nreg; r++) {
                        const uint8_t desc = (regsDesc >> (r * 4)) & 0xf;
                        if (desc == 0x0e) {
                            const uint8_t addr = base[p + 8];  // ADDR byte of GIFPackedA_D
                            gsState[addr] = rdU64(base + p);   // value = low qword
                        }
                        p += 16;
                    }
                }
            } else if (flg == 1) {
                // REGLIST: nloop*nreg 64-bit regs, padded to qword
                const uint64_t words = static_cast<uint64_t>(nloop) * nreg;
                p += ((words + 1) >> 1) * 16;
            } else {
                // IMAGE / IMAGE2: nloop qwords
                p += static_cast<size_t>(nloop) * 16;
            }
        }
    };

    while (!cur.atEnd()) {
        const uint8_t id = cur.u8();
        switch (id) {
            case 0: {  // Transfer
                cur.u8();  // path index
                const uint32_t len = cur.u32();
                const uint8_t* payload = cur.take(len);
                out.counts.transfers++;
                walkGif(payload, len);
                break;
            }
            case 1: cur.skip(1); out.counts.vsync++; break;        // VSync field
            case 2: cur.skip(4); out.counts.readfifo++; break;     // ReadFIFO2 size
            case 3: cur.skip(0x2000); out.counts.regsPackets++; break;  // Registers
            default:
                throw ParseError("unknown packet id");
        }
    }

    return out;
}
