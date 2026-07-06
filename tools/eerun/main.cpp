#include "ee/EeInterpreter.hpp"
#include "ee/EeMemory.hpp"
#include "GsDumpParser.hpp"
#include "gs/GsCommandStream.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace ps2ee;

namespace {

// SP1 interpreter capture (Task 4, sp1-interpreter-runs.md): the GIF DMA kick
// observed for 0x00232618 is D2_MADR=0x00296DF0, D2_QWC=3 -- i.e. the actual
// hardware transfer is EXACTLY 48 bytes (one GIFtag quadword + 2 PACKED A+D
// payload quadwords), per the literal D2_QWC MMIO value logged. The leading
// GIFtag quadword itself is never touched by a store during this run (it is
// a pre-built template resident in the base RAM image, per Task 4's
// "GIF-tag template staging near 0x00296E00" note) -- only the bytes AT and
// AFTER 0x00296E00 appear in the store dump. To reconstruct the real
// transferred bytes we therefore need the base RAM image for the untouched
// template prefix, with the captured stores overlaid on top for the dynamic
// tail. The window is deliberately QWC-precise, not a generous guess: bytes
// past 0x00296E20 sit in the same reused scratch buffer but were NOT part of
// THIS DMA transfer, so decoding them as if they were would misattribute
// leftover/stale buffer content to this packet.
constexpr uint32_t kGifWindowStart = 0x00296DF0;
constexpr uint32_t kGifWindowLen = 0x30;  // D2_QWC=3 * 16 bytes, the literal DMA transfer size

std::string siblingPath(const std::string& path, const std::string& name) {
    const size_t slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? name : path.substr(0, slash + 1) + name;
}

// Standard MIPS/EE GPR names, for --regs-at dumps.
constexpr const char* kGprNames[32] = {
    "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
    "t0",   "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0",   "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "t8",   "t9", "k0", "k1", "gp", "sp", "s8", "ra",
};

// Phase 2 -- staging format DECODED (sp1-interpreter-runs.md, "Phase 2 --
// staging format DECODED" section): draw_crystal_rod's internal SPR staging
// record is NOT a hardware GIFtag -- decodeGifData misreads it (see the report
// this session updates). Per rod = 112 bytes: 16-byte header (ignored for
// geometry) + 4 vertices x 24 bytes. Per-vertex layout [DUMP-MEASURED]:
//   +0x00, +0x04: floats (intermediate, ignored)
//   +0x08: RGBA8
//   +0x0c: float (ignored)
//   +0x10: screen X|Y packed 12.4 fixed -- low16=X, high16=Y (both /16 = px)
//   +0x14: 0xFFFFF010 marker (ignored)
constexpr size_t kRodRecordBytes = 112;
constexpr size_t kRodHeaderBytes = 16;
constexpr size_t kRodVertBytes = 24;
constexpr size_t kRodVertsPerRod = 4;

// GS XYOFFSET (OFX, OFY), in raw 12.4 fixed-point units. Phase 2 TIGHTEN
// (phase2-tighten-report.md): this is now the REAL value read out of
// re/oracle/clock_sw_prims.json's "xyoffset" field (added to gsdump's
// writeJson this session) for the actual rod draws -- the 40 60-vert
// TRI_STRIP (PRIM.type=4) draws with TEX0.TBP0=11264, rod-glow color
// ~(210,230,230,64), drawn to FBP=0 (the pre-composite/refraction buffer).
// All 40 share OFX=27648 raw; OFY alternates between 30976 and 30984 raw
// (a 0.5px shift) across the 4 repeated draw-clusters in the dump -- this
// is a PS2 interlaced-field jitter (odd/even field vertical offset), not
// noise: idx clusters {1-16},{1900-1915} use 30984 and {949-964},
// {2851-2866} use 30976, alternating in dump order. We pick the first
// cluster's value (30984); the previous OFX=25788/OFY=31656 was a fitted
// value from an exhaustive nearest-vertex search and is NOT close to this
// real value (~116px off in X, ~42px off in Y) -- see the report for the
// resulting vdiff --cloud match with this real offset.
constexpr double kOfxRaw = 27648.0;  // 1728.0 px
constexpr double kOfyRaw = 30984.0;  // 1936.5 px

// Parses the raw internal staging bytes directly (per the mapped layout
// above) into one GsPrimitive per rod, PRIM.type=4 (TRI_STRIP), 4 verts each.
// This is deliberately NOT decodeGifData -- that assumes a real hardware
// GIFtag, which this staging buffer is not (see sp1-interpreter-runs.md).
// ofxRaw/ofyRaw default to the MENU's real-register XYOFFSET (kOfxRaw/
// kOfyRaw) but are caller-overridable -- the Visor driver (Phase 2 "THE
// DIAL FOUND" follow-up) has no captured .gs dump of its own to read a
// confirmed XYOFFSET from, so reusing the menu's real value is an explicit,
// disclosed ASSUMPTION (same rasterizer/pipeline family, not a fitted guess).
GsCommandStream decodeStagingDirect(const uint8_t* spr, size_t len, uint32_t rodCount,
                                     double ofxRaw = kOfxRaw, double ofyRaw = kOfyRaw) {
    GsCommandStream stream;
    for (uint32_t r = 0; r < rodCount; r++) {
        const size_t rodOff = r * kRodRecordBytes;
        if (rodOff + kRodRecordBytes > len) break;
        const uint8_t* rod = spr + rodOff;
        GsPrimitive prim{};
        prim.index = r;
        prim.prim.type = 4;  // TRI_STRIP, per the contract's "start with type 4"
        for (size_t v = 0; v < kRodVertsPerRod; v++) {
            const uint8_t* vp = rod + kRodHeaderBytes + v * kRodVertBytes;
            uint32_t xy;
            std::memcpy(&xy, vp + 0x10, 4);
            const uint32_t xRaw = xy & 0xFFFF;
            const uint32_t yRaw = (xy >> 16) & 0xFFFF;
            GsVertex vert{};
            vert.x = static_cast<float>((static_cast<double>(xRaw) - ofxRaw) / 16.0);
            vert.y = static_cast<float>((static_cast<double>(yRaw) - ofyRaw) / 16.0);
            vert.r = vp[0x08];
            vert.g = vp[0x09];
            vert.b = vp[0x0a];
            vert.a = vp[0x0b];
            prim.verts.push_back(vert);
        }
        stream.prims.push_back(std::move(prim));
    }
    stream.counts.draws = static_cast<uint32_t>(stream.prims.size());
    return stream;
}

// Phase 2 task 2: drive the per-rod renderer directly, bypassing 0x00233928's
// Deci2-blocked body entirely (per phase2-render-contract.md). Seeds/state are
// all [DUMP-MEASURED] from re/ram/clock/eeMemory.bin -- see the contract doc
// for the disassembly this is transcribed from.
int runDriveRods(int argc, char** argv) {
    // argv[1] = image path (argv[0] is the exe, argv[2] is "--drive-rods").
    std::string imagePath = argv[1];
    std::string jsonOut;
    std::string stagingJsonOut;
    // Phase 2 -- Visor dial render: the menu's ctx (0x00296AB0, fixed global,
    // reached via the config-menu preview widget 0x0022C8D0/0x00233928) is
    // NOT the only context this shared rod-render machinery runs under. The
    // Visor (full-screen screensaver dial) drives the SAME rod array/
    // draw_crystal_rod through a DIFFERENT top-level entry (0x00233F60,
    // reached indirectly via a scene-object linked list at 0x00371200 and a
    // dispatch stub at 0x0022BCA8 -- ctx = object + 0x10) with its OWN ctx
    // struct, live-confirmed via pcsx2 DebugServer at 0x003715D0 for the
    // clock_viewer capture. --ctx lets the caller point at either.
    uint32_t ctxOverride = 0x00296AB0;
    double ofxOverride = kOfxRaw;
    double ofyOverride = kOfyRaw;
    bool runTransformPass = false;
    // $gp for gp-relative global loads. The transform core 0x002329F8 reads
    // its intensity/fade constants via $gp (lwc1 fN, -0x7dXX($gp)) and a bare
    // call() leaves $gp = 0, which silently reads garbage from low RAM (the
    // earlier 0x22f720 cursor-init fallback was the same root cause).
    // 0x002CFEF0 is the live-verified OSDSYS value (w2-rod-generation.md,
    // read from pcsx2 DebugServer; the stale 0x002AF070 there is FALSIFIED).
    uint32_t gpValue = 0x002CFEF0;
    for (int i = 3; i < argc; i++) {
        if (!std::strcmp(argv[i], "--json") && i + 1 < argc) jsonOut = argv[++i];
        else if (!std::strcmp(argv[i], "--staging-json") && i + 1 < argc) stagingJsonOut = argv[++i];
        else if (!std::strcmp(argv[i], "--ctx") && i + 1 < argc)
            ctxOverride = std::strtoul(argv[++i], nullptr, 16);
        else if (!std::strcmp(argv[i], "--ofx") && i + 1 < argc)
            ofxOverride = std::strtod(argv[++i], nullptr);
        else if (!std::strcmp(argv[i], "--ofy") && i + 1 < argc)
            ofyOverride = std::strtod(argv[++i], nullptr);
        else if (!std::strcmp(argv[i], "--transform"))
            runTransformPass = true;
        else if (!std::strcmp(argv[i], "--gp") && i + 1 < argc)
            gpValue = std::strtoul(argv[++i], nullptr, 16);
    }
    if (imagePath.empty() || jsonOut.empty()) {
        std::printf("usage: eerun <image.bin> --drive-rods --json <out.json> "
                    "[--staging-json <out.json>] [--ctx <hex>] [--ofx <raw>] [--ofy <raw>]\n");
        return 2;
    }

    EeMemory mem;
    if (!mem.loadImage(imagePath)) { std::printf("bad image\n"); return 2; }
    EeInterpreter cpu(mem);
    if (const char* ov = std::getenv("EERUN_MAX_INSTR")) cpu.maxInstructions = std::strtoull(ov, nullptr, 10);
    cpu.gpr[28].lo = gpValue;  // $gp survives call() (callee-saved by MIPS ABI)
    std::printf("drive-rods: gp=%08X\n", gpValue);

    const uint32_t kCtx = ctxOverride;             // per-frame context struct
    const uint32_t kCtxColor = kCtx + 0xA0;        // a1 for draw_crystal_rod (RGBA)
    constexpr uint32_t kRodBase = 0x00375250;      // rod array
    constexpr uint32_t kRodStride = 0x160;
    constexpr uint32_t kPktCtx = 0x00375230;       // GS-packet cursor/base struct
    constexpr uint32_t kInitFn = 0x0022F720;       // packet-cursor (re)init
    constexpr uint32_t kDrawFn = 0x00232E38;       // draw_crystal_rod (leaf emitter)
    // Phase 2 -- Visor dial render: the render-contract's pass #1
    // (0x00232DA0, "matrix-apply prep") runs BEFORE draw_crystal_rod in
    // 0x00233928's own per-rod loop, but the menu capture's screen coords
    // were already baked (static preview), so the original --drive-rods
    // harness never needed to call it. The Visor's own driver (0x00233F60)
    // calls 0x00232DA0 per rod too (confirmed: jal hits at 0x234200 etc.,
    // inside 0x00233F60's body, live-verified via pcsx2 backtrace/registers)
    // -- and the Visor's raw rod-array +0x10/+0x14 fields read as tiny
    // (-0.35..2.6) values, NOT screen pixels, unlike the menu's. --transform
    // opts into calling it, a0=rodPtr a1=ctx a2=0 (args per the contract
    // table; a2's exact role unconfirmed, 0 is the harness's best-effort
    // guess -- not asserted correct, see phase2-visor-report.md).
    constexpr uint32_t kTransformFn = 0x00232DA0;
    // Task 1 (phase2 discovery, task-1-report.md): the batch-start snapshot
    // draw_crystal_rod's own call site never performs, but the finalize step
    // (0x0022F7F8, reached via the 0x00235350 tail-jump stub) requires --
    // it reads *(structPtr+0x14) as "batch start" to compute the qword count
    // for the placeholder GIFtag header word it patches in place. Sibling
    // emitter 0x00232F80 performs this snapshot immediately after its own
    // 0x22F720 init call; draw_crystal_rod's call chain never does, and the
    // live capture confirms +0x14 is genuinely zero pre-snapshot.
    constexpr uint32_t kPktBatchStartOff = 0x14;
    constexpr uint32_t kFinalizeFn = 0x00235350;   // tail-jump stub -> 0x0022F7F8(a0=kPktCtx)

    // One-time init: point the packet cursor at fresh SPR, per the contract.
    cpu.call(kInitFn, kPktCtx);
    uint32_t cursorStart = mem.read32(kPktCtx);
    uint32_t sprBase = mem.read32(kPktCtx + 4);
    bool usedFallback = false;
    if (!EeMemory::isSpr(cursorStart) || !EeMemory::isSpr(sprBase)) {
        // 0x22f720 misbehaved (most likely: $gp is unmodeled by this bare
        // interpreter, so its gp-relative bank-selector load reads garbage).
        // Contract doc's explicit sanctioned fallback: poke the two fields to
        // the values already live/correct in the captured frame.
        usedFallback = true;
        cursorStart = 0x70000060;
        sprBase = 0x70000000;
        mem.write32(kPktCtx, cursorStart);
        mem.write32(kPktCtx + 4, sprBase);
    }
    std::printf("drive-rods: packet cursor init %s -> cursor=%08X base=%08X\n",
                usedFallback ? "FELL BACK (poked fixed values)" : "via 0x22f720 (sane)",
                cursorStart, sprBase);

    // NEW precondition (Task 2, per Task 1's decision): snapshot the
    // batch-start pointer that 0x0022F7F8's finalize reads from +0x14.
    // draw_crystal_rod never sets this; the sibling emitter 0x00232F80 does,
    // right after its own init call -- mirrored here at the same point.
    mem.write32(kPktCtx + kPktBatchStartOff, mem.read32(kPktCtx));
    std::printf("drive-rods: batch-start snapshot +0x14 = %08X\n",
                mem.read32(kPktCtx + kPktBatchStartOff));

    const uint32_t rodCount = mem.read32(kCtx + 4);
    std::printf("drive-rods: ctx=%08X rodCount(ctx+4)=%u\n", kCtx, rodCount);

    uint32_t jxBits = mem.read32(kCtx + 0xB0);
    uint32_t jyBits = mem.read32(kCtx + 0xB4);
    float jx, jy;
    std::memcpy(&jx, &jxBits, 4);
    std::memcpy(&jy, &jyBits, 4);
    std::printf("drive-rods: jitter f12=%f f13=%f\n", jx, jy);

    uint32_t rodsProcessed = 0, rodsCulled = 0;
    for (uint32_t i = 0; i < rodCount; i++) {
        const uint32_t rodPtr = kRodBase + i * kRodStride;
        const uint32_t skipFlag = mem.read32(rodPtr + 0x150);
        if (skipFlag != 0) { rodsCulled++; continue; }
        rodsProcessed++;
        if (runTransformPass) {
            try {
                cpu.call(kTransformFn, rodPtr, kCtx, 0);
            } catch (const EeError& e) {
                std::printf("TRANSFORM EeError rod=%u pc=%08X word=%08X: %s\n", i, e.pc, e.word, e.what.c_str());
                std::map<uint32_t, int> calls;
                for (uint32_t t : cpu.trace) calls[t]++;
                for (auto& [t, n] : calls) std::printf("  call %08X x%d\n", t, n);
                return 1;
            }
        }
        cpu.fpr[12] = jx;
        cpu.fpr[13] = jy;
        cpu.call(kDrawFn, rodPtr, kCtxColor);
    }
    const uint32_t cursorAfterRods = mem.read32(kPktCtx);
    const uint32_t bytesFromRods = (cursorAfterRods >= cursorStart) ? (cursorAfterRods - cursorStart) : 0;
    std::printf("drive-rods: rods processed=%u culled=%u, SPR cursor %08X -> %08X (%u bytes emitted)\n",
                rodsProcessed, rodsCulled, cursorStart, cursorAfterRods, bytesFromRods);
    if (std::getenv("EERUN_DUMP_SPR_PRE")) {
        const uint32_t off = cursorStart - EeMemory::kSprBase;
        const uint8_t* p = mem.sprData() + off;
        std::printf("drive-rods: PRE-finalize raw SPR bytes [%08X..%08X]:\n", cursorStart, cursorAfterRods);
        for (uint32_t k = 0; k < bytesFromRods; k++) {
            std::printf("%02X ", p[k]);
            if ((k + 1) % 16 == 0) std::printf("\n");
        }
        std::printf("\n");
    }

    // Direct staging decode (Phase 2 "staging format DECODED" pivot): parse
    // the raw internal record layout ourselves instead of feeding it through
    // decodeGifData (which misreads it as a hardware GIFtag -- see the report
    // this run is validating). Uses the rod-emission byte range captured
    // above, BEFORE finalize's header patch (irrelevant to vertex geometry).
    if (!stagingJsonOut.empty()) {
        const uint32_t off = cursorStart - EeMemory::kSprBase;
        GsCommandStream stagingStream =
            decodeStagingDirect(mem.sprData() + off, bytesFromRods, rodsProcessed, ofxOverride, ofyOverride);
        GsDumpParser::writeJson(stagingStream, stagingJsonOut);
        std::printf("drive-rods: staging decode -> %zu prims (%u rods) -> %s\n",
                    stagingStream.prims.size(), rodsProcessed, stagingJsonOut.c_str());
    }

    // NEW finalize (Task 2, per Task 1's decision): 0x00235350 is a pure
    // tail-jump stub to 0x0022F7F8(a0=kPktCtx). This patches the placeholder
    // GIFtag header word IN PLACE at the batch-start address (t0 = *(kPktCtx
    // +0x14), snapshotted above) using (cursor - batchStart)>>3 as the qword
    // count, then tail-calls the same DMA-kick family 0x00232618's path
    // uses. After this the SPR buffer holds a real 16-byte GIFtag wire
    // packet instead of draw_crystal_rod's raw 8-byte staging header.
    if (const char* ov = std::getenv("EERUN_MAX_INSTR")) cpu.maxInstructions = std::strtoull(ov, nullptr, 10);
    if (std::getenv("EERUN_TRACE_FINALIZE")) cpu.traceCalls = true;
    try {
        cpu.call(kFinalizeFn);
    } catch (const EeError& e) {
        std::printf("FINALIZE EeError pc=%08X word=%08X: %s\n", e.pc, e.word, e.what.c_str());
        std::map<uint32_t, int> calls;
        for (uint32_t t : cpu.trace) calls[t]++;
        for (auto& [t, n] : calls) std::printf("  call %08X x%d\n", t, n);
        return 1;
    }
    const uint32_t cursorEnd = mem.read32(kPktCtx);
    const uint32_t bytesEmitted = (cursorEnd >= cursorStart) ? (cursorEnd - cursorStart) : 0;
    std::printf("drive-rods: after finalize(0x235350): SPR cursor -> %08X (%u bytes total)\n",
                cursorEnd, bytesEmitted);

    GsCommandStream stream;
    if (bytesEmitted > 0 && EeMemory::isSpr(cursorStart)) {
        const uint32_t off = cursorStart - EeMemory::kSprBase;
        const uint32_t len = std::min(bytesEmitted, EeMemory::kSprSize - off);
        if (std::getenv("EERUN_DUMP_SPR")) {
            const uint8_t* p = mem.sprData() + off;
            std::printf("drive-rods: raw SPR bytes [%08X..%08X]:\n", cursorStart, cursorStart + len);
            for (uint32_t k = 0; k < len; k++) {
                std::printf("%02X ", p[k]);
                if ((k + 1) % 16 == 0) std::printf("\n");
            }
            std::printf("\n");
        }
        GsDumpParser::decodeGifData(stream, mem.sprData() + off, len);
    }
    std::printf("drive-rods: decoded %u draws, %u kicks, %u giftags\n",
                stream.counts.draws, stream.counts.kicks, stream.counts.giftags);

    GsDumpParser::writeJson(stream, jsonOut);
    std::printf("drive-rods: wrote %zu prims -> %s\n", stream.prims.size(), jsonOut.c_str());
    return 0;
}

// Phase 2 -- Visor dial render, "execute the whole driver" probe. The
// hand-rolled per-rod pass driving above ([--transform]) proved the rod
// array's position fields are PRE-transform and only the driver's own body
// rewrites them per frame (its prologue calls 0x002335E8, a sceVu0MulMatrix/
// ApplyMatrix position pass over the rod array, before any per-rod loop).
// 0x00233F60 interpreted cleanly end-to-end in a bare generic run (no Deci2,
// no opcode gaps), so this mode makes the call ABI-faithful instead: the real
// dispatch stub 0x0022BCA8 does
//     f13 = float(int32 @ obj+0x110); f12 = float @ obj+0xF4;
//     a0 = obj+0x10 (ctx); a1 = obj+0x100; j 0x233F60
// with obj = the scene-object node (0x003715C0 in the clock_viewer capture,
// live-verified via the 0x00371200 list walk). Prints the rod-array screen
// fields before/after so the "does the driver break the one-position
// collapse?" question is answered directly from executed original code.
int runDriveVisor(int argc, char** argv) {
    std::string imagePath = argv[1];
    uint32_t objAddr = 0x003715C0;
    uint32_t gpValue = 0x002CFEF0;  // live-verified (w2-rod-generation.md)
    // Default entry = the render driver via the stub ABI. --entry 22B928
    // instead calls the object's WHOLE per-frame handler (update 0x2338D8 +
    // mode dispatch + render), a0 = obj -- one level up the original chain.
    uint32_t entry = 0x00233F60;
    for (int i = 3; i < argc; i++) {
        if (!std::strcmp(argv[i], "--obj") && i + 1 < argc)
            objAddr = std::strtoul(argv[++i], nullptr, 16);
        else if (!std::strcmp(argv[i], "--gp") && i + 1 < argc)
            gpValue = std::strtoul(argv[++i], nullptr, 16);
        else if (!std::strcmp(argv[i], "--entry") && i + 1 < argc)
            entry = std::strtoul(argv[++i], nullptr, 16);
    }
    EeMemory mem;
    if (!mem.loadImage(imagePath)) { std::printf("bad image\n"); return 2; }
    EeInterpreter cpu(mem);
    if (const char* ov = std::getenv("EERUN_MAX_INSTR")) cpu.maxInstructions = std::strtoull(ov, nullptr, 10);
    cpu.gpr[28].lo = gpValue;

    const uint32_t ctx = objAddr + 0x10;
    const uint32_t a1 = objAddr + 0x100;
    uint32_t f12Bits = mem.read32(objAddr + 0xF4);
    const int32_t f13Int = static_cast<int32_t>(mem.read32(objAddr + 0x110));
    float f12;
    std::memcpy(&f12, &f12Bits, 4);
    const float f13 = static_cast<float>(f13Int);
    std::printf("drive-visor: obj=%08X ctx=%08X a1=%08X f12=%f f13=%f gp=%08X\n",
                objAddr, ctx, a1, f12, f13, gpValue);

    constexpr uint32_t kRodBase = 0x00375250;
    constexpr uint32_t kRodStride = 0x160;
    const uint32_t rodCount = mem.read32(ctx + 4);
    auto dumpRods = [&](const char* tag) {
        std::printf("drive-visor: rod fields %s (slot: +0x10 +0x14 | +0x40 | skip+0x150)\n", tag);
        for (uint32_t i = 0; i < rodCount && i < 16; i++) {
            const uint32_t rp = kRodBase + i * kRodStride;
            uint32_t xb = mem.read32(rp + 0x10), yb = mem.read32(rp + 0x14),
                     sb = mem.read32(rp + 0x40), skip = mem.read32(rp + 0x150);
            float x, y, s;
            std::memcpy(&x, &xb, 4); std::memcpy(&y, &yb, 4); std::memcpy(&s, &sb, 4);
            std::printf("  rod %2u: %12.4f %12.4f | %10.6f | %u\n", i, x, y, s, skip);
        }
    };
    dumpRods("BEFORE");

    cpu.fpr[12] = f12;
    cpu.fpr[13] = f13;
    try {
        if (entry == 0x00233F60)
            cpu.call(entry, ctx, a1);
        else
            cpu.call(entry, objAddr);  // whole-handler form: a0 = obj
    } catch (const EeError& e) {
        std::printf("drive-visor: EeError pc=%08X word=%08X: %s\n", e.pc, e.word, e.what.c_str());
        return 1;
    }
    std::printf("drive-visor: driver completed, %llu instructions\n",
                static_cast<unsigned long long>(cpu.instructionsRetired));
    dumpRods("AFTER");
    const uint32_t cursor = mem.read32(0x00375230);
    std::printf("drive-visor: packet cursor now %08X\n", cursor);
    return 0;
}

int runDecode(int argc, char** argv) {
    std::string storesPath, jsonOut, basePath;
    for (int i = 2; i < argc; i++) {
        if (!std::strcmp(argv[i], "--json") && i + 1 < argc) jsonOut = argv[++i];
        else if (!std::strcmp(argv[i], "--base") && i + 1 < argc) basePath = argv[++i];
        else if (storesPath.empty()) storesPath = argv[i];
    }
    if (storesPath.empty() || jsonOut.empty()) {
        std::printf("usage: eerun --decode <stores.bin> --json <out.json> [--base <image.bin>]\n");
        return 2;
    }
    if (basePath.empty()) basePath = siblingPath(storesPath, "eeMemory.bin");

    // Base RAM image supplies the untouched (never-stored) bytes -- e.g. the
    // GIFtag prefix at the DMA source address, which this run never writes.
    std::vector<uint8_t> ram(EeMemory::kRamSize, 0);
    {
        std::ifstream base(basePath, std::ios::binary);
        if (base) base.read(reinterpret_cast<char*>(ram.data()), ram.size());
        else std::printf("warning: base image '%s' not found; untouched bytes read as zero\n", basePath.c_str());
    }

    // SPR (EE Scratchpad RAM) has no base-image counterpart -- on hardware
    // it powers up unspecified/zeroed, and stores.bin's SPR extents (tagged
    // with their real vaddr, 0x70000000..0x70003FFF, per EeMemory::isSpr)
    // are the only content that ever lands there in this capture.
    std::vector<uint8_t> spr(EeMemory::kSprSize, 0);
    uint32_t sprMaxEnd = 0;

    // Overlay the captured store extents (final RAM content at dump time)
    // on top of the base image, in store order (later extents win on overlap).
    std::ifstream sf(storesPath, std::ios::binary);
    if (!sf) { std::printf("cannot open %s\n", storesPath.c_str()); return 1; }
    size_t extentCount = 0;
    size_t sprExtentCount = 0;
    while (sf) {
        uint32_t addr = 0, len = 0;
        sf.read(reinterpret_cast<char*>(&addr), 4);
        sf.read(reinterpret_cast<char*>(&len), 4);
        if (!sf) break;
        std::vector<uint8_t> bytes(len);
        sf.read(reinterpret_cast<char*>(bytes.data()), len);
        if (!sf) break;
        if (addr >= EeMemory::kSprBase && addr < EeMemory::kSprBase + EeMemory::kSprSize) {
            const uint32_t off = addr - EeMemory::kSprBase;
            const uint32_t end = std::min<uint32_t>(off + len, EeMemory::kSprSize);
            if (end > off) std::memcpy(spr.data() + off, bytes.data(), end - off);
            sprMaxEnd = std::max(sprMaxEnd, end);
            sprExtentCount++;
        } else if (uint64_t(addr) + len <= ram.size()) {
            std::memcpy(ram.data() + addr, bytes.data(), len);
        }
        extentCount++;
    }
    std::printf("decode: %zu store extents applied over base '%s' (%zu in SPR)\n",
                extentCount, basePath.c_str(), sprExtentCount);

    GsCommandStream stream;

    // SPR pass first: sp1-interpreter-runs.md's Task 6 finding is that the
    // GIF-packet-shaped writes (GIFtag words, floats, flat color) landing on
    // the aliased 0x1000xxxx range are actually the real vertex/rect packet
    // being built in scratchpad, invisible before this SPR emulation existed.
    if (sprMaxEnd > 0) {
        std::printf("decode: SPR content %08X..%08X (%u bytes)\n",
                    EeMemory::kSprBase, EeMemory::kSprBase + sprMaxEnd, sprMaxEnd);
        GsDumpParser::decodeGifData(stream, spr.data(), sprMaxEnd);
        std::printf("decode: after SPR pass -> %u draws, %u kicks, %u giftags\n",
                    stream.counts.draws, stream.counts.kicks, stream.counts.giftags);
    } else {
        std::printf("decode: SPR empty (no stores landed in 0x70000000..0x70003FFF)\n");
    }

    // Main-RAM GIF window: the D2-DMA'd state-setup packet (TEST_1/ALPHA_1),
    // decoded second so its register state applies on top of whatever the
    // SPR pass already emitted -- both feed the same decodeState/stream.
    const uint32_t winEnd = std::min<uint32_t>(kGifWindowStart + kGifWindowLen,
                                                static_cast<uint32_t>(ram.size()));
    const uint32_t winLen = winEnd - kGifWindowStart;
    std::printf("decode: GIF window %08X..%08X (%u bytes)\n", kGifWindowStart, winEnd, winLen);

    GsDumpParser::decodeGifData(stream, ram.data() + kGifWindowStart, winLen);
    std::printf("decode: %u draws, %u kicks, %u giftags\n", stream.counts.draws,
                stream.counts.kicks, stream.counts.giftags);

    GsDumpParser::writeJson(stream, jsonOut);
    std::printf("decode: wrote %zu prims -> %s\n", stream.prims.size(), jsonOut.c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    // Unbuffered stdout: this is a diagnostic CLI whose run can die inside
    // interpreted code -- buffered progress lines must not be lost.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc >= 2 && !std::strcmp(argv[1], "--decode"))
        return runDecode(argc, argv);

    if (argc >= 3 && !std::strcmp(argv[2], "--drive-rods"))
        return runDriveRods(argc, argv);

    if (argc >= 3 && !std::strcmp(argv[2], "--drive-visor"))
        return runDriveVisor(argc, argv);

    if (argc < 3) {
        std::printf("usage: eerun <image.bin> <hex-addr> [a0 a1 a2 a3] "
                    "[--dump-stores out.bin] [--trace] [--ready-at addr=val]... "
                    "[--regs-at hexpc] [--max-instr N]\n"
                    "       eerun --decode <stores.bin> --json <out.json> [--base <image.bin>]\n");
        return 2;
    }
    EeMemory mem;
    if (!mem.loadImage(argv[1])) { std::printf("bad image\n"); return 2; }
    const uint32_t entry = std::strtoul(argv[2], nullptr, 16);
    uint64_t args[4] = {};
    int ai = 0;
    std::string dumpPath; bool trace = false;
    uint64_t maxInstrOverride = 0;
    uint32_t regsAtPc = 0; bool haveRegsAt = false;
    for (int i = 3; i < argc; i++) {
        if (!std::strcmp(argv[i], "--dump-stores")) dumpPath = argv[++i];
        else if (!std::strcmp(argv[i], "--trace")) trace = true;
        else if (!std::strcmp(argv[i], "--max-instr") && i + 1 < argc) {
            // Phase 2 task 1 exploration: let the caller raise the retirement
            // budget past the default 200M to distinguish "genuinely unbounded
            // loop" from "slow but finite" when chasing a new wall.
            maxInstrOverride = std::strtoull(argv[++i], nullptr, 10);
        }
        else if (!std::strcmp(argv[i], "--regs-at") && i + 1 < argc) {
            // Phase 2 task 1: exec-watch for a runtime GPR snapshot at a chosen
            // PC (e.g. the Deci2 poll site 0x2719dc), since s1's value there is
            // set dynamically via the call chain and not visible statically.
            regsAtPc = std::strtoul(argv[++i], nullptr, 16);
            haveRegsAt = true;
        }
        else if (!std::strcmp(argv[i], "--ready-at") && i + 1 < argc) {
            // Phase 2 spike 2: stub a single polled memory field so a
            // hardware/interrupt-driven wait loop this bare interpreter
            // cannot otherwise satisfy reads as "ready". "addr=val", both
            // hex, no leading "0x" required (matches the other CLI args'
            // convention below).
            const std::string kv = argv[++i];
            const size_t eq = kv.find('=');
            if (eq == std::string::npos) {
                std::printf("bad --ready-at '%s' (expected addr=val)\n", kv.c_str());
                return 2;
            }
            const uint32_t addr = std::strtoul(kv.substr(0, eq).c_str(), nullptr, 16);
            const uint32_t val = std::strtoul(kv.substr(eq + 1).c_str(), nullptr, 16);
            mem.setReadOverride(addr, val);
        }
        else if (ai < 4) args[ai++] = std::strtoull(argv[i], nullptr, 16);
    }
    mem.storeLogEnabled = true;
    std::vector<MmioAccess> mmio;
    mem.onMmio = [&](const MmioAccess& a) { mmio.push_back(a); };

    EeInterpreter cpu(mem);
    cpu.traceCalls = trace;
    if (maxInstrOverride > 0) cpu.maxInstructions = maxInstrOverride;
    std::deque<uint32_t> lastPcs;
    // wrap step loop manually to keep a PC ring buffer
    cpu.gpr[4].lo = args[0]; cpu.gpr[5].lo = args[1];
    cpu.gpr[6].lo = args[2]; cpu.gpr[7].lo = args[3];
    cpu.gpr[29].lo = EeInterpreter::kDefaultStack;
    cpu.gpr[31].lo = EeInterpreter::kReturnSentinel;
    cpu.pc = entry;
    int regsAtHits = 0;
    try {
        while (cpu.pc != EeInterpreter::kReturnSentinel) {
            lastPcs.push_back(cpu.pc);
            if (lastPcs.size() > 8) lastPcs.pop_front();
            if (haveRegsAt && cpu.pc == regsAtPc && regsAtHits < 5) {
                regsAtHits++;
                std::printf("--regs-at %08X hit #%d (instr %llu):\n", cpu.pc, regsAtHits,
                            (unsigned long long)cpu.instructionsRetired);
                for (int r = 0; r < 32; r++)
                    std::printf("  %-4s ($%-2d) = %016llX\n", kGprNames[r], r,
                                (unsigned long long)cpu.gpr[r].lo);
                std::fflush(stdout);
            }
            cpu.step();
            if (++cpu.instructionsRetired > cpu.maxInstructions)
                throw EeError{cpu.pc, 0, "budget exceeded"};
        }
    } catch (const EeError& e) {
        std::printf("EeError at pc=%08X word=%08X: %s\nrecent pcs:", e.pc, e.word,
                    e.what.c_str());
        for (uint32_t p : lastPcs) std::printf(" %08X", p);
        std::printf("\nv0=%08llX v1=%08llX a0=%08llX a1=%08llX\n",
                    (unsigned long long)cpu.gpr[2].lo, (unsigned long long)cpu.gpr[3].lo,
                    (unsigned long long)cpu.gpr[4].lo, (unsigned long long)cpu.gpr[5].lo);
        std::printf("%llu syscall entries logged\n", (unsigned long long)cpu.syscalls.size());
        if (trace) {
            // Phase 2 task 1: print the call-graph tally even on the error
            // path, so a budget/wall failure still answers "did it ever
            // reach the render helpers before dying" without a second run.
            std::map<uint32_t, int> calls;
            for (uint32_t t : cpu.trace) calls[t]++;
            for (auto& [t, n] : calls) std::printf("call %08X x%d\n", t, n);
        }
        return 1;
    }
    std::printf("retired %llu instructions\n",
                (unsigned long long)cpu.instructionsRetired);
    for (int32_t sc : cpu.syscalls) std::printf("syscall %d\n", sc);
    if (trace) {
        std::map<uint32_t, int> calls;
        for (uint32_t t : cpu.trace) calls[t]++;
        for (auto& [t, n] : calls) std::printf("call %08X x%d\n", t, n);
    }
    for (auto& a : mmio)
        std::printf("MMIO %s %08X size %d val %llX\n", a.isWrite ? "W" : "R",
                    a.addr, a.size, (unsigned long long)a.value);
    // merged store extents
    std::vector<std::pair<uint32_t, uint32_t>> ext;
    for (auto& s : mem.storeLog) {
        // Merge only if the new store is within (or just past) the current extent;
        // storeLog is chronological, not address-sorted, so a lower-bound check is
        // required too -- without it, a distant earlier-address store silently gets
        // swallowed into an unrelated extent instead of starting a new one.
        if (!ext.empty() && s.physAddr >= ext.back().first &&
            s.physAddr <= ext.back().second + 16)
            ext.back().second = std::max(ext.back().second, s.physAddr + s.size);
        else ext.push_back({s.physAddr, s.physAddr + s.size});
    }
    std::printf("%zu store extents:\n", ext.size());
    for (auto& [a, b] : ext) std::printf("  %08X..%08X (%u bytes)\n", a, b, b - a);
    if (!dumpPath.empty()) {
        std::FILE* f = std::fopen(dumpPath.c_str(), "wb");
        for (auto& [a, b] : ext) {
            std::fwrite(&a, 4, 1, f); uint32_t len = b - a; std::fwrite(&len, 4, 1, f);
            // SPR extents are tagged with their real vaddr (0x70000000-range,
            // see EeMemory::isSpr) and live in the separate SPR backing store,
            // not the 32MB RAM buffer -- pick the right source per extent.
            const uint8_t* src = (a >= EeMemory::kSprBase && a < EeMemory::kSprBase + EeMemory::kSprSize)
                                      ? mem.sprData() + (a - EeMemory::kSprBase)
                                      : mem.ram() + a;
            std::fwrite(src, 1, len, f);
        }
        std::fclose(f);
        std::printf("stores dumped -> %s\n", dumpPath.c_str());
    }
    return 0;
}
