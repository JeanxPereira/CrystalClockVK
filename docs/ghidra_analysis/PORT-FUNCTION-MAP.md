# Crystal Clock — Port Function Map (evidence-based)

> Goal: generate the clock's GS command stream procedurally (no dump), feeding the
> SAME faithful GsRenderer that already replays dumps at 5–10% pixel-diff. This map
> is the complete inventory of OSDSYS functions to port, each VERIFIED in Ghidra
> (program=OSDSYS.elf, base 0x001f0000). Status: RESOLVED / PARTIAL / UNVERIFIED.
>
> **This replaces the invented procedural look (prism/tunnel/spots shaders were
> guesses, not read from the GS — deleted approach). Nothing here is assumed;
> every entry is decompiled.**

## Per-frame render spine (VERIFIED 2026-07-03, direct decompile)

`ui_render_3d_objects @ 0x00223f78(float param_1 transitionT, param_2, int* clockState)`
is the whole per-frame draw. `clockState[0]` = rotation phase; `[1]` = rod count;
`[0x1b]` = transition trigger; `[0x28..]` = pass-2 matrix ctx; `[0x2c]/[0x2d]` =
pass-3 per-group angle offsets. Rod arrays: **0x375250 (group A) + 0x377e50 (group
B)**, stride **0x160**, field **+0x150** = front/back-face pass selector.

Base rod angle: `fVar18 = clockState[0] * in_f1`. Steady-state (else branch) draws
these passes IN ORDER, each = {set GS state} → {loop rods, emit} → {kick}:

| # | Pass | GS-state call | Emit call | Angle step |
|---|------|---------------|-----------|-----------|
| 1 | surface base | `FUN_002324e8(1,0,1)` + `FUN_00230518(1,1)` | `FUN_00232da0` (rods +0x150==0) | — |
| 2 | additive | `FUN_00230fe8(2,1,2)` + `FUN_00230518(2,2)` | `FUN_00232e38` (draw_crystal_rod) | `fGpffff832c=0.20` |
| 3 | refraction | `FUN_00230518(0,2)` | `FUN_00232e38` | `fGpffff8330=0.40` + `clockState[0x2c/0x2d]` |
| 4 | back surface | `FUN_00232538(1,0,1)` + `FUN_00230518(1,2)` | `FUN_00232da0` (rods +0x150!=0) | — |
| 5 | back highlight | `FUN_002324e8(0,1,1)` + `FUN_00230518(1,2)` | `FUN_00232da0` (…, 0xff) | — |

Before each emit loop the caller writes a **GS register template** into the packet
buffer at **0x375230** via `FUN_0022f720(0x375230)`:
- surface passes use template `DAT_002973a0` (16B: a0/a8/ac).
- rod passes use template `DAT_002973c0` (16B: c0/c4/c8/cc).
These are the per-pass GS blend/tex/test register values — the "style", already
seen byte-for-byte in the dump. `FUN_00235350()` kicks the built packet to the GS.

## Rod draw — `draw_crystal_rod @ 0x00232e38` (VERIFIED)

Calls exactly three, then the built vertices go to the GS packet buffer:
- **`FUN_002732d8(0x29bd10, 0x29bcf0)`** = rotation_build — 2× cross-product
  orthonormal basis. Ported: `ps2clock::BuildRotationMatrix` (Projection.cpp). PARTIAL
  (handedness flagged in RotationBasisTest).
- **`FUN_002730a8(fov, 1.0, fovVariant, 2048.0, 2048.0, 1.0, …, 0x29bd50)`** =
  projection_build — GS-native perspective embedding the viewport. Constants confirmed
  live here: `0x45000000`=2048.0 (far/scale), `0x3f800000`=1.0 (aspect); FOV =
  `uGpffff8480` (4:3) / `uGpffff8484` (16:9), selected by `iGpffff8d18`. Ported:
  `ps2clock::BuildProjectionMatrix`. PARTIAL (FOV gp-global still needs a live value).
- **`FUN_002738a0(0x29bd90, 0x29bd50, 0x29bd10)`** = transform+emit — applies the
  matrices to the rod's model vertices and appends GS XYZ/UV/RGBA to the packet.
  UNVERIFIED internals (the actual vertex→GS-packet write). NEXT to decompile.

## TODO — remaining functions to decompile & document (this session)

Each gets: signature, role, callees, port-status. Grouped:

- **GS state setters** (blend/test/env per pass): FUN_002324e8, FUN_00230518,
  FUN_00230fe8, FUN_00232538 — likely wrap pktSetAlphaBlend/pktSetTEST/pktSetAD.
- **Packet/DMA**: FUN_0022f720 (template blit into 0x375230), FUN_00235350 (kick),
  the 0x375230 buffer + DMA path to VIF1/GIF.
- **Surface pass draw**: FUN_00232da0 (the +0x150 face pass, takes an alpha arg).
- **Transform emit**: FUN_002738a0 internals (vertex → GS packet).
- **Geometry setup**: FUN_002335e8 (reads rod array + clockState → screen scale
  fStack1c0/1c4 × fGpffff8318), FUN_002367c0 (matrix ptr), FUN_00236a80 (Y translate).
- **sceVu0* math lib**: the transform/clip/FTOI primitives the above use.
- **Light spots**: FUN_002354c8 (updates 0x34c830, 8×0x10) + its draw (FUN_0020eda0).
- **Swirl / orb wireframe**: (locate — the LINE_STRIP curves).
- **Text**: the glyph-sheet build + PSMT4 font blit + sprite emit.
- **Time**: osd_decode_bcd_time @ 0x00221610 (RTC 7-byte BCD → 15 nibbles). RESOLVED.
- **Rod-array generation**: who writes rod +0x00 world origin / +0x140 normal
  (unlocated statically per §4 of w2-rod-generation.md — may need a live read).

## Port target
Generated geometry (the 12-rod dial we have) → these ported passes emit a GS command
stream (GsPrimitive list with the REAL register templates + textures 11520/11200/…)
→ existing GsRenderer draws it faithfully. Validate: render generated clock at a
fixed time, pixel-diff vs GSRunner (the same gate, must reach the dump's 5–10%).
