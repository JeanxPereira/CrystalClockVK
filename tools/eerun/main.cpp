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

    // Overlay the captured store extents (final RAM content at dump time)
    // on top of the base image, in store order (later extents win on overlap).
    std::ifstream sf(storesPath, std::ios::binary);
    if (!sf) { std::printf("cannot open %s\n", storesPath.c_str()); return 1; }
    size_t extentCount = 0;
    while (sf) {
        uint32_t addr = 0, len = 0;
        sf.read(reinterpret_cast<char*>(&addr), 4);
        sf.read(reinterpret_cast<char*>(&len), 4);
        if (!sf) break;
        std::vector<uint8_t> bytes(len);
        sf.read(reinterpret_cast<char*>(bytes.data()), len);
        if (!sf) break;
        if (uint64_t(addr) + len <= ram.size())
            std::memcpy(ram.data() + addr, bytes.data(), len);
        extentCount++;
    }
    std::printf("decode: %zu store extents applied over base '%s'\n", extentCount, basePath.c_str());

    const uint32_t winEnd = std::min<uint32_t>(kGifWindowStart + kGifWindowLen,
                                                static_cast<uint32_t>(ram.size()));
    const uint32_t winLen = winEnd - kGifWindowStart;
    std::printf("decode: GIF window %08X..%08X (%u bytes)\n", kGifWindowStart, winEnd, winLen);

    GsCommandStream stream;
    GsDumpParser::decodeGifData(stream, ram.data() + kGifWindowStart, winLen);
    std::printf("decode: %u draws, %u kicks, %u giftags\n", stream.counts.draws,
                stream.counts.kicks, stream.counts.giftags);

    GsDumpParser::writeJson(stream, jsonOut);
    std::printf("decode: wrote %zu prims -> %s\n", stream.prims.size(), jsonOut.c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && !std::strcmp(argv[1], "--decode"))
        return runDecode(argc, argv);

    if (argc < 3) {
        std::printf("usage: eerun <image.bin> <hex-addr> [a0 a1 a2 a3] "
                    "[--dump-stores out.bin] [--trace]\n"
                    "       eerun --decode <stores.bin> --json <out.json> [--base <image.bin>]\n");
        return 2;
    }
    EeMemory mem;
    if (!mem.loadImage(argv[1])) { std::printf("bad image\n"); return 2; }
    const uint32_t entry = std::strtoul(argv[2], nullptr, 16);
    uint64_t args[4] = {};
    int ai = 0;
    std::string dumpPath; bool trace = false;
    for (int i = 3; i < argc; i++) {
        if (!std::strcmp(argv[i], "--dump-stores")) dumpPath = argv[++i];
        else if (!std::strcmp(argv[i], "--trace")) trace = true;
        else if (ai < 4) args[ai++] = std::strtoull(argv[i], nullptr, 16);
    }
    mem.storeLogEnabled = true;
    std::vector<MmioAccess> mmio;
    mem.onMmio = [&](const MmioAccess& a) { mmio.push_back(a); };

    EeInterpreter cpu(mem);
    cpu.traceCalls = trace;
    std::deque<uint32_t> lastPcs;
    // wrap step loop manually to keep a PC ring buffer
    cpu.gpr[4].lo = args[0]; cpu.gpr[5].lo = args[1];
    cpu.gpr[6].lo = args[2]; cpu.gpr[7].lo = args[3];
    cpu.gpr[29].lo = EeInterpreter::kDefaultStack;
    cpu.gpr[31].lo = EeInterpreter::kReturnSentinel;
    cpu.pc = entry;
    try {
        while (cpu.pc != EeInterpreter::kReturnSentinel) {
            lastPcs.push_back(cpu.pc);
            if (lastPcs.size() > 8) lastPcs.pop_front();
            cpu.step();
            if (++cpu.instructionsRetired > cpu.maxInstructions)
                throw EeError{cpu.pc, 0, "budget exceeded"};
        }
    } catch (const EeError& e) {
        std::printf("EeError at pc=%08X word=%08X: %s\nrecent pcs:", e.pc, e.word,
                    e.what.c_str());
        for (uint32_t p : lastPcs) std::printf(" %08X", p);
        std::printf("\n");
        return 1;
    }
    std::printf("retired %llu instructions\n",
                (unsigned long long)cpu.instructionsRetired);
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
            std::fwrite(mem.ram() + a, 1, len, f);
        }
        std::fclose(f);
        std::printf("stores dumped -> %s\n", dumpPath.c_str());
    }
    return 0;
}
