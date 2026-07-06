# OSDSYS 1:1 Master Strategy — boot + menu + clock, interactive, then modernize

**Date:** 2026-07-05 · **Status:** APPROVED (user, this session) · **Supersedes** the
"procedural port via static Ghidra decompile" track (hit the 0x2738a0 wall) and the
scrapped invented-look track (W3 `--clock`).

## 1. Goal

Recreate the PS2 OSDSYS visual layer 1:1 in Vulkan, **fully interactive**:

1. **Boot** screen (towers/fog intro), animated.
2. **Main menu** (browser), animated AND navigable (selection, highlight,
   transitions, config), SDL3 input in place of the pad.
3. **Crystal clock**, live, marking real wall-clock time.

Then (phase 2) modernize/polish the code **without any visual change**, enforced
numerically.

"1:1" is defined numerically, never by eye: pixel-diff vs GSRunner (bit-accurate
software GS) at fixed reference states, at or below the proven replay levels
(clock 5.6–7.9%, menu 5.4%, boot 10.5% — boot target to be tightened in SP3).

## 2. Core principle: execute the original, don't re-derive it

The previous failure mode was rewriting pixel-emitting code from broken static
decompile, then filling gaps by invention. Inverted permanently:

- Logic comes from the **CrystalOSD byte-perfect decomp** (C source) wherever it
  covers the function.
- Where the decomp doesn't cover it, code is read from **live RAM** (the static
  ELF region at 0x2738a0 is data/overlay; runtime RAM has the real code).
- If a function still can't be decompiled from RAM, its original MIPS bytes are
  **executed** (mini EE interpreter / ps2recomp) — never re-invented.
- Every visual element traces to exactly one of: executed original code, a dump
  measurement, or a live read. Authored shaders/looks are banned.

## 3. Ground-truth hierarchy (docs are NEVER a source)

1. Live PCSX2 execution/RAM (BIOS ps2-0230a-20080220 only).
2. GS dumps + GSRunner replay (the pixel/vertex oracle).
3. CrystalOSD decomp source.
4. Ghidra static decompile — **only with raw-disassembly cross-check** (this
   binary has shared tails and misclassified regions; decompile output alone
   was repeatedly wrong).
5. Project docs — a **hypothesis log**, never evidence. See §6 doctrine.

## 4. Sub-projects (each gets its own spec → plan → implementation)

| SP | Scope | Gate |
|----|-------|------|
| **SP0** | Cleanup + docs correction + tooling foundations (§5) | Docs 100% status-tagged; dead code removed; automated pixel-diff gate green on the 5 existing dumps |
| **SP1** | Clock live: rods (338-quad/5-layer tessellation via decomp/RAM/interp), light spots + draw fn, swirl, PSMT4 text, real time | Vertex-diff vs prims_sw.json per pass, then pixel-diff ≤ replay level at fixed time |
| **SP2** | Menu: cube rendering (reuses rod pipeline) + browser state machine, input, highlight, transitions, config storage — ported from decomp | Pixel-diff on menu states + scripted input walkthrough reproducing captured states |
| **SP3** | Boot: generator RE from zero (method proven in SP1/2) + the 10.5% residual | Pixel-diff at N fixed boot-anim timestamps |
| **SP4** | Modernization: refactor with the pixel gate on every commit | Zero numeric drift across the whole refactor |

An SP starts only when the previous one is gated. Known walls per SP are
inventoried in §7 so none of them arrives as a surprise.

## 5. SP0 — Cleanup, docs truth, foundations (the immediate work)

Primary focus (user-directed): **leave the documentation 100% correct and
organized, extracting the maximum information now** (Ghidra + PCSX2 live).

### 5.1 Project cleanup (dead + wrong parts out first)

- **Delete the invented-look track**: prism/tunnel/spot shaders, `--clock`
  invented window path, `bg_tunnel`, `rod_prism`, `spot.frag`, ClockOrb glow
  billboards — everything W3 that guessed the look. Keep (clearly relabeled as
  logic, not look): `ClockState` (time→visual), `RodField::buildDialMesh` 2D
  dial (flagged keeper for a future tray icon), their CTests.
- **Audit `src/clock/`**: the W1 projection is a 1-rod regression-lock
  (underdetermined fit) — mark PROVISIONAL in code or delete if SP1 replaces it.
- Remove stale scripts/outputs (clock_refract.png, clock_orb.png and similar
  scrapped-track artifacts in repo root).

### 5.2 Docs audit — every claim gets a status tag

Sweep ALL of `docs/` (especially `ghidra_analysis/`). Each claim becomes one of:
`LIVE-VERIFIED` / `DUMP-MEASURED` / `DECOMP-SOURCED` / `HYPOTHESIS` /
`FALSIFIED`. Known falsified claims to correct at the source doc (not just in
newer docs):

- "master render = 0x00225E80" → real per-frame render 0x00232618 (live BP).
- The four "GS state setter" labels (WaitSema sync / icon-browser /
  DMA trampoline — not blend writes).
- "FUN_0022f720 writes the register template" (inline in caller instead).
- "FUN_00235350 is the GS kick" (no kick visible; kick is UNLOCATED).
- "96 sprites / one blend" → 3948 draws / 3 blends.
- "8 rods on a circle" → 12-rod dial + 4 menu cubes.
- Stale GP-relative addresses (old gp 0x002AF070 → live 0x002CFEF0).
- W0 blend remap (CONTESTED) and W1 projection (PROVISIONAL fit) marked as such.

Rule going forward: a new correction MUST edit the old doc; append-only
correction layers are what rotted the docs.

### 5.3 CrystalOSD coverage audit — CANCELLED (2026-07-05)

User verified the decomp is unusable ("todo zuado", same class as the old raylib
recreation) and its documented path no longer exists. The decomp is REMOVED from
the ground-truth hierarchy (§3 layer 3 demoted to: use only if a trustworthy
decomp ever materializes). All `RE-NEEDED` classification collapses to: live RAM
+ native PCSX2 disassembly (validated this same day — the 0x2738a0 wall fell to
exactly this method). Task 3 of the SP0 plan is cancelled accordingly.

### 5.4 Live RAM extraction (Ghidra + PCSX2 open now)

For `RE-NEEDED` functions only:
- With each screen active, dump EE RAM (at minimum 0x270000–0x280000 + the
  render-spine range; ideally full 32MB per screen) → import into Ghidra as a
  NEW program per screen (overlays may map different code per screen).
- Read live: `DAT_002973a0/c0` register templates (16B each), rod model struct
  (0x375250, stride 0x160: +0x00 origin/+0x10 dir/+0x20 screen/+0x140 normal),
  the real FOV gp-global, light-spot consumer (trace reads of 0x34c830).
- Tooling rules (already paid for in crashes): boot to the target screen FIRST,
  then arm BPs; never reset with OSDSYS BPs armed; heap shifts per boot; pause
  lands in the idle thread (regs zeroed).

### 5.5 Automated regression gate

One script: build → render each of the 5 dumps → GSRunner ref → pixel-diff →
PASS/FAIL vs recorded thresholds. Runs locally per commit; becomes the visual
freeze for SP4. (Vertex-diff harness vs prims_sw.json added in SP1.)

## 6. Docs doctrine (permanent)

- Docs record hypotheses and their verification status; they are never cited as
  evidence.
- Every claim carries its status tag and its evidence pointer (dump file, live
  session, decomp file:line).
- Corrections edit in place. `FALSIFIED` claims stay visible (struck through or
  in a Falsified section) so they aren't re-derived.
- MEMORY.md and CLAUDE.md updated whenever a wall opens or closes.

## 7. Known walls inventory (what we will face, by evidence)

**Cross-cutting:** 0x2738a0 region unreadable statically (per-screen overlay
risk → per-screen RAM dumps); DMA/GIF kick unlocated (computed dispatch — live
trace only); fragile live tooling (recLUT crash rule, heap-shift, idle-thread
pause); Ghidra DB stale boundaries (always cross-check raw disasm).

**Clock:** 338-quad/5-layer rod tessellation function is the wall function
itself; light-spot DRAW unlocated (positions RESOLVED); real FOV unknown;
after-image trails need cross-FRAME framebuffer persistence (current renderer
proves only intra-frame feedback).

**Menu:** browser state machine + input + config storage barely mapped — but
pure logic (no GS wall); expected to come from the decomp.

**Boot:** zero generator RE exists; fully animated (no static frame); worst
replay residual (10.5%, towers/fog, 5 blend modes).

**Modernization:** silent visual drift — countered by §5.5 gate on every
commit; open fine residuals if we tighten targets: AA1 line energy (~half ref,
wideLines), integer blend rounding (+0x7f), boot residual.

**Infra:** offscreen depth path crash 0xC0000409; shaders need CMake
reconfigure; .git 185MB; ~7GB free disk has blocked tool builds before.

## 8. What is explicitly NOT the plan

- No authored shaders or "artistic" approximations, ever.
- No trusting a single Ghidra decompile without disasm cross-check.
- No new claims written as fact without a status tag + evidence pointer.
- No starting SP1 before SP0's gates are green.
