#include "ee/EeInterpreter.hpp"
#include "ee/EeMemory.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

using namespace ps2ee;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: eerun <image.bin> <hex-addr> [a0 a1 a2 a3] "
                    "[--dump-stores out.bin] [--trace]\n");
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
