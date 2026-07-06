# SP0 — Cleanup, Docs Truth & Foundations Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the invented-look code, make every doc claim status-tagged and correct, audit decomp coverage, capture per-screen live RAM + key live reads, and stand up the automated pixel-diff gate — per the approved spec `docs/superpowers/specs/2026-07-05-osdsys-1to1-master-strategy-design.md`.

**Architecture:** No new product code. Deletions + docs edits + one Node gate script + RE artifacts. Live PCSX2/Ghidra work (Tasks 5–7) runs in the MAIN session (stateful, crash-prone tooling); analysis/docs/cleanup tasks (1–4) go to subagents.

**Tech Stack:** CMake/C++23 build already in place; Node (tools/*.mjs pattern); pcsx2-mcp (DebugServer 21512); ghidra-mcp (`program="OSDSYS.elf"` always).

## Global Constraints

- Commit convention: `Type(Scope): imperative, ≤72 chars`. **NEVER a Co-Authored-By trailer.**
- **Subagents run on Sonnet or Haiku, never the top-tier model** (docs sweeps → Haiku; code/RE analysis → Sonnet).
- Docs doctrine (spec §6): every claim tagged `LIVE-VERIFIED / DUMP-MEASURED / DECOMP-SOURCED / HYPOTHESIS / FALSIFIED` + evidence pointer; corrections edit the old doc IN PLACE; falsified claims stay visible (struck through).
- PCSX2 rules (paid in crashes): boot to target screen BEFORE arming any BP/watchpoint; NEVER reset with OSDSYS BPs armed (recLUT crash); pause lands in idle thread (regs zeroed — memory reads still valid); heap-shifts per boot.
- Ghidra decompile is only trusted with a raw `disassemble_bytes` cross-check (shared-tail binary).
- Known-good inputs: dumps `C:\Users\dell04\Documents\PCSX2\snaps\{boot,clock_sw,clock_viewer,clock_closeup,config_menu}.gs`; GSRunner `C:\CodingProjects\Personal\pcsx2-gsrunner\bin`; replay thresholds clock_sw 6.8% / clock_viewer 7.9% / clock_closeup 5.6% / config_menu 5.4% / boot 10.5%.
- **OPEN INPUT (user):** the CrystalOSD decomp is NOT at the documented `C:\CodingProjects\Personal\CrystalOSD` (path stale; `...\Personal\OSDSYS` is the old raylib recreation, not the decomp). Task 3 needs the real path from the user.

---

### Task 1: Delete the invented-look code (keep evidence-grounded logic)

**Files:**
- Delete: `shaders/bg_tunnel.frag`, `shaders/bg_tunnel.vert`, `shaders/rod_prism.frag`, `shaders/rod_prism.vert`, `shaders/spot.frag`, `shaders/spot.vert`
- Modify: `src/clock/ClockRenderer.{hpp,cpp}` (remove prism/tunnel/spot passes; KEEP the flat dial path), `src/clock/ClockOrb.{hpp,cpp}` (KEEP position/trail formula — it is the RESOLVED FUN_0020eda0 math; DELETE `buildSpotMesh`/billboard-glow code), `src/clock/RodField.{hpp,cpp}` (KEEP `buildDialMesh` — tray-icon keeper; DELETE prism-bar 3D mesh building), `src/main.cpp` (remove `--clock` invented window path), `tools/clockdump/main.cpp` (remove `--prism`; keep flat `--dump-rgba` harness), `CMakeLists.txt` (drop `PrismMeshTest`; prune deleted sources)
- Delete: `tests/PrismMeshTest.cpp`; prune invented-mesh assertions from `tests/ClockOrbTest.cpp` (keep position-formula tests)
- Keep untouched: `src/clock/ClockState.*`, `ClockMath.hpp`, `Projection.*` (add a `// PROVISIONAL: 1-rod regression fit, NOT evidence-grade — replaced in SP1` comment at the top of `Projection.cpp`)

**Interfaces:**
- Produces: a repo where `git grep -l "prism\|tunnel\|spot"` in shaders/src returns only evidence-grounded hits; `clock` lib still exports `ClockState`, `RodField::buildDialMesh`, `ClockOrb` positions.

- [ ] **Step 1:** Read each file above; list every reference to the deleted symbols (compile-time truth beats this plan — follow the actual references).
- [ ] **Step 2:** Delete the 6 shader files and `tests/PrismMeshTest.cpp`; apply the modifications; prune CMakeLists (the `foreach(t ...)` test list at line ~290 and `clock_dump`/main sources at lines ~115/269).
- [ ] **Step 3:** `cmake -B build && cmake --build build` → expect clean build (CMake reconfigure is REQUIRED — shader glob is configure-time).
- [ ] **Step 4:** `ctest --test-dir build --output-on-failure` → expect all remaining tests pass (was 15; PrismMesh removed and ClockOrb pruned → count drops accordingly; record the new count).
- [ ] **Step 5:** Commit: `Refactor(App): delete invented W3 look, keep evidence logic`

### Task 2: Docs audit — status-tag every claim, fix falsified ones in place

**Files:**
- Modify: every file in `docs/` and `docs/ghidra_analysis/` (27 files listed by `Glob docs/**/*.md`), plus `CLAUDE.md`
- Create: `docs/DOCS-AUDIT.md` (index: file → audit date → residual HYPOTHESIS count)

**Interfaces:**
- Consumes: falsified-claims list from spec §5.2.
- Produces: every doc has a header line `> Audit 2026-07-05: claims status-tagged per spec §6.` and inline tags.

- [ ] **Step 1:** Fan out ONE subagent per doc file (Haiku for short docs, Sonnet for `CLOCK-SYSTEM-MAP.md`/`PORT-FUNCTION-MAP.md`/`current-phase` class files). Each subagent: tag each factual claim with `[LIVE-VERIFIED]` / `[DUMP-MEASURED]` / `[DECOMP-SOURCED]` / `[HYPOTHESIS]` / `[FALSIFIED →correct value + evidence]`; apply the spec §5.2 falsified list wherever the wrong claim appears (0x00225E80, the 4 "GS state setters", FUN_0022f720 template writer, FUN_00235350 kick, 96-sprites/1-blend, 8-rod circle, gp 0x002AF070).
- [ ] **Step 2:** In `CLAUDE.md`: fix `pcsx2-ref` path (real: `C:\CodingProjects\Personal\pcsx2`), and mark the CrystalOSD path `STALE — real location pending user` until Task 3 resolves it.
- [ ] **Step 3:** Controller (main session) spot-checks 3 random audited docs against the spec list — subagents over-reach historically; verify no NEW claims were invented during tagging.
- [ ] **Step 4:** Write `docs/DOCS-AUDIT.md` index.
- [ ] **Step 5:** Commit: `Docs(Project): SP0 audit - all claims status-tagged in place`

### Task 3: CrystalOSD decomp coverage audit

**Files:**
- Create: `docs/ghidra_analysis/DECOMP-COVERAGE.md`

**Interfaces:**
- Consumes: decomp path FROM USER (blocker — documented path is stale).
- Produces: per function a verdict `DECOMP-COVERED (file:line)` or `RE-NEEDED`, for: transform+emit (target of 0x2738a0), `FUN_002738e8`, the DMA/GIF kick, light-spot draw, browser/input state machine, config storage, boot generator, `pktSetAlphaBlend`-family GS builders.

- [ ] **Step 1:** Ask the user for the decomp's real path (it is NOT `C:\...\CrystalOSD` nor `C:\...\OSDSYS`).
- [ ] **Step 2:** Subagent (Sonnet): grep the decomp for each function/address/behavior signature (e.g. rod-array 0x375250 refs, WaitSema wrappers, GIF-tag builders); classify each as COVERED/RE-NEEDED with file:line evidence.
- [ ] **Step 3:** Write `DECOMP-COVERAGE.md`; update spec §5.4 scope (RE-NEEDED list shrinks or grows).
- [ ] **Step 4:** Commit: `Docs(Project): decomp coverage audit - walls mapped to sources`

### Task 4: Automated pixel-diff regression gate

**Files:**
- Create: `tools/gate/gate.mjs`, `tools/gate/thresholds.json`
- Test: the gate run itself is the test (it must PASS on current HEAD and FAIL when thresholds are tightened to 0).

**Interfaces:**
- Consumes: app `--dump-rgba` CLI (read `src/main.cpp` to confirm exact arg names before writing the script), `tools/pixeldiff/pdiff.mjs`, GSRunner at `C:\CodingProjects\Personal\pcsx2-gsrunner\bin` (run with bin on PATH, `-renderer sw`, ref pick rule: LAST 640x224 `rt1_<fbp*32 hex>` per dump; display FBP from privRegs — boot/menu FBP 70, clock FBP 0).
- Produces: `node tools/gate/gate.mjs` → exit 0 + per-dump table, or exit 1 naming the failing dump.

- [ ] **Step 1:** Write `tools/gate/thresholds.json`:
```json
{
  "toleranceAbs": 1.0,
  "dumps": {
    "clock_sw":      { "gs": "C:/Users/dell04/Documents/PCSX2/snaps/clock_sw.gs",      "maxPct": 6.8 },
    "clock_viewer":  { "gs": "C:/Users/dell04/Documents/PCSX2/snaps/clock_viewer.gs",  "maxPct": 7.9 },
    "clock_closeup": { "gs": "C:/Users/dell04/Documents/PCSX2/snaps/clock_closeup.gs", "maxPct": 5.6 },
    "config_menu":   { "gs": "C:/Users/dell04/Documents/PCSX2/snaps/config_menu.gs",   "maxPct": 5.4 },
    "boot":          { "gs": "C:/Users/dell04/Documents/PCSX2/snaps/boot.gs",          "maxPct": 10.5 }
  }
}
```
- [ ] **Step 2:** Write `gate.mjs`: for each dump — (a) run the app headless render+readback, (b) run GSRunner ref (cache refs in `tools/gate/.cache/` keyed by dump hash — GSRunner refs never change per dump), (c) run pdiff, (d) compare `pct <= maxPct + toleranceAbs`. Print a table; nonzero exit on any failure.
- [ ] **Step 3:** Run the gate → expect **PASS on all 5** (values ≈ recorded thresholds).
- [ ] **Step 4:** Negative test: temporarily set `clock_sw.maxPct` to 0, rerun → expect FAIL naming clock_sw; revert.
- [ ] **Step 5:** Add a `gate` convenience: `run_gate.bat` at repo root calling the script. Commit: `Feat(CI): automated pixel-diff gate over the 5 reference dumps`

### Task 5: Live RAM capture — clock screen (MAIN session, PCSX2 open)

**Files:**
- Create: `re/ram/clock/ee_ram_clock.bin` (up to 32MB — **gitignored**; add `re/` to `.gitignore`), `docs/ghidra_analysis/sp0-live-reads.md`

**Interfaces:**
- Produces: RAM image for Task 6; live values (all tagged LIVE-VERIFIED with session date): `DAT_002973a0` + `DAT_002973c0` (16B each), rod structs `0x375250` (16 × 0x160B) and `0x377e50`, FOV globals (gp=0x002CFEF0 base: `uGpffff8480`→addr 0x002C8370, `uGpffff8484`→0x002C8374 — compute as gp−0x7b80/−0x7b7c and verify), packet buffer head `0x375230` (64B).

- [ ] **Step 1:** `pcsx2_status` + `pcsx2_connect`; confirm OSDSYS crystal-clock screen is on screen (user confirms visually). `pcsx2_save_state` (backup slot) BEFORE anything else.
- [ ] **Step 2:** `pcsx2_read_memory` the full EE RAM in chunks (0x00000000–0x02000000) → write `re/ram/clock/ee_ram_clock.bin`. Minimum viable if slow: 0x00200000–0x00400000 (code+data window covering 0x2738a0 and all known structs).
- [ ] **Step 3:** Read the listed addresses; paste raw hex into `sp0-live-reads.md` beside each label. Cross-check `DAT_002973a0/c0` bytes against the dump's per-pass ALPHA/TEX register values (gsdump JSON) — they must match or the doc claim is FALSIFIED.
- [ ] **Step 4:** Commit docs + .gitignore: `Docs(Project): SP0 live reads - templates, rod structs, FOV`

### Task 6: Ghidra on the RAM image — settle 0x2738a0

**Files:**
- Modify: `docs/ghidra_analysis/PORT-FUNCTION-MAP.md` (edit the STATIC-RE WALL section in place with the verdict)

**Interfaces:**
- Consumes: `re/ram/clock/ee_ram_clock.bin` (Task 5).
- Produces: verdict — `0x2738a0 = real code in RAM (decompiled, here it is)` or `still data (overlay confirmed absent → interpreter path)`.

- [ ] **Step 1:** `ghidra import_file` the RAM image as a new program (`ram_clock`, MIPS-R5900 little-endian, image base 0x0).
- [ ] **Step 2:** Disassemble at 0x2738a0 and 0x2738e8; cross-check `decompile` vs `disassemble_bytes` (shared-tail rule). Compare bytes vs the static ELF at the same VMA — identical bytes = DB-stale theory dead, RAM≠ELF = overlay confirmed.
- [ ] **Step 3:** If code: decompile transform+emit fully; record signature, matrix layout, GS-packet write pattern in PORT-FUNCTION-MAP.md (tagged LIVE-VERIFIED). If data: record the overlay conclusion and mark the interpreter fallback as the SP1 path.
- [ ] **Step 4:** Commit: `Docs(Project): 0x2738a0 verdict from live RAM image`

### Task 7: Find the light-spot draw consumer (watchpoint on 0x34c830)

**Files:**
- Modify: `docs/ghidra_analysis/PORT-FUNCTION-MAP.md` (Light spots section, in place)

- [ ] **Step 1:** Clock screen active (already true). `pcsx2_set_watchpoint` READ @ 0x34c830 (4B).
- [ ] **Step 2:** On hit: `pcsx2_get_backtrace` + PC → the consumer address. Remove the watchpoint IMMEDIATELY after capture.
- [ ] **Step 3:** Decompile the consumer in Ghidra (RAM program from Task 6 if the region is overlay-affected; else OSDSYS.elf) with the disasm cross-check.
- [ ] **Step 4:** Record in PORT-FUNCTION-MAP.md (LIVE-VERIFIED). Commit: `Docs(Project): light-spot draw consumer located via watchpoint`

### Task 8 (deferred within SP0, needs user at the emulator): RAM captures for menu + boot screens

Same procedure as Task 5/6 (`re/ram/menu/`, `re/ram/boot/`), user navigates the emulator to each screen first. Boot capture needs a savestate early in the boot animation. Gate for SP0 completion includes these two captures existing, but Tasks 1–7 don't block on them.

---

## Execution order & parallelism

- **Wave 1 (subagents, parallel):** Task 1 (Sonnet), Task 2 fan-out (Haiku/Sonnet), Task 4 (Sonnet). Independent files — no conflicts (Task 2 touches docs only; Task 1 code only; Task 4 tools only).
- **Wave 2 (main session, live):** Task 5 → 6 → 7 sequentially (stateful emulator).
- **Task 3** starts the moment the user provides the decomp path (any time).
- **Task 8** when the user can drive the emulator to menu/boot.

## SP0 exit gate (from spec §4)

Docs 100% status-tagged (DOCS-AUDIT.md complete) · invented code deleted, build+ctest green · `gate.mjs` PASS on 5 dumps · DECOMP-COVERAGE.md written · clock RAM image + live reads captured · 0x2738a0 verdict recorded · menu/boot RAM images captured.
