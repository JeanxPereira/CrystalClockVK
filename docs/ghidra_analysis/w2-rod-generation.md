# W2 — Per-Frame Rod Generation (Ghidra RE)

> Target: the code that runs **every frame** to move the 12-rod dial + map real time onto it,
> distinct from `ui_render_3d_objects` (the GS-packet emitter, already mined in
> `rod-pipeline.md`). Program: `OSDSYS.elf` (`program="OSDSYS.elf"` on every call, base
> `0x001f0000`). Every claim cites `name@address`; static decompile only (no live PCSX2 this
> session — see §6 for what a live read would settle).
>
> Builds on: `CLOCK-SYSTEM-MAP.md` (anchors), `rod-pipeline.md` (5-pass GS pipeline + struct
> map), `w2-rod-geometry-live.md` (live array read: 16 slots = 12 dial rods + 4 menu cubes,
> struct field meanings), `w0-angle-steps.md` (angle-step constants), `US6693606-DIGEST.md`
> (time→visual semantics, method only).

## 0. Call graph (per-frame path)

```mermaid
graph TD
    MAINLOOP["FUN_00203b78 @ 00203b78 (OSDSYS event loop)"] -->|per vsync| F14438["FUN_00214438 @ 00214438\n(clock per-frame tick, no callers besides main loop)"]
    F14438 -->|"if unaff_s4 != 0"| RENDER["ui_render_3d_objects @ 00223f78\n(angle math + 5-pass GS emit)"]

    ORBTHREAD["clock_orb_rendering_func @ 00211558\n(entry: External — thread-dispatched, no static jal caller)"] --> BCD1["osd_decode_bcd_time @ 00221610\n(decode RTC BCD → 7 fields)"]
    ORBTHREAD --> INITROT["init_rotation_state @ 002216d8\n(MISNOMER — pad/camera helper, not rod init)"]
    ORBTHREAD --> SETCTX["set_render_context_flag @ 0022beb8\n(MISNOMER — generic OS thread-context flag @ 0x3a8bcc)"]
    ORBTHREAD --> CAMIN["update_camera_angles_input @ 00231478\n(pad-driven CAMERA orbit, not the dial)"]
    ORBTHREAD --> VSYNC["gs_sync_v_or_swap_buffers"]
    ORBTHREAD --> BCD2["osd_decode_bcd_time @ 002219d8\n(2nd decode — display-format variant)"]
    ORBTHREAD --> ANIMDT["calc_animation_delta_time @ 00222160\n+ update_time_accumulator @ 00222210\n(generic frame-delta clock, feeds a +0x5200 accumulator)"]
    ORBTHREAD --> ORBIT["FUN_0022eb10 @ 0022eb10\n(light-spot/orb billboard orbit — see orbs-particles.md)"]
    ORBTHREAD --> SCISSOR["FUN_00232028 / FUN_00232330\n(WaitSema + FUN_00241cc0 draw-env/scissor setup)"]

    RENDER -.reads clockState[0]/[1]/[0x1b]/[0x28]/[0x2c]/[0x2d].-> CLOCKSTATE["clockState struct\n(pointer only — owning global NOT located)"]
    RENDER -.reads/writes rod+0x00..0x150.-> RODARR["rod array 0x00375250 (12 dial) / 0x00377e50 (transition, 4 cubes)"]
```

**No function other than `ui_render_3d_objects` (and its Ghidra-split continuation blocks
`FUN_00224630@00224630`, `FUN_002246a0@002246a0`, `module_clock_2384C8@00224a68`) references
the rod-array base addresses `0x375250` / `0x377e50` anywhere in the binary** — confirmed by an
exhaustive instruction-operand search (§5). This is the load-bearing negative result of this
session: the classic "find the generator, walk callers" method fails because **there is no
separate generator function to find** — everything happens inline in the render/emit function.

## 1. `ui_render_3d_objects @ 0x00223f78` IS the per-frame generator (not just the renderer)

Full fresh decompile (steady-state branch, `param_1 == 0.0`, the non-transition path that
actually runs when the clock is idle-displaying):

```c
fVar18 = (float)*param_3 * in_f1;          // shared BASE ANGLE this frame, all rods
...
// else branch (steady state) — ONE rod group only, 0x375250, count = param_3[1]
iVar15 = 0;
FUN_00230fe8(2,1,2); FUN_00230518(2,2); FUN_0022f720(0x375230);
fVar4 = fGpffff832c;                        // 0.20 rad/rod (pass 2, additive)
do {
    if (*(int*)(iVar14 + 0x150) == 0) {                 // face_flag == 0 (front)
        fVar17 = fVar18 + (float)iVar15 * fVar4;
        FUN_00232e38(fVar17, fVar17, iVar14, param_3 + 0x28);   // draw_crystal_rod
    }
    iVar15++; iVar14 += 0x160;
} while (iVar15 < iVar16);
...
FUN_00230518(0,2);
fVar4 = fGpffff8330;                        // 0.40 rad/rod (pass 3, refraction/subtractive)
do {
    if (*(int*)(iVar14 + 0x150) == 0) {
        fVar17 = fVar18 + (float)iVar15 * fVar4;
        FUN_00232e38(fVar17 + (float)param_3[0x2c], fVar17 + (float)param_3[0x2d],
                     iVar14, param_3 + 0x28);
    }
    iVar15++; iVar14 += 0x160;
} while (iVar15 < iVar16);
```

**This loop *is* the position/rotation generation.** There is no earlier "fill the array"
pass — each frame, for each rod `i`, the function computes an angle from scratch
(`base + i*step[+group_offset]`) and hands it straight to `draw_crystal_rod` (rotation +
projection build). The confirmed formula:

```
base       = clockState[0] * in_f1                       // group phase this frame
angle_p2   = base + i * angleStep_p2                      // additive/prism-surface pass
angle_p3   = base + i * angleStep_p3 + groupOffset[A|B]   // refraction/subtractive pass
```

- **Group rotation** = `base` (`clockState[0] * in_f1`), shared by every rod → the whole
  dial turning together.
- **Per-rod own-axis spin** = `i * angleStep` — NOT true own-axis spin in the patent sense
  (S306-308 describes each rod ALSO spinning individually); here it reads as a **fixed
  per-index angular offset that separates pass-2's surface angle from pass-3's refraction
  angle for the SAME physical rod**, producing the glass double-surface look (see §2).
- The **transition variant** (`param_1 > 0.0`, opening-into-clock animation) additionally
  translates the whole group: `FUN_00236a80(0, clockState[0x1b]*26.0*param_1, 0)` — a
  Y-axis push-translate on the matrix stack, scaled by the transition fraction. This is the
  only place a **group translation/orbit** (as opposed to pure rotation) was found.

## 2. Angle-step constants (confirmed, static data — see `w0-angle-steps.md` for full table)

| Global | Ghidra addr | Value | Used for |
|---|---|---|---|
| `fGpffff832c` | `0x002c821c` | **0.20 rad** (11.46°) | steady pass 2 (additive/prism surface), group A only |
| `fGpffff8330` | `0x002c8220` | **0.40 rad** (22.92°) | steady pass 3 (refraction/subtractive), group A only |
| `fGpffff831c/8320/8324/8328` | `0x002c820c..8218` | 0.10/0.10/0.10/~0 rad | transition variant, groups A/B × passes 2/3 |
| `fGpffff8318` | `0x002c8208` | 0.10 | group-transform XY input scale (both variants) |

Pass-3's step is **exactly 2×** pass-2's (0.4 vs 0.2 rad/rod) in steady state — the
refraction pass fans the same 12 rods out twice as far as the additive surface pass,
which is the mechanism that produces the visible prism-edge/glass-refraction offset between
the two draws of each rod, not a literal geometric duplication.

**These are NOT the 12-dial-position spacing** (`360°/12 = 30°`). The live C++ port
(`RodField::Generate`, `src/clock/RodField.hpp`) places the 12 rods at fixed 30°-apart dial
positions and only needs a *global spin phase* per frame — these constants are OSDSYS's
internal per-rod angle applied on top of a shared base, not a substitute for the dial-position
math. Treat `angleStep_p2/p3` as **render-time surface/refraction offset**, not the dial layout
itself (dial spacing is geometry baked at rod-origin time — see §4).

## 3. Time source: `osd_decode_bcd_time` — found, consumer NOT located

`clock_orb_rendering_func @ 0x00211558` (entry point is thread-dispatched — `get_xrefs_to`
returns only `External`, i.e. it's a PS2 thread-proc registered by pointer, not `jal`-called;
consistent with prior docs) calls, in order:

```c
osd_decode_bcd_time(auStack_a0, auStack_60);   // 00221610
init_rotation_state(auStack_a0, auStack_60);   // 002216d8 — MISNOMER, see below
set_render_context_flag(auStack_a0, auStack_60); // 0022beb8 — MISNOMER, see below
update_camera_angles_input();                  // 00231478 — pad camera orbit, not the dial
gs_sync_v_or_swap_buffers();
FUN_002219a0();
osd_decode_bcd_time();                          // 002219d8 — 2nd overload, display-format variant
calc_animation_delta_time();                    // 00222160
update_time_accumulator();                      // 00222210
FUN_0022b1c0(); FUN_0022e910(); FUN_0022eb10();
FUN_002314b0(); FUN_00232028(); FUN_00231a40(); FUN_00232330();
```

`osd_decode_bcd_time @ 0x00221610`:

```c
void osd_decode_bcd_time(uint param_1, uint *param_2)
{
  byte *pbVar5 = (byte *)(param_1 | 0x1224);   // RTC byte source (stat/sec/min/hour/day/month/year)
  int iVar6 = 0xe;
  do {
    param_2[-1] = *pbVar5 & 7;                  // low nibble  (units digit)
    byte bVar1 = *pbVar5; pbVar5++;
    *param_2 = (bVar1 & 0x70) >> 4;             // high nibble (tens digit)
    param_2 += 2;
  } while (-1 < --iVar6);
  // ... then several config-getter calls (FUN_00204418/002043d8/00204458/00204498,
  //     FUN_00231c68/c58/c48/bd8/af8/a48) write into an unrelated persistent struct
  //     at +0x5118..+0x5154 (video/aspect/DVD config bits — NOT rod state)
}
```

This decodes the PS2 RTC's 7 BCD bytes (seconds/minutes/hours/day/month/year + one more) into
15 nibble fields — **this is the real HH:MM:SS source for the clock face.** However, **the
function(s) that read these decoded digits and convert them into "which rod index is `306a`
(the hour rod)" or "how much of it is colored" were not located this session.** All of
`osd_decode_bcd_time`'s own tail calls write into an unrelated config struct (video
mode/timezone/DVD flags), not a rod-highlight field. The consumer must be one of:
- a function inside the still-unclassified `module_clock_<addr>` list (`CLOCK-SYSTEM-MAP.md
  §6`, ~40 entries, most unexamined), or
- baked directly into `+0x150` (face_flag) or a per-rod color-vertex write inside
  `draw_crystal_rod`/`FUN_00232da0` that this session's static reads did not isolate.

**`init_rotation_state @ 0x002216d8` and `set_render_context_flag @ 0x0022beb8` are prior-session
MISNOMERS**, corrected here:
- `init_rotation_state`: body is `fVar1 = FUN_00231a48(); *(int*)(unaff_s0_lo+0x2c) = (int)fVar1;`.
  `FUN_00231a48` is a **pad-input/camera-orbit state machine** (reads `iGpffff8c04/8c08/8c10`,
  writes `fGpffff8bc0` — the same camera-angle global used by `update_camera_angles_input`).
  Nothing here initializes rod rotation.
- `set_render_context_flag`: body is `*(int*)(param_1*0x970 + 0x3a8bcc) = param_2;` — a generic
  per-slot OS/thread context flag array, unrelated to GS render state or rods.

## 4. Rod ORIGIN / dial-position write: NOT FOUND (confirmed absent from all reachable code)

`w2-rod-geometry-live.md` (live PCSX2 read) already showed the rod struct's `+0x00/04/08`
(world origin) values are small and tightly clustered near a shared centre point, and that all
12 dial rods + 4 cubes have **their own distinct `+0x140` unit vector** ("normal", not the
render-time ring angle). This session adds a definitive negative result:

```
search_instructions(operand_pattern="0x5250") → 18 matches, ALL inside ui_render_3d_objects
                                                  or its Ghidra-split continuation blocks
search_instructions(operand_pattern="0x7e50") → 11 matches, same scope (plus 1 unrelated
                                                  gp-relative coincidence in FUN_0020eda0,
                                                  the light-spot writer — different address)
```

No `lui`/`addiu` pair anywhere in `OSDSYS.elf` builds `0x375250` or `0x377e50` outside
`ui_render_3d_objects`. Conclusion: **either the rod origin/normal fields are static data
baked once at load time and never rewritten by CPU code** (most consistent with the small,
near-constant live values already measured), **or they are produced by a mechanism invisible
to literal-address scanning** (e.g. a VU0-macro/scratchpad DMA path). The `FUN_00230e10(0..9)`
×10 calls in `module_clock_init_resources @ 0x00211488` were considered as a candidate
per-slot initializer but the count (10) does not match either 12 (dial rods) or 16 (array
slots) — more likely a 10-digit font/glyph table init (it runs immediately before
`do_load_font`). **This remains the single open item that blocks a fully-grounded origin/
dial-spacing port** — see §6 checklist item 1.

## 5. Vertex-rewrite path (link to `draw_crystal_rod`)

Already fully mined in `rod-pipeline.md §2, §4` and `vu0-math-pipeline.md` — summarized for
this doc's completeness, not re-derived:

- `draw_crystal_rod = FUN_00232e38 @ 0x00232e38` receives `(angleA, angleB, rodPtr,
  matrixSlotPtr)` from the loops in §1. It writes `clockState[0x1b]` (global scale) into
  `rod+0x04` and zeroes `rod+0x08` (W0-1 correction: `+0x04` is NOT the angle, despite the
  field's former name).
- Calls `rotation_build @ 0x002732d8` (2× VOPMSUB cross-product → orthonormal basis, NOT
  Euler/Rodrigues) then `projection_build @ 0x002730a8` (custom GS-native, far=2048,
  scale=65536, aspect=1.0) then `matrix_multiply = sceVu0MulMatrix @ 0x002638a0`.
- `FUN_00232da0 @ 0x00232da0` (thunk → `0x00242ac8`) computes per-rod glow alpha
  (`rod+0x60`) from position index / group / selection state — the after-image trail write.
- Each pass writes a fresh GS A+D header (`DAT_002973a0`/`DAT_002973c0` template blocks) via
  `FUN_0022f720`(begin)/`FUN_00235350`(kick) — one `vkCmdDraw` per rod per pass in the port.

## 6. Port checklist

| # | Item | Status | Notes |
|---|---|---|---|
| 1 | 12 dial positions (even 30° spacing) | **RESOLVED (by design, not by RE)** | OSDSYS's own origin-write path is unlocated (§4); the C++ port (`RodField::Generate`) independently derives 12 evenly-spaced positions from the patent + live screen-space measurements. Do not block on finding the OSDSYS writer — build from the patent-grounded model already in `w2-rod-geometry-live.md`. |
| 2 | Group rotation (shared base angle) | **NEEDS-LIVE-READ** | Formula `base = clockState[0]*in_f1` confirmed (§1); the writer that increments `clockState[0]` per frame was not located. A live watch on `clockState[0]` across consecutive frames (once `clockState`'s address is pinned) settles rotation speed directly — cheaper than more static hunting. |
| 3 | Per-rod pass-offset angle steps | **RESOLVED** | `fGpffff832c=0.20 rad`, `fGpffff8330=0.40 rad` (steady); `831c/8320/8324≈0.10, 8328≈0` (transition). See `w0-angle-steps.md`. |
| 4 | Group translate (transition only) | **RESOLVED** | `FUN_00236a80(0, clockState[0x1b]*26.0*transitionT, 0)`, Y-axis push-translate, transition variant only. |
| 5 | Rotation basis construction | **RESOLVED** | 2× cross-product orthonormal basis, `vu0-math-pipeline.md`. |
| 6 | Projection matrix | **RESOLVED** (minus FOV) | far=2048, scale=65536, aspect=1.0; FOV still needs a live gp-relative read (`w0-projection-constants.md`). |
| 7 | Hour → colored rod index (`306a`) | **NEEDS-LIVE-READ** | `osd_decode_bcd_time` (RTC decode) confirmed as the time source (§3); the consumer that picks the highlighted dial index from the decoded hour digits was not found statically this session. Live: freeze on "Ajuste do Relógio" (per `w2-rod-geometry-live.md`, already shows rod-0 highlighted at 00:00) and diff `+0x150` / a vertex-color field across a manually-advanced hour. |
| 8 | Min/sec → partial color fill on `306a` | **NEEDS-LIVE-READ** | Patent-specified (S304-305) as a per-vertex color RANGE, not a flag; no OSDSYS field for this was isolated. Not `+0x150` (that's front/back face selector, confirmed in `rod-pipeline.md`). |
| 9 | AM=blue / PM=red | **NEEDS-LIVE-READ** | Patent-specified; no OSDSYS color-select code path found this session. |
| 10 | Rod struct field map (0x160 stride) | **RESOLVED** | `rod-pipeline.md §4` + `w2-rod-geometry-live.md` (live-confirmed offsets). |
| 11 | GS packet / blend per pass | **RESOLVED** (blend equations); **NEEDS-LIVE-READ** (exact ALPHA/TEST bit patterns) | `rod-pipeline.md §3, §6`. |
| 12 | Light-spot array (`0x34c830`, 8×0x10) | **RESOLVED** (position formula); separate from rods | `FUN_002354c8`/`FUN_0020eda0`, quadratic-radius polar spread — patent's `308`, not dial rods. |
| 13 | Menu cubes (slots 12-15 of the 16-slot array) | **HYPOTHESIS, not decisively tested** | `w2-rod-geometry-live.md §"CAVEAT to confirm"` — diff slots 12-15 between menu-mode and pure-Visor screenshots. |

### Fastest remaining unblock (if a live PCSX2 pass becomes available)

1. Pin `clockState`'s real address (watch what pointer lands in `$a2`/`$s2` at the
   `jal ui_render_3d_objects` call site, `0x002144a4`) → then watch `clockState[0]` across
   frames (rotation speed) and `clockState[0x2c]/[0x2d]` (pass-3 per-group angle offsets).
2. With clockState pinned, diff its bytes across a manually-advanced RTC hour (via "Ajuste do
   Relógio") to find the hour→highlight write — likely a small integer field, not necessarily
   inside the 16-slot rod array at all.
3. Only then chase item 4 (origin/dial-spacing writer) if still needed — low priority since
   the C++ port already has a working, patent-grounded 12-position generator independent of
   finding OSDSYS's exact mechanism.

---

## §7 LIVE READS (2026-07-03 session, PCSX2 DebugServer, BIOS booted to Visor)

- BP @ 0x002144a4 (jal ui_render_3d_objects) FIRES per frame on the clock screen. At entry:
  gp=0x002CFEF0 (matches doc), a2=0x006800B0 (small flag struct, not clockState),
  v0=v1=0x002C4180 (a STATIC JUMP TABLE containing 0x2144a4 etc — not state).
- BP @ 0x00221610 (osd_decode_bcd_time entry): a0=0x0036C780 (ctx ptr; RTC raw @0x0036C880),
  a1=dst=0x0036C7C0 (stack; consumed within the frame — read at entry shows stale/zero).
- **GROUP ROTATION PHASE FOUND (at decode-entry stack snapshot): float @ 0x0036C7D0**,
  decreasing by **-0.001671 rad/frame ≈ -0.1 rad/s** (constant across 3 samples;
  1.64722 → 1.64554 → 1.64388). Neighbouring floats: @+4 a slow-increasing value ~13.03;
  @+0x10 the projection constants block (2048.0 far, 65536.0 scale — matches W0 ✓).
  NOTE: this is a per-frame STACK slot along the decode call path (stable per frame,
  not a persistent global) — the persistent phase global still unlocated.
- NEXT (hour→rod): diff a persistent BSS window across a manual hour advance
  ("Ajuste do Relógio"): candidate windows 0x34C000+4K (near light-spot array),
  0x375000+4K (rod array neighbourhood), 0x2C7000+4K (gp data). Seconds-field can be
  isolated first by diffing the same windows across ~2s of free run (no user input).
