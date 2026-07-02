// GS STQ perspective divisor: the parser must capture RGBAQ.Q per vertex.
// GS semantics (GS User's Manual, GIF packed/reglist formats):
//  - PACKED ST (desc 0x02) carries S, T and Q (dword 2) — Q goes to an internal
//    temp register.
//  - PACKED RGBAQ (desc 0x01) latches the temp Q into RGBAQ.Q.
//  - REGLIST RGBAQ carries Q directly in bits 32-63 (float).
//  - Reset value of RGBAQ.Q is 1.0f.
// The rasterizer divides S,T by Q at sampling time; without Q the clock's rod
// faces (STQ tri-strips, s,t ~ [0,0.05]) sample a single texel.

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "GsDumpParser.hpp"

namespace {

int g_fails = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("  FAIL: %s\n", what.c_str());
        g_fails++;
    }
}
bool near(float a, float b) { return std::fabs(a - b) < 1e-6f; }

void putU32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; i++) b.push_back(uint8_t(v >> (i * 8)));
}
void putU64(std::vector<uint8_t>& b, uint64_t v) {
    for (int i = 0; i < 8; i++) b.push_back(uint8_t(v >> (i * 8)));
}
void putF32(std::vector<uint8_t>& b, float f) { putU32(b, std::bit_cast<uint32_t>(f)); }

void putGifTag(std::vector<uint8_t>& b, uint32_t nloop, uint32_t prim, uint32_t flg,
               uint32_t nreg, uint64_t regsDesc) {
    putU32(b, nloop | (1u << 15));                            // nloop + EOP
    putU32(b, (1u << 14) | (prim << 15) | (flg << 26) | (nreg << 28));  // PRE=1
    putU64(b, regsDesc);
}

std::vector<uint8_t> buildDump(const std::vector<uint8_t>& gifPayload) {
    std::vector<uint8_t> d;
    putU32(d, 0);          // leadCrc
    putU32(d, 36);         // headerSize
    const size_t blob = d.size();
    for (int i = 0; i < 9; i++) putU32(d, 0);
    // stateVersion=9, stateSize=4, serialOffset/size=0, crc=0, w=640, h=480
    uint32_t v = 9;   std::memcpy(d.data() + blob + 0, &v, 4);
    v = 4;            std::memcpy(d.data() + blob + 4, &v, 4);
    v = 640;          std::memcpy(d.data() + blob + 20, &v, 4);
    v = 480;          std::memcpy(d.data() + blob + 24, &v, 4);
    d.insert(d.end(), 4, 0);       // freeze (stateSize bytes)
    d.insert(d.end(), 0x2000, 0);  // GSPrivRegSet
    d.push_back(0);                // packet id: Transfer
    d.push_back(3);                // path
    putU32(d, uint32_t(gifPayload.size()));
    d.insert(d.end(), gifPayload.begin(), gifPayload.end());
    return d;
}

}  // namespace

int main() {
    std::vector<uint8_t> gif;
    const uint32_t primStq = 4;  // TRI_STRIP, FST=0 (STQ)

    // Group 0: bare XYZ2 kick, no ST/RGBAQ ever written -> Q = reset value 1.0.
    putGifTag(gif, 1, primStq, 0, 1, 0x5);  // PACKED, regs: XYZ2
    putU32(gif, 1600); putU32(gif, 1600); putU32(gif, 0); putU32(gif, 0);

    // Group 1: PACKED ST(S,T,Q) then RGBAQ (latches Q) then kick.
    putGifTag(gif, 1, primStq, 0, 3, 0x512);  // regs: ST, RGBAQ, XYZ2
    putF32(gif, 0.02f); putF32(gif, 0.04f); putF32(gif, 0.05f); putU32(gif, 0);
    putU32(gif, 10); putU32(gif, 20); putU32(gif, 30); putU32(gif, 40);
    putU32(gif, 1600); putU32(gif, 1600); putU32(gif, 0); putU32(gif, 0);

    // Group 2: PACKED ST writes a NEW temp Q, but no RGBAQ write follows ->
    // the kick still sees the previously latched Q (0.05), not 0.5.
    putGifTag(gif, 1, primStq, 0, 2, 0x52);  // regs: ST, XYZ2
    putF32(gif, 0.10f); putF32(gif, 0.20f); putF32(gif, 0.5f); putU32(gif, 0);
    putU32(gif, 1600); putU32(gif, 1600); putU32(gif, 0); putU32(gif, 0);

    // Group 3: REGLIST RGBAQ (Q in bits 32-63) then XYZ2.
    putGifTag(gif, 1, primStq, 1, 2, 0x51);  // REGLIST, regs: RGBAQ, XYZ2
    putU64(gif, uint64_t(std::bit_cast<uint32_t>(0.25f)) << 32 | 0x04030201u);
    putU64(gif, (uint64_t(0) << 32) | (100u << 16) | 100u);

    // Group 4: CLAMP_1 via A+D (WMS=1 CLAMP, WMT=3 REGION_REPEAT,
    // MINU=5 MAXU=100, MINV=255 MAXV=63), then a kick.
    putGifTag(gif, 2, primStq, 0, 1, 0xe);  // PACKED, reg: A+D, nloop=2
    const uint64_t clampVal = 1ull | (3ull << 2) | (5ull << 4) | (100ull << 14)
                            | (255ull << 24) | (63ull << 34);
    putU64(gif, clampVal); gif.push_back(0x08);              // addr = CLAMP_1
    for (int i = 0; i < 7; i++) gif.push_back(0);
    putU64(gif, (uint64_t(0) << 32) | (100u << 16) | 100u);  // XYZ2 kick
    gif.push_back(0x05);
    for (int i = 0; i < 7; i++) gif.push_back(0);

    const std::vector<uint8_t> dump = buildDump(gif);
    const GsCommandStream s = GsDumpParser::parse(dump.data(), dump.size());

    check(s.prims.size() == 5, "5 draw groups parsed, got " + std::to_string(s.prims.size()));
    if (s.prims.size() != 5 ||
        s.prims[0].verts.size() != 1 || s.prims[1].verts.size() != 1 ||
        s.prims[2].verts.size() != 1 || s.prims[3].verts.size() != 1 ||
        s.prims[4].verts.size() != 1) {
        std::printf("FAILED (bad group/vertex structure)\n");
        return 1;
    }

    check(near(s.prims[0].verts[0].q, 1.0f), "reset Q = 1.0");

    check(near(s.prims[1].verts[0].s, 0.02f), "PACKED S");
    check(near(s.prims[1].verts[0].t, 0.04f), "PACKED T");
    check(near(s.prims[1].verts[0].q, 0.05f), "PACKED ST Q latched via RGBAQ");

    check(near(s.prims[2].verts[0].q, 0.05f), "ST without RGBAQ keeps old Q");

    check(near(s.prims[3].verts[0].q, 0.25f), "REGLIST RGBAQ Q from bits 32-63");

    check(s.prims[3].clamp.wms == 0 && s.prims[3].clamp.wmt == 0,
          "CLAMP resets to REPEAT/REPEAT");
    const GsClamp& c4 = s.prims[4].clamp;
    check(c4.wms == 1, "CLAMP WMS");
    check(c4.wmt == 3, "CLAMP WMT");
    check(c4.minu == 5 && c4.maxu == 100, "CLAMP MINU/MAXU");
    check(c4.minv == 255 && c4.maxv == 63, "CLAMP MINV/MAXV");

    if (g_fails) {
        std::printf("FAILED (%d)\n", g_fails);
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
