#pragma once

#include "ee/EeMemory.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace ps2ee {

struct U128 {
    uint64_t lo = 0, hi = 0;
};

struct EeError {
    uint32_t pc;
    uint32_t word;
    std::string what;
};

class EeInterpreter {
public:
    explicit EeInterpreter(EeMemory& mem);

    U128 gpr[32] = {};
    uint32_t pc = 0;
    uint64_t lo = 0, hi = 0;
    float fpr[32] = {};
    uint32_t fcr31 = 0;
    float vf[32][4] = {};
    float vacc[4] = {};
    float vq = 0.0f;  // VU0 Q register (macro-mode DIV/SQRT/RSQRT result)
    uint32_t cop0[32] = {};  // COP0 (system control) register file: plain storage,
                             // no MMU/interrupt/exception semantics modeled

    static constexpr uint32_t kReturnSentinel = 0xDEADBEE0u;
    static constexpr uint32_t kDefaultStack = 0x01FF8000u;

    uint64_t call(uint32_t addr, uint64_t a0 = 0, uint64_t a1 = 0,
                  uint64_t a2 = 0, uint64_t a3 = 0);
    void step();

    uint64_t instructionsRetired = 0;
    uint64_t maxInstructions = 200'000'000;
    bool traceCalls = false;
    std::vector<uint32_t> trace;
    std::vector<int32_t> syscalls;  // BIOS syscall numbers observed (from $v1 at each `syscall`)

private:
    EeMemory& m_mem;

    void executeOne(uint32_t word, uint32_t atPc);
};

}  // namespace ps2ee
