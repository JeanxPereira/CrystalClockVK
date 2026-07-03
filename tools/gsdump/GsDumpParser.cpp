#include "GsDumpParser.hpp"

#include <array>
#include <bit>
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
// TEXA: TA0 = bits 0-7, AEM = bit 15, TA1 = bits 32-39 (GSRegs.h GIFRegTEXA).
GsTexa decTexa(uint64_t v) {
    return {static_cast<uint8_t>(lo32(v) & 0xff),
            static_cast<uint8_t>(hi32(v) & 0xff),
            bool((lo32(v) >> 15) & 1)};
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
// CLAMP: WMS 0-1, WMT 2-3, MINU 4-13, MAXU 14-23, MINV 24-33, MAXV 34-43.
GsClamp decClamp(uint64_t v) {
    GsClamp c{};
    c.wms = v & 3;
    c.wmt = (v >> 2) & 3;
    c.minu = (v >> 4) & 0x3ff;
    c.maxu = (v >> 14) & 0x3ff;
    c.minv = (v >> 24) & 0x3ff;
    c.maxv = (v >> 34) & 0x3ff;
    return c;
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
    REG_CLAMP_1 = 0x08, REG_CLAMP_2 = 0x09,
    REG_TEX1_1 = 0x14, REG_TEX1_2 = 0x15,
    REG_XYOFFSET_1 = 0x18, REG_XYOFFSET_2 = 0x19,
    REG_SCISSOR_1 = 0x40, REG_SCISSOR_2 = 0x41,
    REG_TEXA = 0x3b,
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

    // --- packet stream + GIF vertex-kick walk ---
    // PRIM (re)loads via the GIFtag PRE bit, A+D PRIM writes, or REGLIST PRIM
    // descriptors each start a new draw group; vertices accumulate on every XYZ
    // kick, snapshotting resolved register state at the group's first kick.
    std::array<uint64_t, 256> gsState{};  // GS register file (addr -> value)

    auto makeState = [&](uint32_t primField) {
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
        pr.texa = decTexa(gsState[REG_TEXA]);  // TEXA is not context-banked
        pr.tex0 = decTex0(gsState[pick(REG_TEX0_1, REG_TEX0_2)]);
        pr.tex1 = decTex1(gsState[pick(REG_TEX1_1, REG_TEX1_2)]);
        pr.clamp = decClamp(gsState[pick(REG_CLAMP_1, REG_CLAMP_2)]);
        pr.frame = decFrame(gsState[pick(REG_FRAME_1, REG_FRAME_2)]);
        pr.zbuf = decZbuf(gsState[pick(REG_ZBUF_1, REG_ZBUF_2)]);
        pr.scissor = decScissor(gsState[pick(REG_SCISSOR_1, REG_SCISSOR_2)]);
        pr.xyoffset = decXyOffset(gsState[pick(REG_XYOFFSET_1, REG_XYOFFSET_2)]);
        pr.dthe = gsState[REG_DTHE] & 1;
        pr.colclamp = gsState[REG_COLCLAMP] & 1;
        pr.pabe = gsState[REG_PABE] & 1;
        pr.fba = gsState[pick(REG_FBA_1, REG_FBA_2)] & 1;
        return pr;
    };

    uint32_t curPrimField = 0;
    bool pendingGroup = false;
    long curDrawIdx = -1;
    struct { uint8_t r = 0, g = 0, b = 0, a = 0; } curColor;
    struct { float u = 0, v = 0; bool valid = false; } curUV;
    struct { float s = 0, t = 0; bool valid = false; } curST;
    // GS internal Q: PACKED ST carries a temp Q; a PACKED RGBAQ write latches it
    // into RGBAQ.Q. REGLIST RGBAQ carries Q directly (bits 32-63). Reset = 1.0.
    float qTemp = 1.0f;
    float curQ = 1.0f;

    auto setPrim = [&](uint32_t primField) {
        curPrimField = primField;
        pendingGroup = true;
    };
    auto startGroupIfNeeded = [&]() {
        if (curDrawIdx >= 0 && !pendingGroup) return;
        out.prims.push_back(makeState(curPrimField));
        curDrawIdx = static_cast<long>(out.prims.size()) - 1;
        pendingGroup = false;
    };
    auto emitVertex = [&](uint32_t xRaw, uint32_t yRaw, uint32_t z, bool hasFog, uint8_t fog) {
        startGroupIfNeeded();
        GsPrimitive& d = out.prims[curDrawIdx];
        GsVertex v{};
        v.x = (static_cast<float>(xRaw) - static_cast<float>(d.xyoffset.ofx)) / 16.0f;
        v.y = (static_cast<float>(yRaw) - static_cast<float>(d.xyoffset.ofy)) / 16.0f;
        v.z = z;
        v.r = curColor.r; v.g = curColor.g; v.b = curColor.b; v.a = curColor.a;
        v.fog = hasFog ? fog : 0;
        if (d.prim.fst) { if (curUV.valid) { v.u = curUV.u; v.v = curUV.v; } }
        else if (curST.valid) { v.s = curST.s; v.t = curST.t; }
        v.q = curQ;
        d.verts.push_back(v);
        out.counts.kicks++;
    };

    // REGLIST descriptor: one 64-bit value (GIFReg* layout). Also the semantics
    // of an A+D write to a vertex-path register (addr <= 0x05).
    auto handleReglist = [&](uint8_t desc, uint64_t val) {
        const uint32_t lo = static_cast<uint32_t>(val), hi = static_cast<uint32_t>(val >> 32);
        switch (desc) {
            case 0x00: setPrim(lo & 0x7ff); break;
            case 0x01: curColor = {uint8_t(lo & 0xff), uint8_t((lo >> 8) & 0xff), uint8_t((lo >> 16) & 0xff), uint8_t((lo >> 24) & 0xff)}; curQ = std::bit_cast<float>(hi); break;
            case 0x02: curST = {std::bit_cast<float>(lo), std::bit_cast<float>(hi), true}; break;
            case 0x03: curUV = {(lo & 0x3fff) / 16.0f, ((lo >> 16) & 0x3fff) / 16.0f, true}; break;
            case 0x04: emitVertex(lo & 0xffff, (lo >> 16) & 0xffff, hi & 0xffffff, true, uint8_t((hi >> 24) & 0xff)); break;
            case 0x05: emitVertex(lo & 0xffff, (lo >> 16) & 0xffff, hi, false, 0); break;
            case 0x0f: break;  // NOP
            default: gsState[desc] = val; break;  // setup register (TEX0=6, CLAMP=8, ...)
        }
    };
    // PACKED descriptor unit (GIFPacked* 128-bit layouts).
    auto handlePacked = [&](uint8_t desc, const uint8_t* o) {
        const uint32_t w0 = rdU32(o), w1 = rdU32(o + 4), w2 = rdU32(o + 8), w3 = rdU32(o + 12);
        switch (desc) {
            case 0x00: setPrim(w0 & 0x7ff); break;
            case 0x01: curColor = {uint8_t(w0 & 0xff), uint8_t(w1 & 0xff), uint8_t(w2 & 0xff), uint8_t(w3 & 0xff)}; curQ = qTemp; break;
            case 0x02: curST = {std::bit_cast<float>(w0), std::bit_cast<float>(w1), true}; qTemp = std::bit_cast<float>(w2); break;
            case 0x03: curUV = {(w0 & 0x3fff) / 16.0f, (w1 & 0x3fff) / 16.0f, true}; break;
            case 0x04: emitVertex(w0 & 0xffff, w1 & 0xffff, (w2 >> 4) & 0xffffff, true, uint8_t((w3 >> 4) & 0xff)); break;
            case 0x05: emitVertex(w0 & 0xffff, w1 & 0xffff, w2, false, 0); break;
            case 0x0e: {  // A+D: value = low qword, addr at byte 8
                const uint8_t addr = o[8];
                const uint64_t val = rdU64(o);
                // A+D to a vertex-path register (PRIM/RGBAQ/ST/UV/XYZF2/XYZ2)
                // behaves like the REGLIST write: kicks vertices, updates the
                // color/texcoord latches. The clock stream has 24 XYZ2 + 12
                // RGBAQ + 12 PRIM writes via A+D.
                if (addr <= 0x05) handleReglist(addr, val);
                else gsState[addr] = val;
                break;
            }
            // PACKED setup-register descriptors (TEX0_1/2=0x06/07, CLAMP_1/2=
            // 0x08/09, FOG=0x0a, XYZF3/XYZ3=0x0c/0d): the register value is the
            // low qword, same as REGLIST. The clock's text stamps set their
            // PSMT4 font TEX0 this way — dropping these loses the whole font.
            case 0x06: case 0x07: case 0x08: case 0x09:
                gsState[desc] = rdU64(o);
                break;
            default: break;
        }
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
            if (pre) setPrim(prim);
            if (nloop == 0) continue;

            if (flg == 0) {
                for (uint32_t l = 0; l < nloop; l++)
                    for (uint32_t r = 0; r < nreg; r++) {
                        handlePacked((regsDesc >> (r * 4)) & 0xf, base + p);
                        p += 16;
                    }
            } else if (flg == 1) {
                const uint8_t* q = base + p;
                for (uint32_t l = 0; l < nloop; l++)
                    for (uint32_t r = 0; r < nreg; r++) {
                        handleReglist((regsDesc >> (r * 4)) & 0xf, rdU64(q));
                        q += 8;
                    }
                p += ((static_cast<uint64_t>(nloop) * nreg + 1) >> 1) * 16;
            } else {
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

    out.counts.draws = static_cast<uint32_t>(out.prims.size());
    return out;
}
