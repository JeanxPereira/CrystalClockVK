#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "GsDumpParser.hpp"

namespace {

const char* kPrim[] = {"POINT", "LINE", "LINE_STRIP", "TRI", "TRI_STRIP", "TRI_FAN", "SPRITE", "INVALID"};
const char* kABCD[] = {"Cs", "Cd", "0", "?"};
const char* kCval[] = {"As", "Ad", "FIX", "?"};
const char* kAtst[] = {"NEVER", "ALWAYS", "LESS", "LEQUAL", "EQUAL", "GEQUAL", "GREATER", "NOTEQUAL"};
const char* kAfail[] = {"KEEP", "FB_ONLY", "ZB_ONLY", "RGB_ONLY"};
const char* kZtst[] = {"NEVER", "ALWAYS", "GEQUAL", "GREATER"};

std::string psmName(uint8_t psm) {
    switch (psm) {
        case 0x00: return "PSMCT32";
        case 0x01: return "PSMCT24";
        case 0x02: return "PSMCT16";
        case 0x0a: return "PSMCT16S";
        case 0x13: return "PSMT8";
        case 0x14: return "PSMT4";
        case 0x1b: return "PSMT8H";
        case 0x24: return "PSMT4HL";
        case 0x2c: return "PSMT4HH";
        case 0x30: return "PSMZ32";
        case 0x31: return "PSMZ24";
        case 0x32: return "PSMZ16";
        case 0x3a: return "PSMZ16S";
        default: return std::to_string(psm);
    }
}

std::string fmt(const char* f, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

// Counts distinct formatted values across the primitives in stream order.
template <typename F>
void printUnique(const char* label, const GsCommandStream& s, F sel) {
    std::map<std::string, int> seen;
    std::vector<std::string> order;
    for (const auto& p : s.prims) {
        std::string k = sel(p);
        if (seen.find(k) == seen.end()) order.push_back(k);
        seen[k]++;
    }
    std::printf("%s\n", label);
    for (const auto& k : order) std::printf("  %4d  %s\n", seen[k], k.c_str());
}

void writeJson(const GsCommandStream& s, const std::string& path) {
    std::ofstream o(path, std::ios::binary);
    o << "[\n";
    for (size_t i = 0; i < s.prims.size(); i++) {
        const auto& p = s.prims[i];
        o << " {\"idx\":" << p.index
          << ",\"PRIM\":{\"type\":" << int(p.prim.type) << ",\"TME\":" << p.prim.tme
          << ",\"ABE\":" << p.prim.abe << ",\"FST\":" << p.prim.fst << ",\"CTXT\":" << int(p.prim.ctxt) << "}"
          << ",\"ALPHA\":{\"A\":" << int(p.alpha.a) << ",\"B\":" << int(p.alpha.b)
          << ",\"C\":" << int(p.alpha.c) << ",\"D\":" << int(p.alpha.d) << ",\"FIX\":" << int(p.alpha.fix) << "}"
          << ",\"TEST\":{\"ATE\":" << p.test.ate << ",\"ATST\":" << int(p.test.atst)
          << ",\"AREF\":" << int(p.test.aref) << ",\"ZTST\":" << int(p.test.ztst) << "}"
          << ",\"TEX0\":{\"TBP0\":" << p.tex0.tbp0 << ",\"TBW\":" << p.tex0.tbw
          << ",\"PSM\":" << int(p.tex0.psm) << ",\"TW\":" << p.tex0.tw << ",\"TH\":" << p.tex0.th << "}"
          << ",\"CLAMP\":{\"WMS\":" << int(p.clamp.wms) << ",\"WMT\":" << int(p.clamp.wmt)
          << ",\"MINU\":" << p.clamp.minu << ",\"MAXU\":" << p.clamp.maxu
          << ",\"MINV\":" << p.clamp.minv << ",\"MAXV\":" << p.clamp.maxv << "}"
          << ",\"FRAME\":{\"FBP\":" << p.frame.fbp << ",\"FBW\":" << int(p.frame.fbw)
          << ",\"PSM\":" << int(p.frame.psm) << "}"
          << ",\"SCISSOR\":{\"X0\":" << p.scissor.scax0 << ",\"X1\":" << p.scissor.scax1
          << ",\"Y0\":" << p.scissor.scay0 << ",\"Y1\":" << p.scissor.scay1 << "}"
          << ",\"DTHE\":" << p.dthe << ",\"COLCLAMP\":" << p.colclamp
          << ",\"PABE\":" << p.pabe << ",\"FBA\":" << p.fba
          << ",\"nverts\":" << p.verts.size()
          << ",\"verts\":[";
        for (size_t v = 0; v < p.verts.size(); v++) {
            const auto& k = p.verts[v];
            o << (v ? "," : "") << "{\"x\":" << k.x << ",\"y\":" << k.y
              << ",\"u\":" << k.u << ",\"v\":" << k.v
              << ",\"s\":" << k.s << ",\"t\":" << k.t << ",\"q\":" << k.q << "}";
        }
        o << "]}";
        o << (i + 1 < s.prims.size() ? ",\n" : "\n");
    }
    o << "]\n";
    std::printf("wrote %zu prims -> %s\n", s.prims.size(), path.c_str());
}

// Asserts the known clock_viewer.gs invariants. Returns non-zero on mismatch.
int verifyClock(const GsCommandStream& s) {
    int fails = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { std::printf("  FAIL: %s\n", what); fails++; }
    };
    check(s.header.serial == "20080220-175343", "serial");
    // 3948/21224 after A+D vertex-path handling (12 sprites arrive via A+D
    // PRIM/RGBAQ/XYZ2 writes; the pre-A+D totals were 3936/21200).
    check(s.counts.draws == 3948, "3948 draws");
    check(s.counts.kicks == 21224, "21224 verts");
    std::map<int, int> blend;  // distinct (A,B,C,D) blends seen
    uint64_t vtotal = 0;
    for (const auto& p : s.prims) {
        // Uniform GS state across the whole clock (dump-verified):
        check(!p.dthe, "DTHE off");
        check(p.colclamp, "COLCLAMP on");
        check(p.frame.psm == 0, "FRAME PSMCT32");
        check(p.texa.ta0 == 0x7f && p.texa.aem && p.texa.ta1 == 0x81, "TEXA TA0=0x7f AEM=1 TA1=0x81");
        check(p.alpha.c == 0, "blend C = As");  // As/128 scaling is universal
        blend[(p.alpha.a << 6) | (p.alpha.b << 4) | (p.alpha.c << 2) | p.alpha.d]++;
        vtotal += p.verts.size();
    }
    check(vtotal == s.counts.kicks, "vertex total matches kick count");
    check(blend.size() == 3, "exactly 3 blend modes (additive/src-over/subtractive)");
    if (fails) std::printf("verify: %d FAILURES\n", fails);
    else std::printf("verify: OK (all clock invariants hold)\n");
    return fails;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: gsdump <decompressed.gs> [--verify] [--json <out>]\n");
        return 2;
    }
    const std::string inPath = argv[1];
    bool verify = false;
    std::string jsonOut;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--verify") verify = true;
        else if (a == "--json" && i + 1 < argc) jsonOut = argv[++i];
    }

    std::ifstream in(inPath, std::ios::binary | std::ios::ate);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", inPath.c_str());
        return 1;
    }
    const std::streamsize n = in.tellg();
    in.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(n));
    in.read(reinterpret_cast<char*>(buf.data()), n);

    GsCommandStream s;
    try {
        s = GsDumpParser::parse(buf.data(), buf.size());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "parse error: %s\n", e.what());
        return 1;
    }

    const auto& h = s.header;
    const auto& c = s.counts;
    std::printf("=== GSDumpHeader ===\n");
    std::printf("state_version %u  state_size %u  crc 0x%x\n", h.stateVersion, h.stateSize, h.crc);
    std::printf("serial \"%s\"  screenshot %ux%u\n", h.serial.c_str(), h.screenshotWidth, h.screenshotHeight);
    std::printf("\n=== packet stream ===\n");
    std::printf("transfers %u  vsync %u  readfifo %u  regs %u\n", c.transfers, c.vsync, c.readfifo, c.regsPackets);
    std::printf("giftags %u  nloop(sum) %llu  draws %u  kicks/verts %u\n", c.giftags,
                static_cast<unsigned long long>(c.nloopSum), c.draws, c.kicks);
    std::printf("FLG: PACKED %u  REGLIST %u  IMAGE %u  IMAGE2 %u\n", c.flgPacked, c.flgReglist, c.flgImage, c.flgImage2);

    std::map<size_t, int> drawSizes;
    for (const auto& p : s.prims) drawSizes[p.verts.size()]++;
    std::printf("\n=== %zu draws decoded; sizes (nverts: count):", s.prims.size());
    for (auto& [n, cnt] : drawSizes) std::printf(" %zu:%d", n, cnt);
    std::printf(" ===\n");
    printUnique("\nPRIM:", s, [](const GsPrimitive& p) {
        return fmt("%s IIP%d TME%d ABE%d FST%d CTXT%d", kPrim[p.prim.type & 7], p.prim.iip,
                   p.prim.tme, p.prim.abe, p.prim.fst, int(p.prim.ctxt));
    });
    printUnique("\nALPHA (A-B)*C/128+D:", s, [](const GsPrimitive& p) {
        return fmt("(%s-%s)*%s/128+%s  FIX=%d", kABCD[p.alpha.a & 3], kABCD[p.alpha.b & 3],
                   kCval[p.alpha.c & 3], kABCD[p.alpha.d & 3], int(p.alpha.fix));
    });
    printUnique("\nTEST:", s, [](const GsPrimitive& p) {
        return fmt("ATE%d %s AREF%d AFAIL=%s ZTE%d ZTST=%s", p.test.ate, kAtst[p.test.atst & 7],
                   int(p.test.aref), kAfail[p.test.afail & 3], p.test.zte, kZtst[p.test.ztst & 3]);
    });
    printUnique("\nTEX0:", s, [](const GsPrimitive& p) {
        return fmt("TBP0=%u TBW=%u PSM=%s %dx%d TCC%d TFX%d", p.tex0.tbp0, p.tex0.tbw,
                   psmName(p.tex0.psm).c_str(), 1 << p.tex0.tw, 1 << p.tex0.th, p.tex0.tcc, p.tex0.tfx);
    });
    printUnique("\nFRAME:", s, [](const GsPrimitive& p) {
        return fmt("FBP=%u FBW=%u(*64px) PSM=%s FBMSK=0x%x", p.frame.fbp, p.frame.fbw,
                   psmName(p.frame.psm).c_str(), p.frame.fbmsk);
    });
    printUnique("\nscalar toggles (DTHE/COLCLAMP/PABE/FBA):", s, [](const GsPrimitive& p) {
        return fmt("DTHE%d COLCLAMP%d PABE%d FBA%d", p.dthe, p.colclamp, p.pabe, p.fba);
    });

    if (!jsonOut.empty()) writeJson(s, jsonOut);
    if (verify) return verifyClock(s);
    return 0;
}
