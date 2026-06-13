# OSDSYS Clock — Live PCSX2 Runtime Trace (captured 2026-06-13)

> Runtime values read live from PCSX2 (DebugServer) with the crystal clock on screen. These fill
> the runtime-numeric gaps the static RE could not (see CLOCK-SYSTEM-MAP §7). All addresses are
> **EE runtime (cached RAM)** unless noted. Memory reads are live & valid; **register reads were NOT**
> (returned a zeroed/halted snapshot — gp=0, pc=0x81fc0) so gp-relative globals (FOV/near, orbit
> angle) are still pending. Setting an exec BP at `0x00232618` dropped the DebugServer (PCSX2 crash);
> retry with memory-only reads or a save-state, or extract those statically (projection_build).

## GS packet templates (the 5-pass blend/test state)

### Rod templates @ `0x002973a0` (64 bytes, u32)
```
0x2973a0: 00008000 c4000000  43431880 00004343   <- DAT_002973a0 group
0x2973b0: 00008000 a4000000  43434310 00000043
0x2973c0: 00008000 e4000000  41241280 00412412   <- DAT_002973c0 group
0x2973d0: 000000ff 000000ff   000000ff 00000080   <- RGBA = (255,255,255,128)
```
- Recurring `0x00008000` low word + `0xc4/0xa4/0xe4 000000` high → A+D register VALUE qwords
  (`0x..000000_00008000`). The `0xc4/0xa4/0xe4` selects the blend/test variant per pass.
- Trailing `ff ff ff 80` = vertex color white, **alpha 0x80 = 128** (the GS additive/full alpha).
- TODO: decode each qword as its GS A+D register (ALPHA 0x42 / TEST 0x47) — the bytes encode A,B,C,D.

### Orb templates @ `0x00297420` (64 bytes, u32)
```
0x297420: 00008000 24000000  00000010 00000000
0x297430: 00008000 24000000  00000041 00000000
0x297440: 00000080 00000080  00000080 00000080   <- color (128,128,128,128) additive
0x297450: 000000ff 000000ff   000000ff 00000080
```
- `0x24000000` high word = the orb blend variant (additive). Colors all `0x80` = the glow tint.

## Orbit integrate function table @ `0x0029b3c0`

```
[0] = 0x00239440   [1] = 0x00238d60   (rest zero)
```
Two function pointers — the orbit/position integrate dispatch (called via `module_clock_22F5D0`).
**→ decompile these two statically in Ghidra** (no live emu needed) to recover angular velocity /
radius / tilt math. This was the orbs doc's primary blocker.

## Menu layout / display list @ `0x00274c00` (256 bytes, u32)

A command list (NOT a flat XY table), 16-byte records `[tag][ptr][0][0]`:
```
30000000 00100000 ....   <- tag 0x30000000, arg 0x00100000 (separator/blank?)
3000003c 00275c90 ....   <- tag 0x3000003c -> ptr 0x00275c90  (entry, count 0x3c)
3000000e 00277230 ....   <- tag 0x3000000e -> ptr 0x00277230
30000000 00100000 ....
70000000 00000000 ....   <- tag 0x70000000 = END/group marker
... pattern repeats (3 groups seen): ptrs 0x276050/0x277310, 0x276490/0x2773f0
```
- Tag hi-nibble `0x3` = draw/item, `0x7` = end. The `0x002760xx`/`0x002772xx` pointers are the
  per-item data (strings/positions). **→ follow 0x00275c90 etc.** for the actual XY/glyph records.

## Rod array @ `0x00375250` (ROD_GROUP_A, live-confirmed s3)

Records repeat every **0x50 bytes** (20 u32). The struct stride is 0x160 (from the render loop), so
each rod likely holds **current + 2 history snapshots** (0x50 each = after-image trail) + tail.
Decoded **rod 0** (first 0x50):
```
+0x00  c150a0c4  -13.039   (world X)
+0x04  416aaa34   14.666   (world Y)        [render loop reads angle at +0x04 — verify]
+0x08  42491571   50.271   (world Z / radius)
+0x0c  00000000    0.0
+0x10  3f800000    1.0      (scale.x)
+0x14  3f800000    1.0      (scale.y)
+0x18  00000000    0.0
+0x1c  00000000    0.0
+0x20  44ef6652   1915.20   (screen X, float)
+0x24  4504634e   2118.20   (screen Y, float)
+0x28  c37f00fd   -255.0    (screen Z / depth)
+0x2c  3f7fffff    ~1.0     (W)
+0x30  000077b3            12.4 fixed screen X  (0x77b3/16 = 1916.19) <- matches +0x20
+0x34  00008463            12.4 fixed screen Y  (0x8463/16 = 2118.19) <- matches +0x24
+0x38  fffff010            12.4 fixed Z
+0x3c  0000000f
+0x40  3ca2f4fc    0.0199  (small float — perspective 1/w?)
+0x44..+0x4f  0
```
Subsequent rods: rod1 X=-13.347, rod2 X=-7.916, rod3 X=-8.224, rod4 X=0.234 … → the radial ring
world positions. Screen Y ~2118 for all (they sit on a ring). **This array IS the rod geometry the
procedural port must reproduce** (world XYZ + scale → project → 12.4 fixed screen coords).

## Still pending (need a stable live read or static extraction)

- **Projection FOV** `gp[-0x73d8]` + **near** `gp[-0x7b78]`: need valid `gp` (register read failed).
  Alternative: disassemble `projection_build @ 0x002730a8` to see if loaded as immediate vs gp-data.
- **Orbit angle/velocity globals** `fGpffff8b88/8464/8bc0`: dynamic; the constants live in the two
  fn-table funcs above (static-decompilable).
- **Config storage** base addr: disassemble `module_clock_get_config_item @ 0x00221540`.
- Decode the GS A+D qwords above into explicit ALPHA/TEST register fields (A,B,C,D / ATST/ZTST).
