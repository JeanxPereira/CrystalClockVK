# SP1 Phase 2 — Direct Rod Render to a Real Subset Match (Milestone A) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Drive the crystal-clock rod render directly (bypassing the Deci2-blocked
`0x233928`), execute the ORIGINAL packetize/emit code so the rods become real GS wire
packets, and land a genuine vertex `--subset` match against the dump oracle — then feed
those packets into the existing renderer so the rods are VISIBLE (Milestone A).

**Architecture:** Extend the `eerun --drive-rods` harness (Phase 1) which already loops the
rod array `0x00375250` and calls `draw_crystal_rod` (`0x00232e38`). Phase 1 proved
`draw_crystal_rod` writes a correct-but-internal STAGING record (8-byte header, misread as a
GIFtag). This phase adds the original packetize step (`0x230518`/`0x22FD00` family that ran
clean in Phase 1's `0x232618` path) so the staging becomes a real wire GIF packet, decodes it
with the existing `GsDumpParser::decodeGifData`, and vertex-diffs it against
`re/oracle/clock_sw_prims.json`. No re-derived formats: the wire packet comes from executed
original code (project doctrine: execute the original, never re-invent).

**Tech Stack:** C++23, CMake 3.30+, CTest, the `ee`/`gsdump_lib`/`gs`/`gsvk`/`app` libs, MSVC.

## Global Constraints (from the SP1 spec + CLAUDE.md)

- Design basis: `docs/superpowers/specs/2026-07-06-sp1-clock-live-design.md` (Gate B row) +
  the fully-mapped contract `docs/ghidra_analysis/sp1-interpreter-runs.md` §"Phase 2 — per-rod
  render contract" and its detail file `.superpowers/sdd/phase2-render-contract.md`.
- `src/ee/` and `gs/`/`gsdump_lib` stay Vulkan-free.
- **Read-from-evidence, never invent.** A partial/zero subset match reported honestly is a
  SUCCESS; a faked "rods render" is a failure (the invented-clock incident burned trust).
- Every new opcode gets a discriminating unit test (recall the VMUL broadcast-vs-per-lane
  typo). Verify VU/MMI/COP semantics against `C:\CodingProjects\Personal\pcsx2\pcsx2\`
  (reference only, never bulk-copy).
- Run test exes via `timeout 60 ./bin/<exe>.exe` (Git Bash); a failing MSVC Debug assert pops
  a modal dialog that hangs ctest. Full suite: `timeout 120 ctest --test-dir build -C Debug`;
  must stay green (currently 17/17, more with new tests).
- English only; comments only for EE/GS-semantics; commit `Type(Scope): imperative ≤72 chars`
  (scopes incl. GS/Ee/App/CI), NEVER a Co-Authored-By trailer.
- `re/oracle/` + `stores_*.bin` are gitignored (like `re/ram/`).
- Interpreter facts you can rely on: `EeInterpreter::call(addr,a0..a3)` (integer args; set FPU
  args by writing `cpu.fpr[12]/fpr[13]` before the call), fail-fast on unknown opcode,
  per-call instruction budget (reset each call). `EeMemory` models 32MB RAM + 16KB SPR at
  `0x70000000` + `setReadOverride` + `storeLog`/`sprData()`. `EeError{pc,word,what}` on gaps.

## Known addresses (all [DUMP-MEASURED], from the contract doc)

- Rod array `0x00375250`, stride `0x160`, count `*(ctx+4)`, per-rod skip flag `+0x150`.
- Context `ctx = 0x00296AB0`; RGBA color at `ctx+0xa0` (=`0x00296B50`); jitter floats at
  `ctx+0xb0`/`+0xb4`; render gate `*(ctx+0x6c)`.
- Packet-context struct `0x00375230`: `+0x00` = write cursor (into SPR), `+0x04` = SPR base.
- Packet-cursor init fn `0x22f720`; per-rod emitter `draw_crystal_rod = 0x00232e38` (leaf);
  transform-prep `0x232da0`; packetize/stream helpers called between passes: `0x230518`,
  `0x235350`, `0x230fe8`, `0x2324e8`; DMA/GIF emit family `0x0022FD00`
  (`0x22F720/FB28/FBE8/F7F8`).

## File Structure

```
tools/eerun/main.cpp          — extend the --drive-rods loop (passes + packetize + wire capture)
src/ee/EeInterpreter.cpp/.hpp — any new opcodes the packetize path hits (on demand, tested)
tests/EeInterpreterTest.cpp   — a discriminating test per new opcode
src/app/GsScene.cpp/.hpp      — load a candidate GsCommandStream from decoded rod packets
src/main.cpp                  — a --drive-clock path: render the driven rods via GsRenderer
docs/ghidra_analysis/sp1-interpreter-runs.md — Phase 2 findings, status-tagged
```

---

### Task 1: Characterize the staging record vs. the wire packet (discovery)

Decide the concrete route from `draw_crystal_rod`'s output to decodable GsPrimitives:
does running the original packetize helpers produce a wire GIF packet, or is the staging
already near-wire? This is a discovery task (procedure + decision, not fixed code), like
Phase 1's Task 4.

**Files:** none changed (analysis only); record findings in
`docs/ghidra_analysis/sp1-interpreter-runs.md` (§"Phase 2 task 1 — staging vs wire").

**Interfaces:**
- Consumes: `bin/eerun.exe --drive-rods` (Phase 1), `EeMemory::sprData()`, the contract doc.
- Produces: a documented DECISION — "run packetize fn X to get the wire packet at buffer Y"
  OR "the staging at SPR offset Z is directly decodable with framing W" — plus the exact
  bytes evidence. Task 2 consumes this decision.

- [ ] **Step 1: Dump what `draw_crystal_rod` actually wrote**

Run: `bin\eerun.exe re\ram\clock\eeMemory.bin --drive-rods --json re\oracle\cand_rods.json`
then hex-dump the SPR region it filled. Add a temporary `--dump-spr <file>` to eerun if not
present (write `sprData()[0 .. finalCursor-0x70000000]` to a file), or read it via a small
node/python script over a `--dump-stores` output. Capture the first 128 bytes.

- [ ] **Step 2: Compare against the two reference framings**

Compare the dumped bytes against (a) a real GIF PACKED packet header from the oracle dump
(a GIFtag is 16 bytes: NLOOP/EOP/PRE/PRIM/FLG/NREG in the low qword, REGS descriptor in the
high qword — see `tools/gsdump/GsDumpParser.cpp` `decodeGifData`), and (b) the staging layout
in the contract doc §2 (8-byte header `0x54`/`0x100`, then per-vertex packed words). State
which it matches. The Phase 1 finding is that decode read `nloop=84 (0x54)` — i.e. the 8-byte
header is NOT a 16-byte GIFtag.

- [ ] **Step 3: Trace the packetize consumer**

In `0x00233928`'s body, between the per-rod loops, it calls `0x230518` / `0x235350` /
`0x230fe8` / `0x2324e8`. Disassemble these (Capstone over the RAM image, phys = vaddr &
0x1FFFFFFF — the contract doc shows the method) to find which one READS `0x00375230`'s cursor
/ the SPR staging and WRITES a 16-byte-GIFtag wire packet to a DMA buffer (candidate: the
`0x20297220` packet buffer from Phase 1's `0x232618` path, or a fresh DMA-chain buffer built
by `0x22FD00`). Identify: the packetize entry fn, its args, and the destination buffer
address of the wire packet.

- [ ] **Step 4: Record the decision + commit the doc**

Write the finding: the exact packetize call to make (fn + args) and where the wire packet
lands, OR (if the staging is directly decodable) the exact offset + framing. Status-tag
[DUMP-MEASURED]/[HYPOTHESIS]. If a genuine wall appears (packetize needs unmodeled state),
document it precisely as the Task-2 starting point.

```bash
git add docs/ghidra_analysis/sp1-interpreter-runs.md
git commit -m "Docs(Project): characterize rod staging vs wire packet route"
```

---

### Task 2: Emit real wire packets for one rod + first subset measurement

Implement Task 1's decision so ONE rod produces decodable GsPrimitives, and measure the
first honest `--subset` result.

**Files:**
- Modify: `tools/eerun/main.cpp` (extend the `--drive-rods` loop), `CMakeLists.txt` only if a
  new link is needed (eerun already links `ee gsdump_lib gs`).
- Modify: `src/ee/EeInterpreter.cpp` + `tests/EeInterpreterTest.cpp` ONLY if the packetize path
  hits an unimplemented opcode (implement on demand, one discriminating test each).

**Interfaces:**
- Consumes: Task 1's packetize decision; `EeInterpreter::call`, `cpu.fpr[]`, `EeMemory`
  (`sprData()`, RAM reads for the wire-buffer), `GsDumpParser::decodeGifData`/`writeJson`,
  `tools/vdiff/vdiff.mjs --subset`.
- Produces: `bin/eerun.exe re/ram/clock/eeMemory.bin --drive-rods --json <out>` emitting
  decodable rod draws; the wire packet captured from the buffer Task 1 identified (not the
  raw staging). Task 3 relies on the same loop structure to add passes.

- [ ] **Step 1: Add the packetize call(s) to the per-rod driver**

Per Task 1's decision, after the `draw_crystal_rod` call(s) for a rod (or after the rod loop,
matching the original's between-loop placement), `call()` the packetize fn with its mapped
args so the staging becomes a wire packet. If the original places packetize once per pass
group rather than per rod, mirror that. Show the exact added `call(...)` lines. Read the wire
packet from the destination buffer Task 1 found (RAM or SPR) instead of the raw staging.

- [ ] **Step 2: Iterate any opcode gaps (discovery loop)**

Run: `timeout 90 bin\eerun.exe re\ram\clock\eeMemory.bin --drive-rods --json re\oracle\cand_rods.json`
If it throws `EeError` on an unimplemented opcode: decode the word, implement the op in
`src/ee/EeInterpreter.cpp`, add a discriminating hand-assembled test to
`tests/EeInterpreterTest.cpp` (poke() pattern; a wrong impl must fail it), rebuild, rerun.
Repeat until the run completes and writes candidate JSON. Keep `timeout 120 ctest` at 17/17+.

- [ ] **Step 3: Decode + first subset measurement**

Run: `node tools\vdiff\vdiff.mjs --subset re\oracle\clock_sw_prims.json re\oracle\cand_rods.json`
Report the REAL count (e.g. "N candidate rod draws, M matched oracle prim-type-4 draws within
±1 raw-12.4 tolerance, exact color"). It may be partial (only 1-2 of the 7 passes implemented).
That is fine — record it honestly.

- [ ] **Step 4: Record + commit**

Write the result (draws decoded, subset count, first mismatches) to
`docs/ghidra_analysis/sp1-interpreter-runs.md` [DUMP-MEASURED].

```bash
git add tools/eerun/main.cpp src/ee/ tests/EeInterpreterTest.cpp docs/ghidra_analysis/sp1-interpreter-runs.md
git commit -m "GS(Ee): emit real rod wire packets, first subset measurement"
```

---

### Task 3: Add the remaining per-rod passes until the rod set matches

Bring the candidate up to a strong subset match by adding the passes the contract's §1 table
lists (transform-prep `0x232da0`, the second `0x232e38` variant, `0x233328`/`0x2333b8`,
`0x232f80`), measuring after each so a regressing pass is caught immediately.

**Files:** Modify `tools/eerun/main.cpp` (add passes to the loop), plus
`src/ee/EeInterpreter.cpp`/`tests/EeInterpreterTest.cpp` for any opcode gaps.

**Interfaces:**
- Consumes: Task 2's driver loop + wire capture.
- Produces: a candidate JSON whose rod draws subset-match the oracle at a recorded rate
  (target: the rod-related draws — the oracle's prim-type-4 crystal quads — substantially
  matched; exact target set from Task 2's baseline, since the frame has ~3381 type-4 draws
  total across all layers and 6 rods this frame is one layer's worth).

- [ ] **Step 1: Add passes incrementally, measure after each**

For each pass in §1's table not yet added (in body order: `0x232da0` prep, `0x232e38` colored
+ zeroed-jitter variants, `0x233328`, `0x2333b8`, `0x232f80`), add the `call(...)` with its
mapped a0/a1/f12/f13 (from the §1 table), rerun `--drive-rods` + `vdiff --subset`, and record
the match delta. If a pass REDUCES the match or emits garbage, revert it and note why
(it may need state the harness hasn't seeded — a finding, not a failure).

- [ ] **Step 2: Reconcile the "rendered twice" open item**

The contract flags the body rendering the rod set via `0x232e38` in TWO loop groups ([OPEN] —
two targets / reflection). If the first group already subset-matches the oracle's rod draws,
STOP there for Milestone A (do not chase the second group unless the oracle needs it). Record
which groups were needed.

- [ ] **Step 3: Record + commit**

```bash
git add tools/eerun/main.cpp src/ee/ tests/EeInterpreterTest.cpp docs/ghidra_analysis/sp1-interpreter-runs.md
git commit -m "GS(Ee): add per-rod passes to match the oracle rod set"
```

---

### Task 4: Render the driven rods — Milestone A (rods visible)

Feed the interpreter-produced rod packets into the existing renderer (the proven 5–8% replay
path) so the rods are actually drawn to a frame — the first VISIBLE output of the execute-the-
original pipeline.

**Files:**
- Modify: `src/app/GsScene.{hpp,cpp}` (a loader that builds a `GsCommandStream` from a decoded
  candidate — reuse `GsDumpParser::decodeGifData` on the captured wire bytes), `src/main.cpp`
  (a `--drive-clock` branch: run the rod driver, decode to a `GsCommandStream`, hand it to
  `GsRenderer` exactly as a loaded `.gs` scene is today).
- Test: reuse the pixel-diff gate (`tools/gate`) informally — render the driven rods and
  compare to the clock_sw reference at the frozen time; not a hard gate yet (partial scene).

**Interfaces:**
- Consumes: the wire packets from Tasks 2–3; `GsScene`/`GsRenderer` (the existing replay
  renderer), `ResourceManager::downloadImage`/`--dump-rgba` readback.
- Produces: `CrystalClockVK --drive-clock` renders the driven rods to `mainColorImage`;
  `--dump-rgba <file>` reads it back. Milestone A = a recognizable rod dial appears.

- [ ] **Step 1: Build a GsCommandStream from the driven packets**

Add to `GsScene` a path that takes the captured wire-packet bytes (from the rod driver) and
runs `GsDumpParser::decodeGifData` into a `GsCommandStream` (same struct `GsScene::load`
produces from a `.gs`). The simplest wiring: eerun writes the wire bytes to a file
(`--dump-wire <file>`), and `GsScene` gains `loadWirePackets(path)`. Show the function.

- [ ] **Step 2: Wire a --drive-clock render path in main.cpp**

Add a `--drive-clock` branch in `src/main.cpp` that: loads `re/ram/clock/eeMemory.bin` via the
rod driver (or reads a pre-dumped wire file), builds the `GsScene`, and drives `GsRenderer`
exactly as the existing `.gs` path (`gsRenderer.init(...)` + per-frame `record(...)`). Reuse
the existing GsRenderer setup verbatim — do NOT fork the renderer.

- [ ] **Step 3: Render + read back + eyeball against the reference**

Run: `bin\CrystalClockVK.exe --drive-clock --dump-rgba re\oracle\drive_rods.rgba` then convert
with `node tools\pixeldiff\rgba2png.mjs re\oracle\drive_rods.rgba 640x224 re\oracle\drive_rods.png`
and compare structurally to `re/ram/clock/Screenshot.png` (radial crystal rods present?). This
is the Milestone-A check: rods visibly render from executed original code. Report honestly
whether they appear and how they compare (partial is fine — only the rod passes are wired).

- [ ] **Step 4: Record + commit**

Write the Milestone-A result (what renders, the informal pixel-diff vs reference) to
`docs/ghidra_analysis/sp1-interpreter-runs.md` and update the memory phase note.

```bash
git add src/app/GsScene.* src/main.cpp docs/ghidra_analysis/sp1-interpreter-runs.md
git commit -m "Feat(App): --drive-clock renders driven rods (Milestone A)"
```

---

## After this plan

Milestone A reached = rods visibly rendered from executed original code, subset-matching the
oracle. Remaining SP1 (own follow-up plans, written from the findings here): light spots +
swirl render, PSMT4 text, time→state seeding for live wall-clock time, and the cross-frame
trail persistence (Gate C/D). Then SP2 (menu) and SP3 (boot) reuse this whole interpreter +
driver + renderer machine. SP4 ports the executed functions to clean C++ under the pixel gate.

## Self-review notes

- Spec coverage: SP1 spec Gate B (vertex-diff of rod passes) = Tasks 2–3; the "then pixel-diff"
  half = Task 4. Gates C/D (spots/swirl/text/time/trails) are explicitly deferred to follow-up
  plans per "After this plan" — this plan is scoped to Milestone A (rods) by design.
- Discovery-vs-code: Task 1 is discovery (procedure + decision, no fixed code) because the
  staging→wire route has [OPEN] items in the contract; Tasks 2–4 carry concrete code/commands.
  This mirrors Phase 1's Task 4 discovery precedent — honest for RE work, not a placeholder.
- Judgment calls the implementer must LOG not guess: the packetize entry fn + destination
  buffer (Task 1), which passes are needed for the match (Task 3), the "rendered twice" open
  item (Task 3 Step 2). All have stated procedures.
- Types consistent: `call()`/`fpr[]`/`sprData()`/`decodeGifData`/`writeJson`/`GsCommandStream`
  used with the signatures confirmed in the codebase; addresses all from the contract doc.
