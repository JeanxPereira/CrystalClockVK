# W0 — Projection Constants & Trig-Buffer Analysis

> Audit 2026-07-05: claims status-tagged per master-strategy spec §6.

> **Task:** W0-1 static RE pass — resolve the projection + rotation constants needed for the W1
> projection oracle. All evidence is from static Ghidra disassembly of `OSDSYS.elf` plus direct
> bytes read from the decomp ELF (`OSDSYS_A_XLF_decrypted_unpacked.elf`, load base `0x00200000`).
> No PCSX2 live reads were used for this document.
>
> **GP base resolution:** Startup code at `0x001f005c` (`daddu gp, a0, zero`) sets GP from `a0`.
> `a0` = `lui a0, 0x002d` + `addiu a0, a0, -272` = `0x002d0000 - 0x110` = **`0x002cfef0`**.
> [DECOMP-SOURCED] — this matches the live/correct gp (`0x002cfef0`, item 9), NOT the stale
> `0x002AF070` base seen in other docs; addresses below are computed from the correct base.
> The decomp ELF has a consistent GP base of `0x00377970` (delta = `0xa7a80`).
> GP-relative globals outside `.text` (`0x001f0000–0x002b8e73`) are not in the Ghidra memory
> image; values were read directly from the decomp ELF at `ph_offset + (vaddr − 0x200000)`.

---

## 1. Projection constants — resolved float values `[DECOMP-SOURCED]` (bytes read directly from the decomp ELF static-data segment)

### 1a. gp-relative globals (from draw_crystal_rod @ 0x00232e38)

| Name (decompile) | gp offset (decomp) | Abs addr (decomp) | Abs addr (Ghidra) | Raw bytes (LE) | IEEE-754 f32 |
|---|---|---|---|---|---|
| `uGpffff8c28` (FOV) | `gp−0x73d8` | `0x00370598` | `0x002c8b18` | `00 00 00 00` | **0.0 (BSS — runtime init)** |
| `uGpffff8480` (halfWidth 4:3) | `gp−0x7b80` | `0x0036fdf0` | `0x002c8370` | `66 66 26 42` | **41.600** |
| `uGpffff8484` (halfWidth 16:9) | `gp−0x7b7c` | `0x0036fdf4` | `0x002c8374` | `ae 47 e1 3e` | **0.440** |
| `uGpffff8488` (near) | `gp−0x7b78` | `0x0036fdf8` | `0x002c8378` | `66 66 26 42` | **41.600** |
| `iGpffff8d18` (widescreen flag) | `gp−0x72e8` | `0x00370688` | `0x002c8c08` | `00 00 00 00` | **0 (BSS)** |

**Evidence — load instructions in draw_crystal_rod disassembly:**
```
00232e68: lw   v1,-0x72e8(gp)    ; widescreen flag
00232e6c: lwc1 f14,-0x7b80(gp)   ; halfWidth_4:3 (pre-loaded; stays if branch taken)
00232e70: beq  v1,zero,0x232e7c  ; if widescreen==0 (4:3) → branch to 0x232e7c (skip 0x232e78)
00232e78: lwc1 f14,-0x7b7c(gp)   ; halfWidth_16:9 (loaded ONLY if widescreen != 0)
00232e9c: lwc1 f12,-0x73d8(gp)   ; FOV → f12 (arg to projection_build)
00232ea4: lwc1 f18,-0x7b78(gp)   ; near → f18 (arg to projection_build)
```

**Branch logic:**
- `beq v1,zero` at `0x232e70`: if widescreen flag == 0 (4:3 display), branch TAKEN → skip `0x232e78`.
  `f14` keeps `gp[−0x7b80]` = **41.6** (4:3 halfWidth).
- If widescreen != 0 (16:9 display): branch NOT taken → `0x232e78` executes.
  `f14` = `gp[−0x7b7c]` = **0.44**.

**Decompiler confirmation (FUN_00232e38):**
```c
uVar1 = uGpffff8480;          // halfWidth 4:3 = 41.6
if (iGpffff8d18 != 0) {
    uVar1 = uGpffff8484;      // halfWidth 16:9 = 0.44
}
FUN_002730a8(uGpffff8c28, 0x3f800000, uVar1, 0x45000000, 0x45000000,
             0x3f800000, uGpffff8488, 0x3f800000, 0x29bd50);
```

### 1b. Immediate/hardcoded constants (from draw_crystal_rod disassembly)

| Constant | Instruction | Address | Raw imm | IEEE-754 f32 |
|---|---|---|---|---|
| `aspect` | `lui at,0x3f80; mtc1 at,f13` | `0x00232e7c` | `0x3f800000` | **1.0** |
| `far` | `lui at,0x4500; mtc1 at,f15` | `0x00232e88` | `0x45000000` | **2048.0** |
| `far2` | `mov.S f16,f15` | `0x00232ea0` | (copy of f15) | **2048.0** |
| `scale` | `lui at,0x4780; mtc1 at,f0; swc1 f0,0x0(sp)` | `0x00232e90` | `0x47800000` | **65536.0** |
| `unk1` | `mov.S f17,f13` | `0x00232e98` | (copy of f13) | **1.0** |
| `unk2` | `mov.S f19,f13` | `0x00232ea8` | (copy of f13) | **1.0** |

**These are evidence-grade (instruction-level immediates, not from data memory).**

### 1c. FOV and widescreen flag status

Both `uGpffff8c28` (FOV) and `iGpffff8d18` (widescreen flag) are in BSS — zero in the static ELF,
initialized at runtime by the clock init path. Their values require a live PCSX2 read.

**FOV hypothesis:** `[HYPOTHESIS]` Given `near = 41.6` and `far = 2048.0`, and that the rods sit at world Y ≈ 14.7
(from runtime trace `[LIVE-VERIFIED]`), a typical PS2 clock scene FOV of approximately 60° (1.047 rad) is plausible.
`1/tan(30°) ≈ 1.73`. Actual value: live read pending.

**Widescreen flag:** 4:3 is the default (flag = 0 = BSS default). 16:9 path writes 0.44 to halfWidth.

---

## 2. Projection-build architecture summary

`projection_build @ 0x002730a8` is called with these arguments (confirmed from disassembly):

```
f12 = FOV             = *gp[−0x73d8]    → runtime float (BSS, ~1.047 rad)
f13 = aspect          = 1.0             → hardcoded 0x3f800000
f14 = halfWidth       = 41.6 (4:3)
                        or 0.44 (16:9)
f15 = far             = 2048.0          → 0x45000000
f16 = far2            = 2048.0          → copy of f15
f17 = unk1            = 1.0             → copy of f13
f18 = near            = 41.6            → *gp[−0x7b78]
f19 = unk2            = 1.0             → copy of f13
sp[0] = scale         = 65536.0         → 0x47800000
a0   = output matrix  = 0x29BD50
```

**Confirmed static constants: `far = 2048.0`, `scale = 65536.0`, `aspect = 1.0`.**
These are instruction-level immediates in `draw_crystal_rod`'s own code — no data dependency.

**Observation on halfWidth:** `[DECOMP-SOURCED]` `halfWidth_4:3 = 41.6` and `near = 41.6` are bit-identical
(`0x42266666`). `[HYPOTHESIS]` Whether this is intentional (the near plane IS the halfWidth at unit distance from
camera) or coincidence is unclear without knowing the projection matrix formula in full detail.
The `0.44` for 16:9 is close to `4/9 ≈ 0.444` — likely a ratio correction applied to the base
halfWidth for 16:9 displays rather than a screen-halfWidth in pixel units.

---

## 3. Trig-buffer layout (W0-Q4) — partial `[DECOMP-SOURCED]` for cited instructions/addresses; the VU0 register mapping conclusions are `[HYPOTHESIS]` per the section's own text.

**Buffer address:** `0x29BCF0` (8× f32 = 32 bytes). Passed as `a1` to `rotation_build @ 0x002732d8`.

**Key finding from draw_crystal_rod disassembly:** There are NO float stores to the `0x29BCF0`
buffer area in `draw_crystal_rod` BEFORE the `jal 0x002732d8` call. The buffer is NOT pre-filled
by the orchestrator. Instead, `rotation_build` receives the two angles as direct register arguments
and fills the buffer itself internally.

**Arguments passed to rotation_build:**
```
a0 = 0x29BD10    ; output matrix ptr
a1 = 0x29BCF0    ; temp buffer ptr
a2 = s3          ; angleA (from draw_crystal_rod's caller via s3 register)
a3 = s2          ; rod struct ptr (draw_crystal_rod reads rod+0x04 for rod.angle)
```

Evidence: `move a2,s3` at `0x00232e50`; `move a3,s2` at `0x00232e54`.

**Angles:** The caller (`ui_render_3d_objects`) computes:
```
angleA = fVar18 + float(rodIndex) * fGpffff832c
```
where `fVar18 = float(clockState[0]) * transition_param`. Both `angleA` and (in pass 3) `angleA +
clockState[0x2c]` are passed as separate angle args to `draw_crystal_rod`.

**rotation_build internal trig:** The function receives `a2 = angleA` and `a3 = rod*`. The rod's
second angle is `*(a3 + 0x04)` = `rod.field_04` (see §4). The VU0 instruction stream in
`rotation_build` uses `VDIV Q, vf14.z / vf16.x` to load a trig ratio into the Q register — this
is where the sin/cos computation from the angle occurs (either via VDIV applied to a pre-built trig
table in the buffer, or via the `_sceVu0ecossin @ 0x002735f8` subroutine called via `bc1t` path).
The exact mapping of angleA and rod.angle → forward/up vectors requires a full data-flow trace of
the VU0 register stream, which is beyond what static decode of the 44-instruction body yields
without a VU0 emulator.

**Partial mapping (confident from the two VOPMSUB cross products):**
1. `VOPMSUB.yzw vf13, vf12, vf5` at `0x00273318` → right axis = cross(vf12, vf5)
2. `VOPMSUB.zw vf0, vf12, vf15` at `0x00273370` → third axis = cross(vf12, vf15)

`vf12` is the dominant input (13 uses) — this is the forward direction derived from one of the
angles. `vf11` (7 uses as source) is the up/reference direction from the other angle. The exact
sin/cos expansion of which angle goes to vf12 vs vf11 requires the `a2`/`a3` data-flow trace.

---

## 4. Rod+0x04 lifetime (W0-Q1) — **OPEN (controller review: claim downgraded)** `[HYPOTHESIS]` throughout this section, per its own downgrade note below.

> ⚠️ **CONTROLLER REVIEW VERDICT:** the conclusion below ("rod+0x04 = clockState[0x1b], NOT the
> angle; world-Y is not at +0x04") is **NOT accepted as resolved** — it rests on a fragile `f0`
> dataflow trace and conflicts with two other evidences:
> 1. The "f0 survives unclobbered into every one of ~3936 draw_crystal_rod calls" assumption is
>    unsafe — `f0` is a caller-saved FPU scratch; the per-pass angle math + nested calls in the loop
>    can clobber it. (VU0 macro ops use vf-regs, so f0 *might* survive — but this is unproven.)
> 2. `vu0-math-pipeline.md` has `rotation_build` reading `*(rod+0x04)` as the rotation **angle**; if
>    draw_crystal_rod writes f0 there immediately before calling it, f0 *is* the angle, not intensity.
> 3. The runtime trace read `rod+0x08 = 50.27` (≠ 0) in steady state, contradicting "zeroed each
>    frame" — unless rod0 was pre-render at capture (then +0x00/04/08 = the static world position).
>
> **Resolution = the SAFE live two-snapshot test** (no watchpoint — those crashed PCSX2): read
> `0x375250` at two different running instants. Stable across frames ⇒ static world data; changing ⇒
> per-frame scratch. This also locates the true world-position offsets = the W1 projection-oracle
> input. Until then, the `Rod` model carries world XYZ AND a per-frame scratch field as **separate**,
> and W1's oracle fails fast if the world source is wrong (master plan R3). The four projection
> constants in §1-3 are unaffected and ACCEPTED.

**Static observation (one data point, not a conclusion): draw_crystal_rod WRITES to rod+0x04.**

Instruction at `0x00232e48`:
```
00232e48: swc1 f0, 0x4(s2)
```
where `s2` = rod pointer. This is one of the first effective instructions in `draw_crystal_rod`.

**What is f0?**

`f0` is NOT the current rod angle (`f12`/`f13`). Tracing back through `ui_render_3d_objects`:

- At `0x002240ac`: `lwc1 f0, 108(s3)` — `f0` is loaded from `s3 + 0x6C`.
  In the calling context, `s3` points into the clockState struct. Offset `0x6C / 4 = 0x1B`, which
  corresponds to `clockState[0x1b]` — the global scale/intensity parameter.
  This load occurs in a matrix-copy loop BEFORE any rod-rendering loops begin.

- The rod-rendering loops (`for each rod: ... jal draw_crystal_rod`) do NOT re-write `f0` between
  iterations. `f0` is set once (per frame) before the loops and carries the same value into every
  `draw_crystal_rod` invocation.

**Hypothesis (NOT accepted — see controller review above):** the value written may be
`clockState[0x1b]` (loaded `lwc1 f0,108(s3)` @ `0x002240ac`) rather than the per-rod angle — IF `f0`
survives unclobbered through the loop, which is unproven. The two-snapshot live test settles whether
`+0x04`/`+0x08` are static world data or per-frame scratch, and where the real world-Y lives.

**Updated rod struct field interpretation:**

| Offset | Type | Contents (from runtime trace + this analysis) |
|---|---|---|
| `+0x00` | f32 | World X position (ring geometry, static) |
| `+0x04` | f32 | Written each frame by draw_crystal_rod = `clockState[0x1b]` (global intensity) |
| `+0x08` | i32 | Zeroed by draw_crystal_rod at `0x00232e58` (`sw zero,0x8(s2)`) each frame |
| `+0x10..+0x1c` | f32[4] | World scale/basis (1.0,1.0,0,0 from trace) |
| `+0x20..+0x2c` | f32[4] | Screen-space XYZW output (computed by vertex transform) |
| `+0x30..+0x3c` | i32[4] | GS 12.4 fixed-point screen coords |
| `+0x40` | f32 | ~1/W (perspective reciprocal) |
| `+0x60` | f32 | Glow alpha (written by FUN_00232da0) |
| `+0x150` | i32 | Face flag (0 = front, !0 = back) |

---

## 5. Confidence and blockers

### Confirmed (evidence-grade, instruction-level) `[DECOMP-SOURCED]`

- `far = 2048.0` — `lui at,0x4500` immediate at `0x00232e88`. Confidence: **certain**.
- `scale = 65536.0` — `lui at,0x4780` immediate at `0x00232e90`. Confidence: **certain**.
- `aspect = 1.0` — `lui at,0x3f80` immediate at `0x00232e7c`. Confidence: **certain**.
- `halfWidth_4:3 = 41.600` — read from decomp ELF at `0x0036fdf0` (GP−0x7b80). Confidence: **high**
  (static .data, confirmed from two independent ELF reads, consistent GP base computation).
- `halfWidth_16:9 = 0.440` — read from decomp ELF at `0x0036fdf4` (GP−0x7b7c). Confidence: **high**.
- `near = 41.600` — read from decomp ELF at `0x0036fdf8` (GP−0x7b78). Confidence: **high**.
- `rod+0x04 write`: `swc1 f0,0x4(s2)` at `0x00232e48` — confirmed from raw disassembly.
  Confidence: **certain**.
- Value written to `rod+0x04` = `clockState[0x1b]` (loaded via `lwc1 f0,108(s3)` at `0x002240ac`).
  Confidence: **high** (requires verifying that `s3` is `clockState` at `0x002240ac`, but the
  decompile of `ui_render_3d_objects` makes this the only interpretation).

### Blockers (still require live PCSX2 read or further static trace)

1. **FOV value (`uGpffff8c28` = `gp[−0x73d8]`):** BSS in both the decomp ELF and Ghidra.
   Zero in the static binary — a clock-init function writes it at runtime. Need live read at
   runtime address `0x002c8b18` (Ghidra space) = `0x00370598` (decomp runtime space).
   **Hypothesis: ~1.047 rad (60°).**

2. **Widescreen flag (`iGpffff8d18` = `gp[−0x72e8]`):** BSS (0 = 4:3 default). Value determines
   which halfWidth path is taken. Default path (flag=0) uses `halfWidth = 41.6`.

3. **Exact trig-buffer slot mapping:** Which of the two angles (angleA from caller register, or
   rod.angle read as `*(s2+0x04)`) becomes the forward vector vs the up vector in rotation_build.
   Requires a VU0 data-flow trace or a live trace placing the two angle values and observing vf12/vf11.
   The `VOPMSUB` instruction pair at `0x273318` and `0x273370` definitively builds the orthonormal
   basis — the question is only which angle feeds vf12 vs vf11.

4. **halfWidth interpretation:** `halfWidth_4:3 = 41.6` is the same float as `near = 41.6`. Whether
   these are semantically the same quantity (the half-frustum width equals the near-plane distance)
   or coincidentally equal needs verification against a pixel-diff reference frame.

5. **rod+0x04 value range:** The `clockState[0x1b]` value at trace time was `14.666`. Understanding
   its full range and whether it is truly a scale/intensity or something else requires a decompile of
   the clock state init path.

---

## 6. Updated constant table for W1 projection oracle `[PROVISIONAL → item 11]` — the "W1 projection oracle" this feeds is an underdetermined single-rod (rod0) regression fit, not evidence-grade; the CONFIRMED constants below (far/scale/aspect/halfWidth/near) stand on their own decomp-sourced evidence, but the oracle they feed into is provisional.

```c
// CONFIRMED constants (use directly in Vulkan projection)
const float FAR    = 2048.0f;   // 0x45000000 — instruction immediate
const float SCALE  = 65536.0f;  // 0x47800000 — instruction immediate
const float ASPECT = 1.0f;      // 0x3f800000 — instruction immediate

// CONFIRMED from static .data (decomp ELF)
const float HALF_WIDTH_43  = 41.6f;  // 0x42266666 @ gp−0x7b80
const float HALF_WIDTH_169 = 0.44f;  // 0x3ee147ae @ gp−0x7b7c
const float NEAR           = 41.6f;  // 0x42266666 @ gp−0x7b78

// RUNTIME-ONLY (BSS in ELF) — FIT in W1, not read (see §7 outcome)
// float FOV = <fit>;   // @ gp−0x73d8 — solved by ProjectionOracleTest from the rod0 oracle
```

---

## 7. Live-read attempt (session 2) — OUTCOME: FOV deferred to the W1 fit `[PROVISIONAL → item 11]`

A second live pass (clock on screen, memory-only, no breakpoints) could NOT read FOV or fresh rods: `[LIVE-VERIFIED]`
- `0x375250` (and `0x377e50`) read **all zeros** — the rod array **heap-shifted after the
  crash+reboot** (the earlier same-session capture at `0x375250` is now stale; rod arrays are per-boot
  heap, FOUNDATION-STATUS).
- `0x2c8b18` (FOV, `gp-0x73d8`) = **0.0** — BSS, computed only during active rod render; at pause all
  11 OSDSYS threads are `status=4` (sleeping between frames), so FOV is unset.
- Registers are unusable: every pause catches the EE kernel idle loop at `PC=0x00081fc0` (zeroed
  context), so `gp` / the live rod-base pointer can't be read to relocate the moved array.

**RESOLUTION — no further live read needed.** `[PROVISIONAL → item 11]` The master plan designs FOV + the projection
column-order as **FIT parameters** (R1/R2), not reads. near/halfWidth/far/scale/aspect are resolved
statically (§6); the rod0 world→screen oracle is already captured (`runtime-trace.md`: world
`(-13.04, 14.67, 50.27)` → screen `(1915.2, 2118.2)` → 12.4 `0x77b3/0x8463`, + rod1-4 X spread). `[LIVE-VERIFIED]` W1's
`ProjectionOracleTest` solves `fov` (1 continuous) + column-order (1 discrete) from that pair. `[PROVISIONAL → item 11]`
This is an underdetermined single-rod (rod0) regression fit — not evidence-grade — per item 11.
**W0 is complete enough to start W1.** (A future boot that leaves the visor rods live at a findable
address can capture spread rods to upgrade the oracle from regression-lock to correctness-proof.)
