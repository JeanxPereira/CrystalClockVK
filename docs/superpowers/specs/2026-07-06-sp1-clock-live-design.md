# SP1 — Clock Live: execute the original render code, interpret first, port after

**Date:** 2026-07-06 · **Status:** APPROVED (user, this session) · **Parent:**
`2026-07-05-osdsys-1to1-master-strategy-design.md` (SP1 row). SP0 is gated
(docs tagged, gate green on 5 dumps, 6 per-screen RAM images captured, branch
reviewed and fixed).

## 1. Goal

The crystal clock rendering live in the app, marking real wall-clock time,
visually 1:1 with OSDSYS: 12 crystal rods (hour lit, min/sec fill), light
spots with after-image trails, swirl wireframe, PSMT4 date/time text.

"1:1" is numeric: vertex-diff against the dump oracle per pass, then
pixel-diff at or below the proven replay level (clock_viewer 7.9%).

## 2. Decisions (user, this session)

1. **Strategy: hybrid.** A mini EE interpreter executes the ORIGINAL MIPS
   bytes from the captured clock RAM image inside the app first (clock runs
   1:1 early, and every run is a trace oracle); functions are then ported to
   C++ one at a time under vertex-diff, replacing interpreted code piecewise.
2. **Interpreter boundary: render pipeline only.** Execution starts at the
   per-frame render entry (`0x00232618` [LIVE-VERIFIED], `draw_crystal_rod
   @0x232e38`, tessellation, emitter family `0x0022FD00`). Clock STATE (time,
   angles, rod structs) is prepared by our C++ before each call. No syscalls,
   no threads, no RTC emulation.
3. **Order: interpreter core → rods → spots/swirl/text → real time + trails.**
   Highest risk (the rod tessellation wall) dies first.
4. **Trails are in-scope for the SP1 gate.** Cross-frame framebuffer
   persistence is an explicit deliverable (the current renderer re-seeds VRAM
   every frame; SP1 keeps it alive between frames).

## 3. Architecture

New module `src/ee/` — pure C++, no Vulkan symbols (same rule as `gs/`).

Per frame:

```
ClockState (C++: real time → angles/indices)
  → write state into the original addresses (0x2C8000+ block, 0x375250 rod
    records, matrix scratch 0x29bd10+)
  → EeInterpreter::call(0x232618, ...)      ← original code executes
  → GS packets appear in the uncached buffer 0x20297220 (phys 0x297220)
  → GifCapture walks the DMA chain / GIF tags → raw GS stream bytes
  → the EXISTING gsdump parser state machine consumes them → GsPrimitive list
  → GsRenderer (the proven 5–8% replay renderer) draws the frame
```

The interpreter's output has the same shape as a `.gs` dump's draw content;
the entire `GsPrimitive → pixel` path already exists and is already gated.
SP1 changes the SOURCE of draws (static dump → executed original code at real
time), not the style layer.

## 4. Components

- **`ee/EeMemory`** — owns a writable copy of `re/ram/clock/eeMemory.bin`
  (32MB) per run; address translation (kseg / uncached `0x2xxxxxxx` mirrors →
  physical); registered MMIO watch-ranges that intercept and log any access
  outside RAM (DMA registers, VIF1 FIFO `0x1000A000`) — this is also how the
  unlocated GS kick gets found.
- **`ee/EeInterpreter`** — R5900 subset + COP2 macro-mode (`lqc2/sqc2/
  vmulax/vmadday/vmaddaz/vmaddw...` as used by the sceVu0 lib), 128-bit GPRs
  (`lq/sq`). Opcodes implemented ON DEMAND; unknown opcode = hard error with
  PC + bytes (never guess semantics). API: `call(addr, a0..a3)` runs until
  the matching `jr ra` returns. Optional trace mode logs PCs, calls, and
  loads/stores — the porting oracle.
- **`ee/GifCapture`** — reads the DMA-chain the `0x22FD00` family builds
  (linked list via `(a0)`/`+0x14`, GIF-tag words, 16-byte templates from
  `0x296dd0/e0`) and emits the equivalent flat GS stream.
- **Parser reuse** — `gsdump_lib`'s GIF/vertex-kick state machine parses that
  stream to `GsPrimitive`. No new decode code.
- **`clock/ClockState`** (existing, evidence-grounded) — time→visual mapping;
  gains a `writeTo(EeMemory&)` that populates the original state addresses.
- **Vertex-diff harness** — compares generated `GsPrimitive`s vs
  `prims_sw.json` (dump oracle at 17:57:50): per-pass draw counts equal,
  vertex coords within ±1 LSB in 12.4 fixed point, colors/UVs exact.

## 5. Porting loop (continuous, leaves → roots)

1. Pick the innermost unported function the trace shows hot.
2. Capture real input/output pairs from interpreter runs → CTest fixture.
3. Port to C++ (in `clock/` if pure math, `ee/ports/` if pipeline glue).
4. Hook the interpreter's address table so calls to that address run the C++
   version; vertex-diff must stay green.

First port is `sceVu0MulMatrix/ApplyMatrix @0x2738a0/e8` (known semantics —
doubles as the interpreter's correctness proof against glm). Porting
EVERYTHING is not an SP1 gate; the interpreter may still cover unported code
at SP1's end. Full replacement completes during SP4 (modernization).

## 6. Gates

| Gate | Content | Criterion |
|------|---------|-----------|
| A | Interpreter core | Interpreted sceVu0MulMatrix/ApplyMatrix == glm on fixtures (CTest) |
| B | Rods | Vertex-diff of rod passes vs prims_sw.json, state frozen at 17:57:50 |
| C | Full frame | All passes (rods+spots+swirl+text); pixel-diff vs GSRunner at the frozen time ≤ the matching dump's replay level (clock_sw 6.8%, the 17:57:50 capture) |
| D | Live + trails | Persistent VRAM across frames (no per-frame re-seed), real wall-clock time, trails present; pixel-diff on a live-captured reference |

Gate D's VRAM persistence subsumes the per-frame re-seed inefficiency flagged
in the SP0 branch review (GsRenderer::record re-copies 2.85M px/frame).

## 7. Risks

- **Unimplemented rare opcode** — fail-fast + implement on demand; COP2 macro
  set used by sceVu0 is small and already disassembled live.
- **Per-frame fields in the 0x2C8000+ state block we can't regenerate** —
  start with the EXACT captured state (must reproduce the dump frame
  bit-for-bit modulo AA), only then vary time; memory_diff between the clock
  and clock_viewer RAM images narrows which fields matter.
- **DMA/GIF kick still unlocated** — MMIO interception in EeMemory finds it
  the first time interpreted code fires it.
- **Heap-shift** — addresses in the RAM image are self-consistent (the image
  IS the heap); only cross-checking against a NEW live session needs care.

## 8. Not the plan

- No authored geometry/shaders; every vertex comes from executed original
  code or a measured dump.
- No full-system emulation (no IOP, no threads, no syscalls).
- No trusting Ghidra static decompile without live-disasm cross-check.
