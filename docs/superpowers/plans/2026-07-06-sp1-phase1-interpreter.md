# SP1 Phase 1 — EE Interpreter Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A mini EE (R5900+COP1+COP2-macro) interpreter that executes the original
clock render code from `re/ram/clock/eeMemory.bin`, proves itself against known
functions (Gate A), and runs the per-frame render entry `0x00232618` to completion,
capturing the GS packet stream it emits.

**Architecture:** New pure module `src/ee/` (no Vulkan symbols, like `gs/`).
`EeMemory` owns a writable copy of the 32MB RAM image with EE address translation
and MMIO interception; `EeInterpreter` is a fail-fast switch interpreter with an
in-order store log; a CLI harness `tools/eerun` drives discovery runs. Parser reuse
comes later (Phase 2) once the captured stream exists.

**Tech Stack:** C++23, CMake 3.30+ (existing build), CTest, glm (already vendored),
MSVC on Windows.

## Global Constraints (from spec + CLAUDE.md)

- `src/ee/` must not include any Vulkan header or symbol.
- Unknown opcode = hard error carrying PC + raw word (never guess semantics).
- Every visual/geometry value must come from executed original code or a dump
  measurement — no authored fallbacks.
- English only; zero comments except reverse-engineered GS/EE semantics that need
  explanation. PascalCase module boundaries.
- Commits: `Type(Scope): imperative, ≤72 chars`, scopes from the CLAUDE.md list
  (use `GS` scope for `ee/` until a scope is added), NEVER a Co-Authored-By trailer.
- The RAM image `re/ram/clock/eeMemory.bin` is gitignored input; tests that need it
  must SKIP (return 77 / GTEST_SKIP-equivalent early-exit `return 0` with a printed
  `SKIPPED` marker) when the file is absent, so CI without the image stays green.
- Reference doc for evidence values: `docs/ghidra_analysis/sp0-live-reads.md`
  (0x2738a0 disasm, 0x232618 behavior, packet buffer 0x20297220).

## File Structure

```
src/ee/EeMemory.hpp/.cpp      — 32MB image, address translation, MMIO hooks, store log
src/ee/EeInterpreter.hpp/.cpp — R5900 core: GPRs, COP1, COP2-macro, dispatch, call()
tools/eerun/main.cpp          — CLI: load image, call address, report (discovery harness)
tests/EeMemoryTest.cpp        — translation, image load, MMIO intercept
tests/EeInterpreterTest.cpp   — hand-assembled snippets: ALU/branch/load-store
tests/EeRealCodeTest.cpp      — Gate A: real 0x2738a0/0x2738e8/0x26753c vs C++ oracles
```

---

### Task 1: EeMemory — image, translation, MMIO, store log

**Files:**
- Create: `src/ee/EeMemory.hpp`, `src/ee/EeMemory.cpp`
- Modify: `CMakeLists.txt` (new `ee` static lib + test exe, follow the existing
  `add_library(gs STATIC ...)` / test-loop pattern)
- Test: `tests/EeMemoryTest.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace ps2ee {
  struct MmioAccess { uint32_t addr; uint64_t value; int size; bool isWrite; };
  struct StoreRecord { uint32_t physAddr; uint32_t size; };  // in program order
  class EeMemory {
  public:
      static constexpr uint32_t kRamSize = 32 * 1024 * 1024;
      bool loadImage(const std::string& path);            // false if missing/short
      // EE virtual -> physical: mask 0x1FFFFFFF covers useg low, 0x2/0x3
      // uncached windows, kseg0 0x8..., kseg1 0xA... . phys >= kRamSize = MMIO.
      static uint32_t translate(uint32_t vaddr) { return vaddr & 0x1FFFFFFF; }
      bool isRam(uint32_t vaddr) const { return translate(vaddr) < kRamSize; }
      uint8_t*       ram()       { return m_ram.data(); }
      const uint8_t* ram() const { return m_ram.data(); }
      uint32_t read32(uint32_t vaddr) const;   // RAM only; MMIO read -> onMmio + 0
      uint64_t read64(uint32_t vaddr) const;
      void     read128(uint32_t vaddr, uint32_t out[4]) const;
      void write8 (uint32_t vaddr, uint8_t  v);   // logs to storeLog; MMIO -> onMmio
      void write16(uint32_t vaddr, uint16_t v);
      void write32(uint32_t vaddr, uint32_t v);
      void write64(uint32_t vaddr, uint64_t v);
      void write128(uint32_t vaddr, const uint32_t v[4]);
      std::function<void(const MmioAccess&)> onMmio;   // null = ignore reads, log-free
      std::vector<StoreRecord> storeLog;               // every RAM store, in order
      bool storeLogEnabled = false;
  private:
      std::vector<uint8_t> m_ram;
  };
  }  // namespace ps2ee
  ```
- Consumes: nothing (leaf).

- [ ] **Step 1: Write the failing test**

```cpp
// tests/EeMemoryTest.cpp
#include "ee/EeMemory.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

using ps2ee::EeMemory;

int main() {
    // translation: uncached window and kseg mirrors collapse to physical
    assert(EeMemory::translate(0x20297220u) == 0x00297220u);
    assert(EeMemory::translate(0x80232618u) == 0x00232618u);
    assert(EeMemory::translate(0xA0001000u) == 0x00001000u);
    assert(EeMemory::translate(0x00232618u) == 0x00232618u);
    assert(EeMemory::translate(0x1000A000u) == 0x1000A000u);  // VIF1 FIFO = MMIO

    EeMemory mem;
    assert(!mem.isRam(0x1000A000u));

    // fresh memory (no image): zeroed RAM read/write roundtrip + store log
    assert(mem.loadImage("__no_such_file__") == false);
    mem.storeLogEnabled = true;
    mem.write32(0x00100000u, 0xDEADBEEFu);
    assert(mem.read32(0x00100000u) == 0xDEADBEEFu);
    assert(mem.read32(0x20100000u) == 0xDEADBEEFu);  // uncached mirror sees it
    assert(mem.storeLog.size() == 1 && mem.storeLog[0].physAddr == 0x00100000u
           && mem.storeLog[0].size == 4);

    // MMIO write intercepted, not stored
    bool hit = false;
    mem.onMmio = [&](const ps2ee::MmioAccess& a) {
        hit = a.isWrite && a.addr == 0x1000A000u && a.value == 0x42 && a.size == 4;
    };
    mem.write32(0x1000A000u, 0x42u);
    assert(hit);
    assert(mem.storeLog.size() == 1);

    // real image (skip if absent)
    EeMemory img;
    if (!img.loadImage("re/ram/clock/eeMemory.bin")) {
        std::printf("SKIPPED: re/ram/clock/eeMemory.bin not present\n");
        return 0;
    }
    // first word of sceVu0MulMatrix @0x2738a0 is an lqc2 (opcode 0x36 = LQC2,
    // top 6 bits 110110) — sp0-live-reads.md
    const uint32_t w = img.read32(0x002738A0u);
    assert((w >> 26) == 0x36u);
    std::printf("ee_memory: all assertions passed\n");
    return 0;
}
```

- [ ] **Step 2: Add CMake targets and run test to verify it fails**

In `CMakeLists.txt`, next to `add_library(gs STATIC ...)`:

```cmake
add_library(ee STATIC
    src/ee/EeMemory.cpp
)
target_include_directories(ee PUBLIC src)
target_compile_features(ee PUBLIC cxx_std_23)
```

and register `EeMemoryTest` in the existing test loop (same pattern as the
`clock` tests: executable + `add_test`, working dir = repo root so the relative
`re/ram/...` path resolves).

Run: `cmake -B build && cmake --build build --config Debug --target ee_memory_tests`
Expected: FAIL to compile (`EeMemory.hpp` missing).

- [ ] **Step 3: Implement EeMemory**

```cpp
// src/ee/EeMemory.hpp — exactly the interface block above.
// src/ee/EeMemory.cpp:
#include "ee/EeMemory.hpp"
#include <cstdio>
#include <cstring>

namespace ps2ee {

bool EeMemory::loadImage(const std::string& path) {
    m_ram.assign(kRamSize, 0);
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    const size_t n = std::fread(m_ram.data(), 1, kRamSize, f);
    std::fclose(f);
    return n == kRamSize;
}

uint32_t EeMemory::read32(uint32_t vaddr) const {
    const uint32_t p = translate(vaddr);
    if (p >= kRamSize) { if (onMmio) onMmio({p, 0, 4, false}); return 0; }
    if (m_ram.empty()) return 0;
    uint32_t v; std::memcpy(&v, &m_ram[p], 4); return v;
}
// read64/read128 identical shape (memcpy 8 / 16 bytes).

void EeMemory::write32(uint32_t vaddr, uint32_t v) {
    const uint32_t p = translate(vaddr);
    if (p >= kRamSize) { if (onMmio) onMmio({p, v, 4, true}); return; }
    if (m_ram.empty()) m_ram.assign(kRamSize, 0);
    std::memcpy(&m_ram[p], &v, 4);
    if (storeLogEnabled) storeLog.push_back({p, 4});
}
// write8/16/64/128 identical shape.

}  // namespace ps2ee
```

(Constructor may lazily allocate on first write as shown, or eagerly allocate
zeroed RAM in the constructor — either, as long as the test passes.)

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build -C Debug -R ee_memory --output-on-failure`
Expected: PASS (with the real-image section exercised, since `re/ram/clock/` exists locally).

- [ ] **Step 5: Commit**

```bash
git add src/ee/ tests/EeMemoryTest.cpp CMakeLists.txt
git commit -m "GS(GS): EeMemory - RAM image, EE translation, MMIO, store log"
```

---

### Task 2: EeInterpreter core — registers, dispatch, integer subset

**Files:**
- Create: `src/ee/EeInterpreter.hpp`, `src/ee/EeInterpreter.cpp`
- Modify: `CMakeLists.txt` (add `src/ee/EeInterpreter.cpp` to lib `ee`; new test)
- Test: `tests/EeInterpreterTest.cpp`

**Interfaces:**
- Consumes: `ps2ee::EeMemory` (Task 1 signatures).
- Produces:
  ```cpp
  namespace ps2ee {
  struct U128 { uint64_t lo = 0, hi = 0; };   // GPR; lo holds the 64-bit view
  struct EeError {                             // thrown on any unknown opcode
      uint32_t pc; uint32_t word; std::string what;
  };
  class EeInterpreter {
  public:
      explicit EeInterpreter(EeMemory& mem);
      U128 gpr[32];                 // gpr[0] forced zero after every op
      uint32_t pc = 0;
      uint64_t lo = 0, hi = 0;
      float    fpr[32] = {};        // COP1 single-precision
      uint32_t fcr31 = 0;           // condition bit 23
      float    vf[32][4] = {};      // COP2 vf00..vf31 (vf00 = 0,0,0,1)
      float    vacc[4] = {};        // COP2 ACC
      static constexpr uint32_t kReturnSentinel = 0xDEADBEE0u;
      static constexpr uint32_t kDefaultStack   = 0x01FF8000u;  // top-of-RAM scratch
      // Set a0..a3/sp/ra, run until pc == kReturnSentinel. Returns v0 (gpr[2]).
      uint64_t call(uint32_t addr, uint64_t a0 = 0, uint64_t a1 = 0,
                    uint64_t a2 = 0, uint64_t a3 = 0);
      void step();                  // execute one instruction (handles delay slots)
      uint64_t instructionsRetired = 0;
      uint64_t maxInstructions = 200'000'000;  // runaway guard -> EeError
      bool traceCalls = false;      // log jal/jalr targets to trace
      std::vector<uint32_t> trace;  // call targets in order (when traceCalls)
  };
  }  // namespace ps2ee
  ```
  Register indices: `ra=31, sp=29, a0..a3=4..7, v0=2, gp=28`.

- [ ] **Step 1: Write the failing test — hand-assembled snippets**

```cpp
// tests/EeInterpreterTest.cpp
#include "ee/EeInterpreter.hpp"
#include "ee/EeMemory.hpp"
#include <cassert>
#include <cstdio>

using namespace ps2ee;

static void poke(EeMemory& m, uint32_t addr, std::initializer_list<uint32_t> words) {
    for (uint32_t w : words) { m.write32(addr, w); addr += 4; }
}

int main() {
    EeMemory mem;
    mem.write32(0, 0);  // force RAM allocation
    EeInterpreter cpu(mem);

    // 1) addiu v0, a0, 5 ; jr ra ; nop      (delay slot exercised)
    //    001001 00100 00010 0000000000000101 = 0x24820005
    //    jr ra = 0x03E00008 ; nop = 0
    poke(mem, 0x1000, {0x24820005u, 0x03E00008u, 0u});
    assert(cpu.call(0x1000, 37) == 42);

    // 2) loop: addiu a0,a0,-1 ; bne a0,zero,loop ; addiu v0,v0,1  (branch+slot)
    //    addiu a0,a0,-1 = 0x2484FFFF ; bne a0,zero,-2 = 0x1480FFFE ;
    //    addiu v0,v0,1 = 0x24420001 ; jr ra ; nop
    poke(mem, 0x2000, {0x2484FFFFu, 0x1480FFFEu, 0x24420001u, 0x03E00008u, 0u});
    EeInterpreter cpu2(mem);
    assert(cpu2.call(0x2000, 5) == 5);   // slot executes 5 times

    // 3) lw/sw roundtrip: lw v0, 0(a0) ; sw v0, 4(a0) ; jr ra ; nop
    //    lw = 0x8C820000 ; sw = 0xAC820004
    poke(mem, 0x3000, {0x8C820000u, 0xAC820004u, 0x03E00008u, 0u});
    mem.write32(0x8000, 0xCAFEBABEu);
    EeInterpreter cpu3(mem);
    cpu3.call(0x3000, 0x8000);
    assert(mem.read32(0x8004) == 0xCAFEBABEu);

    // 4) unknown opcode fails fast with pc + word
    poke(mem, 0x4000, {0xFC000000u});
    EeInterpreter cpu4(mem);
    bool threw = false;
    try { cpu4.call(0x4000); } catch (const EeError& e) {
        threw = (e.pc == 0x4000 && e.word == 0xFC000000u);
    }
    assert(threw);

    std::printf("ee_interpreter: all assertions passed\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --config Debug --target ee_interpreter_tests`
Expected: FAIL to compile (`EeInterpreter.hpp` missing).

- [ ] **Step 3: Implement the core**

Dispatch skeleton (complete this shape; each listed op is REQUIRED in this task):

```cpp
// src/ee/EeInterpreter.cpp (structure)
#include "ee/EeInterpreter.hpp"
namespace ps2ee {

static int64_t  s64(uint64_t v) { return int64_t(v); }
static uint64_t sx32(uint32_t v) { return uint64_t(int64_t(int32_t(v))); }

uint64_t EeInterpreter::call(uint32_t addr, uint64_t a0, uint64_t a1,
                             uint64_t a2, uint64_t a3) {
    gpr[4].lo = a0; gpr[5].lo = a1; gpr[6].lo = a2; gpr[7].lo = a3;
    gpr[29].lo = kDefaultStack;
    gpr[31].lo = kReturnSentinel;
    pc = addr;
    while (pc != kReturnSentinel) {
        if (++instructionsRetired > maxInstructions)
            throw EeError{pc, 0, "instruction budget exceeded"};
        step();
    }
    return gpr[2].lo;
}

void EeInterpreter::step() {
    const uint32_t w = memRead32(pc);          // helper over m_mem
    const uint32_t op = w >> 26, rs = (w >> 21) & 31, rt = (w >> 16) & 31,
                   rd = (w >> 11) & 31, sa = (w >> 6) & 31, fn = w & 63;
    const int32_t  simm = int16_t(w & 0xFFFF);
    uint32_t nextPc = pc + 4;                  // branches set m_branchTarget
    // ... switch (op) { ... } — semantics table below
    // delay slots: on taken/untaken branch, execute pc+4 then jump — implement
    // with the classic two-phase loop: compute target, execute slot inline.
    gpr[0] = U128{};
    pc = nextPc;
}
}  // namespace ps2ee
```

Required integer ops for this task, with exact semantics (64-bit GPR `lo` view;
`sx32` = sign-extend the 32-bit result):

| Encoding | Op | Semantics |
|---|---|---|
| op=0 fn=0x00 | sll | `rd = sx32(u32(rt) << sa)` |
| op=0 fn=0x02 | srl | `rd = sx32(u32(rt) >> sa)` |
| op=0 fn=0x03 | sra | `rd = sx32(s32(rt) >> sa)` |
| op=0 fn=0x04/06/07 | sllv/srlv/srav | same, shift = `rs & 31` |
| op=0 fn=0x08 | jr | branch to `rs` (delay slot) |
| op=0 fn=0x09 | jalr | `rd = pc + 8`; branch to `rs` |
| op=0 fn=0x0A/0B | movz/movn | `if (rt==0 / rt!=0) rd = rs` |
| op=0 fn=0x18/1A | mult/div | `lo/hi` from s32×s32 / s32÷s32 (div by 0: lo=hi=0 is fine for this codebase — no div-by-0 observed; assert instead) |
| op=0 fn=0x10/12 | mfhi/mflo | `rd = hi / lo` |
| op=0 fn=0x21/23 | addu/subu | `rd = sx32(u32(rs) ± u32(rt))` |
| op=0 fn=0x24..0x27 | and/or/xor/nor | 64-bit bitwise |
| op=0 fn=0x2A/2B | slt/sltu | `rd = s64(rs)<s64(rt) / u64<u64` |
| op=0 fn=0x2D/2F | daddu/dsubu | 64-bit add/sub |
| op=0 fn=0x38/3A/3B | dsll/dsrl/dsra | 64-bit shifts by sa |
| op=0 fn=0x3C/3E/3F | dsll32/dsrl32/dsra32 | shift by sa+32 |
| op=1 rt=0/1 | bltz/bgez | `s64(rs) < 0 / >= 0` relative branch |
| op=2/3 | j/jal | jump within 256MB page; jal: `ra = pc + 8` |
| op=4/5 | beq/bne | compare full 64-bit GPRs |
| op=6/7 | blez/bgtz | `s64(rs) <= 0 / > 0` |
| op=9 | addiu | `rt = sx32(u32(rs) + simm)` |
| op=0xA/0xB | slti/sltiu | signed / unsigned-of-sign-extended compare |
| op=0xC/0xD/0xE | andi/ori/xori | zero-extended imm16, 64-bit op |
| op=0xF | lui | `rt = sx32(imm << 16)` |
| op=0x14/0x15 | beql/bnel | likely: slot executes ONLY if taken |
| op=0x19 | daddiu | `rt = rs + simm` (64-bit) |
| op=0x1E/0x1F | lq/sq | 128-bit load/store, addr & ~0xF |
| op=0x20..0x27 | lb/lh/lwl*/lw/lbu/lhu/lwr*/lwu | standard MIPS ((*) lwl/lwr may be deferred to fail-fast if unused) |
| op=0x28..0x2B,0x2F | sb/sh/swl*/sw/swr* | standard |
| op=0x37 | ld | 64-bit load |
| op=0x3F | sd | 64-bit store |

Anything not in the table (including COP0, MMI, cache ops): `throw EeError`.

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build -C Debug -R ee_interpreter --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/ee/EeInterpreter.* tests/EeInterpreterTest.cpp CMakeLists.txt
git commit -m "GS(GS): EeInterpreter core - R5900 integer subset, fail-fast"
```

---

### Task 3: COP1 + COP2-macro ops + Gate A (real code vs oracles)

**Files:**
- Modify: `src/ee/EeInterpreter.cpp` (extend dispatch), `CMakeLists.txt` (new test)
- Test: `tests/EeRealCodeTest.cpp`

**Interfaces:**
- Consumes: Task 1+2 signatures. `re/ram/clock/eeMemory.bin` (skip if absent).
- Produces: an interpreter able to run `0x2738a0` (sceVu0MulMatrix), `0x2738e8`
  (sceVu0ApplyMatrix), `0x26753c` (128-bit memcpy) — the three live-verified
  known-semantics functions.

- [ ] **Step 1: Write the failing test (Gate A)**

```cpp
// tests/EeRealCodeTest.cpp
#include "ee/EeInterpreter.hpp"
#include "ee/EeMemory.hpp"
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace ps2ee;

int main() {
    EeMemory mem;
    if (!mem.loadImage("re/ram/clock/eeMemory.bin")) {
        std::printf("SKIPPED: re/ram/clock/eeMemory.bin not present\n");
        return 0;
    }
    constexpr uint32_t kDst = 0x01FE0000, kM = 0x01FE0100, kSrc = 0x01FE0200;

    // sceVu0MulMatrix(dst, m, src): out column j = sum_k m.row(k)*src[k][j]
    float m[16], src[16];
    for (int i = 0; i < 16; i++) { m[i] = 0.25f * i - 1.0f; src[i] = 1.5f * i + 0.5f; }
    for (int i = 0; i < 16; i++) {
        mem.write32(kM + i * 4, std::bit_cast<uint32_t>(m[i]));
        mem.write32(kSrc + i * 4, std::bit_cast<uint32_t>(src[i]));
    }
    EeInterpreter cpu(mem);
    cpu.call(0x002738A0u, kDst, kM, kSrc);
    for (int col = 0; col < 4; col++)
        for (int j = 0; j < 4; j++) {
            float expect = 0.f;
            for (int k = 0; k < 4; k++) expect += m[k * 4 + j] * src[col * 4 + k];
            float got = std::bit_cast<float>(mem.read32(kDst + (col * 4 + j) * 4));
            assert(std::fabs(got - expect) <= 1e-4f * std::max(1.f, std::fabs(expect)));
        }

    // sceVu0ApplyMatrix(out, m, vec): one column of the above
    float v4[4] = {1.f, -2.f, 3.f, 1.f};
    for (int i = 0; i < 4; i++) mem.write32(kSrc + i * 4, std::bit_cast<uint32_t>(v4[i]));
    EeInterpreter cpu2(mem);
    cpu2.call(0x002738E8u, kDst, kM, kSrc);
    for (int j = 0; j < 4; j++) {
        float expect = 0.f;
        for (int k = 0; k < 4; k++) expect += m[k * 4 + j] * v4[k];
        float got = std::bit_cast<float>(mem.read32(kDst + j * 4));
        assert(std::fabs(got - expect) <= 1e-4f * std::max(1.f, std::fabs(expect)));
    }

    // memcpy @0x26753c (lq/sq loop): copy 64 bytes, verify byte-exact
    for (uint32_t i = 0; i < 64; i++) mem.write8(kSrc + i, uint8_t(i * 7 + 3));
    EeInterpreter cpu3(mem);
    cpu3.call(0x0026753Cu, kDst, kSrc, 64);
    for (uint32_t i = 0; i < 64; i++) {
        const uint8_t got = uint8_t(mem.read32(kDst + (i & ~3u)) >> ((i & 3u) * 8));
        assert(got == uint8_t(i * 7 + 3));
    }

    std::printf("ee_realcode (Gate A): all assertions passed\n");
    return 0;
}
```

(The MulMatrix expectation formula transcribes the live disasm: rows of `m` go to
vf04..vf07; each src column vf08 produces `x*row0 + y*row1 + z*row2 + w*row3`.
If the assertion pattern fails on the FIRST run, print both matrices before
touching interpreter code — a convention mismatch [row/col] is a TEST bug;
a NaN/garbage value is an INTERPRETER bug. Fix accordingly.)

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest --test-dir build -C Debug -R ee_realcode --output-on-failure`
Expected: FAIL with `EeError` (COP2 opcodes not implemented) — the failure
message must show pc=0x2738a0 and the lqc2 word.

- [ ] **Step 3: Implement COP1 + COP2-macro ops**

Required ops with exact semantics:

| Encoding | Op | Semantics |
|---|---|---|
| op=0x31 | lwc1 | `fpr[rt] = bit_cast<float>(read32(rs + simm))` |
| op=0x39 | swc1 | store `bit_cast<uint32_t>(fpr[rt])` |
| op=0x11 rs=0/4 | mfc1/mtc1 | GPR↔FPR bit moves (mfc1 sign-extends) |
| op=0x11 rs=0x10 fn=0/1/2/3 | add/sub/mul/div.s | `fd = fs op ft` IEEE single |
| op=0x11 rs=0x10 fn=4/5/6/7 | sqrt/abs/mov/neg.s | standard |
| op=0x11 rs=0x10 fn=0x24 | cvt.w.s | `fd = float->int32 (truncate)` bit-stored |
| op=0x11 rs=0x14 fn=0x20 | cvt.s.w | int32 bits -> float |
| op=0x11 rs=0x10 fn=0x32/3C/3E | c.eq/lt/le.s | set fcr31 bit 23 |
| op=0x11 rs=8 rt=0/1 | bc1f/bc1t | branch on fcr31 bit 23 |
| op=0x36 | lqc2 | `vf[rt] = 4 floats at (rs + simm) & ~0xF` |
| op=0x3E | sqc2 | store vf[rt] |
| op=0x12 (COP2) | vmulax.xyzw | `vacc[i] = vf[fs][i] * vf[ft][bc]` where the macro-mode broadcast field bc and dest mask come from the standard VU encoding: fields `dest=(w>>21)&0xF (x8y4z2w1)`, `ft=(w>>16)&0x1F`, `fs=(w>>11)&0x1F`, `fd=(w>>6)&0x1F`, `bc=w&3`, opcode via `fn=w&0x3F` (0x18+bc = mulabc family via VU0 special2/special3 tables) |
| op=0x12 | vmadday/vmaddaz | `vacc[i] += vf[fs][i] * vf[ft][bc]` |
| op=0x12 | vmaddw | `vf[fd][i] = vacc[i] + vf[fs][i] * vf[ft][3]` |
| op=0x12 | vmulx etc. (if hit) | `vf[fd][i] = vf[fs][i] * vf[ft][bc]` |

Implementation note: decode COP2 macro ops by `w & 0x3F` (special) and the
`(w >> 6) & 0x1F` sub-field for the SPECIAL2 table exactly as PCSX2's
`VU0macro` tables do — when in doubt about an encoding, read
`C:\CodingProjects\Personal\pcsx2\pcsx2\VU0microInterp.cpp` / `COP2 tables`
(reference-only, never bulk-copy). vf00 stays `{0,0,0,1}` after every op.
Anything COP2 not listed: fail-fast `EeError` (implement on demand when a real
run hits it).

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build -C Debug -R ee_realcode --output-on-failure`
Expected: PASS — Gate A is green.

- [ ] **Step 5: Commit**

```bash
git add src/ee/EeInterpreter.cpp tests/EeRealCodeTest.cpp CMakeLists.txt
git commit -m "GS(GS): COP1+COP2 macro ops - Gate A green vs sceVu0 oracles"
```

---

### Task 4: eerun discovery harness — run 0x232618 to completion

**Files:**
- Create: `tools/eerun/main.cpp`
- Modify: `CMakeLists.txt` (add executable `eerun`, links `ee`)
- Test: manual discovery runs (this tool IS the test harness for opcode gaps);
  its own smoke test = Task 3's functions via CLI.

**Interfaces:**
- Consumes: `EeMemory`, `EeInterpreter`, `MmioAccess`, `StoreRecord`.
- Produces: `eerun.exe <image.bin> <hex-addr> [a0 a1 a2 a3] [--dump-stores out.bin]
  [--trace]` — prints: instructions retired, distinct call targets (when
  `--trace`), every MMIO access (addr/size/value/rw), store-extent summary
  (merged ranges), and on EeError: pc, word, and the 8 preceding PCs.

- [ ] **Step 1: Write the tool**

```cpp
// tools/eerun/main.cpp
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
        if (!ext.empty() && s.physAddr <= ext.back().second + 16)
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
```

(Note: the loop above duplicates `call()` intentionally to own the PC ring
buffer; if `EeInterpreter` grows a `preStepHook`, simplify to `call()`.)

- [ ] **Step 2: Build and smoke-test on Gate A functions**

Run: `cmake --build build --config Debug --target eerun`
then `bin\eerun.exe re\ram\clock\eeMemory.bin 2738a0 1FE0000 1FE0100 1FE0200`
Expected: `retired ~40 instructions`, 1 store extent of 64 bytes at 0x01FE0000.

- [ ] **Step 3: The discovery run — 0x232618**

Run: `bin\eerun.exe re\ram\clock\eeMemory.bin 232618 --trace --dump-stores re\ram\clock\stores_232618.bin`

This WILL fail with `EeError` on the first unimplemented opcode. That is the
task's working loop:

1. Read the error (pc, word, recent PCs).
2. Decode the word by hand (opcode table / PCSX2 source as reference).
3. Implement the op in `EeInterpreter.cpp` with a hand-assembled unit test
   appended to `tests/EeInterpreterTest.cpp` (same poke() pattern as Task 2 —
   every new op gets a test).
4. Rebuild, rerun. Repeat until the run completes or hits a genuine wall
   (e.g. reads a hardware register we must model — then extend the MMIO
   handler to return a documented value and LOG it).

Completion criterion for this step: `eerun ... 232618` exits 0, prints MMIO
accesses (expected: VIF1/GIF-related writes — the kick), and dumps store
extents that include the packet buffer near 0x00297220.

IMPORTANT: 0x232618 may expect a0 = a state/context pointer (live regs showed
s0 = 0x20297220 set by a CALLER). If the run crashes dereferencing a0=0, find
the caller's argument values: `docs/ghidra_analysis/sp0-live-reads.md` says
0x232618 builds its rect into 0x20297220 — try a0=20297220 first; if still
wrong, disassemble the call site 0x00233928 in the image (read the words
before the jal) to see what a0/a1 are loaded from, and use those live values.
Log every such decision in `docs/ghidra_analysis/sp1-interpreter-runs.md`
(new doc, status-tagged per doctrine).

- [ ] **Step 4: Record results + commit**

Write `docs/ghidra_analysis/sp1-interpreter-runs.md` with: ops added (list),
MMIO accesses observed (the kick location evidence), store extents, call
graph from --trace. Status-tag every claim.

```bash
git add src/ee/ tests/ tools/eerun/ CMakeLists.txt docs/ghidra_analysis/sp1-interpreter-runs.md
git commit -m "GS(GS): eerun discovery - 0x232618 interpreted to completion"
```

---

### Task 5: Vertex oracle regeneration + GS-stream decode of the captured buffer

**Files:**
- Create: `tools/vdiff/vdiff.mjs`
- Modify: `tools/gsdump/main.cpp` ONLY if `--json` lacks per-draw vertex output
  (check first — memory says it exists)
- Test: oracle-vs-itself identity + perturbation failure (script self-test mode)

**Interfaces:**
- Consumes: `bin/gsdump.exe --json <dump.gs>` JSON (existing); the store dump
  from Task 4.
- Produces: `node tools/vdiff/vdiff.mjs <oracle.json> <candidate.json>` —
  exits 0 when per-pass draw counts match and every vertex XY matches within
  ±1 unit of 12.4 fixed point (i.e. |Δ| ≤ 1 on the raw 12.4 ints), colors/UVs
  exact; prints the first 10 mismatches otherwise. `--self-test` runs the
  identity + perturbation checks and needs no inputs.

- [ ] **Step 1: Regenerate the oracle**

Run: `bin\gsdump.exe --json "C:\Users\dell04\Documents\PCSX2\snaps\clock_sw.gs" > re\oracle\clock_sw_prims.json`
(create `re\oracle\`, gitignored like `re\ram\`). Verify draw count 3948 in the
JSON (the [DUMP-MEASURED] clock number).

- [ ] **Step 2: Write vdiff.mjs with self-test**

```js
// tools/vdiff/vdiff.mjs — per-pass vertex diff between two gsdump --json files.
import { readFileSync } from "node:fs";

function loadDraws(path) {
  const j = JSON.parse(readFileSync(path, "utf8"));
  return j.draws ?? j; // tolerate either a wrapper object or a bare array
}
function diff(oracle, cand) {
  const errs = [];
  if (oracle.length !== cand.length)
    errs.push(`draw count ${cand.length} != oracle ${oracle.length}`);
  const n = Math.min(oracle.length, cand.length);
  for (let i = 0; i < n && errs.length < 10; i++) {
    const o = oracle[i], c = cand[i];
    if (o.prim !== c.prim) { errs.push(`draw ${i}: prim ${c.prim} != ${o.prim}`); continue; }
    const ov = o.verts ?? [], cv = c.verts ?? [];
    if (ov.length !== cv.length) { errs.push(`draw ${i}: ${cv.length} verts != ${ov.length}`); continue; }
    for (let k = 0; k < ov.length && errs.length < 10; k++) {
      if (Math.abs(ov[k].x - cv[k].x) > 1 || Math.abs(ov[k].y - cv[k].y) > 1)
        errs.push(`draw ${i} v${k}: xy (${cv[k].x},${cv[k].y}) != (${ov[k].x},${ov[k].y})`);
      if ((ov[k].rgba ?? 0) !== (cv[k].rgba ?? 0))
        errs.push(`draw ${i} v${k}: rgba ${cv[k].rgba} != ${ov[k].rgba}`);
    }
  }
  return errs;
}
if (process.argv[2] === "--self-test") {
  const a = [{ prim: 4, verts: [{ x: 100, y: 200, rgba: 0xff00ff00 }] }];
  const b = JSON.parse(JSON.stringify(a));
  if (diff(a, b).length !== 0) { console.error("identity failed"); process.exit(1); }
  b[0].verts[0].x += 5;
  if (diff(a, b).length === 0) { console.error("perturbation not caught"); process.exit(1); }
  console.log("vdiff self-test OK"); process.exit(0);
}
const errs = diff(loadDraws(process.argv[2]), loadDraws(process.argv[3]));
if (errs.length) { errs.forEach((e) => console.error(e)); process.exit(1); }
console.log("vdiff: MATCH");
```

Adjust the field names (`draws`/`verts`/`x`/`y`/`rgba`/`prim`) to the ACTUAL
`gsdump --json` schema — read a few lines of the generated oracle first and
transcribe the real keys; the diff logic stays as written.

- [ ] **Step 3: Run self-test**

Run: `node tools/vdiff/vdiff.mjs --self-test`
Expected: `vdiff self-test OK`.

- [ ] **Step 4: Commit**

```bash
git add tools/vdiff/vdiff.mjs .gitignore
git commit -m "GS(CI): vdiff vertex-diff harness + clock_sw oracle regen"
```

---

### Task 6: Decode the interpreted packet stream to GsPrimitives (phase gate)

**Files:**
- Create: `tools/eerun/DecodeStores.cpp` (or extend `tools/eerun/main.cpp` with
  `--decode` if smaller), reusing `gsdump_lib`
- Modify: `CMakeLists.txt` (link `gsdump_lib` + `gs` into eerun)
- Test: end-to-end — decoded draws from the Task 4 store dump vs the oracle
  subset via vdiff

**Interfaces:**
- Consumes: Task 4's store-dump format (`[addr u32][len u32][bytes]`*), the
  `GsDumpParser`/`GsCommandStream` API, `vdiff.mjs`.
- Produces: `eerun --decode <stores.bin> --json out.json` — walks the packet
  buffer content in STORE ORDER, feeds the GIF-tag stream through the same
  register/vertex-kick state machine as the dump parser, and writes the same
  JSON schema `gsdump --json` writes.

- [ ] **Step 1: Expose the parser's GIF-payload decode**

`GsDumpParser::parse` today consumes a whole `.gs` file. Extract its inner
GIF-data machine into a public entry (added to `tools/gsdump/GsDumpParser.hpp`):

```cpp
    // Decodes raw GIF packet payload bytes (a sequence of GIFtags + register
    // data, as found in dump TRANSFER packets) into `stream`, continuing from
    // stream's current register state. Used by the SP1 interpreter capture.
    static void decodeGifData(GsCommandStream& stream,
                              const uint8_t* data, size_t size);
```

Refactor `parse` to call `decodeGifData` for its TRANSFER packets (behavior
unchanged — the existing `gsdump --verify` invariants over `clock_viewer.gs`
are the regression test).

- [ ] **Step 2: Verify no parser regression**

Run: `bin\gsdump.exe --verify "C:\Users\dell04\Documents\PCSX2\snaps\clock_viewer.gs"`
Expected: same PASS output as before the refactor.

- [ ] **Step 3: Implement --decode and produce candidate JSON**

In eerun: load the store dump, locate the extents inside the packet buffer
region (phys 0x00290000..0x002A0000 window — refine from Task 4's actual
extents), concatenate their bytes in store order, call `decodeGifData`, write
draws as JSON (same emitter code path as `gsdump --json`; factor that JSON
writer into `gsdump_lib` if it lives in `tools/gsdump/main.cpp`).

- [ ] **Step 4: The phase gate — compare against the oracle**

Run:
```
bin\eerun.exe re\ram\clock\eeMemory.bin 232618 --dump-stores re\ram\clock\stores_232618.bin
bin\eerun.exe --decode re\ram\clock\stores_232618.bin --json re\oracle\cand_232618.json
node tools\vdiff\vdiff.mjs re\oracle\clock_sw_prims.json re\oracle\cand_232618.json
```

Expected at THIS phase: NOT a full match (0x232618 alone emits a subset — the
rect + whatever its callees add). Success criterion: the candidate draws
decode cleanly (valid GIFtags, plausible 12.4 coords in-screen) and each
candidate draw finds a matching draw in the oracle (subset match — add
`--subset` mode to vdiff: every candidate draw must match SOME oracle draw
at the same prim type within tolerance; report match count).
Record match count + first mismatches in
`docs/ghidra_analysis/sp1-interpreter-runs.md` [DUMP-MEASURED].

- [ ] **Step 5: Commit**

```bash
git add tools/eerun/ tools/gsdump/ tools/vdiff/ CMakeLists.txt docs/ghidra_analysis/sp1-interpreter-runs.md
git commit -m "GS(GS): decode interpreted GS stream - subset match vs oracle"
```

---

## After this plan

Phase 1 ends with: interpreter proven (Gate A), 0x232618 running, the kick
located (MMIO log), the emitted stream decoding to real draws, and a subset
vertex-match against the dump. **Phase 2 (full rods, Gate B)** — walking the
caller loop 0x233928, populating ClockState into the original addresses, all
rod passes matching — gets its own plan, written from the discovery doc this
plan produces (`sp1-interpreter-runs.md`), because the caller's argument
protocol and the state block layout are unknowns until Task 4/6 reveal them.

## Self-review notes

- Spec coverage: Gate A = Task 3; discovery of kick/DMA = Task 4; vertex-diff
  harness = Task 5; parser reuse = Task 6. Gates B/C/D deferred to Phase 2+
  plans by design (§ After this plan).
- Types consistent: `EeMemory`/`EeInterpreter`/`EeError`/`U128` used with the
  same signatures across Tasks 1-4; store-dump binary format defined in Task 4
  and consumed in Task 6.
- Known judgment calls the implementer must log, not guess: a0 protocol of
  0x232618 (Task 4 Step 3 gives the procedure), `gsdump --json` schema keys
  (Task 5 Step 2), packet-buffer window (Task 6 Step 3).
