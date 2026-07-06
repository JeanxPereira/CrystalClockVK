# SP0 Live Reads — session 2026-07-05 (PCSX2 DebugServer, BIOS ps2-0230a)

> All values below are [LIVE-VERIFIED] this session unless noted. Emulator state at
> capture: OSDSYS running, NOT on the crystal-clock screen yet (rod array zeroed),
> EE paused in idle thread (PC=0x00081fc0) — memory reads valid.

## ⭐ THE 0x2738a0 WALL IS DOWN — it's the sceVu0 macro lib [LIVE-VERIFIED]

Native PCSX2 disassembly of live RAM (perfect decode, no Ghidra involved):

```
0x002738a0  sceVu0MulMatrix(dst a0, m a1, src a2):
  lqc2 vf04..vf07, 0x00/10/20/30(a1)      ; load matrix m rows
  li   a3, 4
loop:
  lqc2 vf08, (a2)                          ; src column
  vmulax.xyzw  ACC, vf04, vf08x
  vmadday.xyzw ACC, vf05, vf08y
  vmaddaz.xyzw ACC, vf06, vf08z
  vmaddw.xyzw  vf09, vf07, vf08w
  sqc2 vf09, (a0)
  addi a3,-1; addi a2,+0x10; bnez a3,loop; addi a0,+0x10
  jr ra

0x002738e8  sceVu0ApplyMatrix(out a0, m a1, vec a2):
  same 4 lqc2 + single vmulax/vmadday/vmaddaz/vmaddw + sqc2, jr ra
```

Consequences (correct PORT-FUNCTION-MAP.md in place):
- `draw_crystal_rod @0x232e38` tail-jump `FUN_002738a0(0x29bd90, 0x29bd50, 0x29bd10)`
  = **compose 0x29bd90 = projection(0x29bd50) × rotation(0x29bd10)**. It does NOT
  transform vertices and does NOT emit GS packets. The "transform+emit crux" label is
  [FALSIFIED].
- `FUN_00232da0` tail-call `FUN_002738e8(&stack0x20, rod+0x30, rod+0x10)` = apply the
  matrix at rod+0x30 to the vector at rod+0x10 (one point). Also not an emit.
- The Ghidra "misclassified data table / halt_baddata" diagnosis is [FALSIFIED]: the
  region is COP2 macro-mode code (lqc2/sqc2/vmul*), which this Ghidra language config
  fails to decompile. The "smooth monotonic cop0/cop1 operands" observation does not
  match live bytes.
- **The REAL vertex→GS-packet emitter is still unlocated.** Prime candidate: the body
  of the live-verified per-frame rod render `0x00232618` (live regs there: s0 =
  0x20297220 uncached GS packet buffer, v1 = 0x1000A000 VIF1 FIFO). NEXT: disassemble
  0x232618's body live and/or watchpoint-write on the packet buffer.

## GS register templates [LIVE-VERIFIED] (static data, screen-independent)

```
DAT_002973a0: 00 80 00 00 00 00 00 c4  80 18 43 43 43 43 00 00
DAT_002973b0: 00 80 00 00 00 00 00 a4  10 43 43 43 43 00 00 00
DAT_002973c0: 00 80 00 00 00 00 00 e4  80 12 24 41 12 24 41 00
DAT_002973d0: ff 00 00 00 ff 00 00 00  ff 00 00 00 80 00 00 00
```
(0x...c4/a4/e4 low-dword pattern = VIF/GIF-tag-shaped; cross-check against the dump's
per-pass ALPHA/TEX values is still TODO — do NOT mark that correspondence verified yet.)

## FOV gp-globals [LIVE-VERIFIED addresses, values captured]

gp (live) = 0x002CFEF0 → `uGpffff8480` = 0x002C8370, `uGpffff8484` = 0x002C8374.
```
0x002C8370: 0x3ef0a3d7 = 0.4700f   (FOV/projection param, 4:3 slot)
0x002C8374: 0x3f0a5e35 = 0.5405f   (16:9 slot)
0x002C8378: 0x4b7fffff = 16777215.0f
0x002C837C: 0x461c4000 = 10000.0f
```
[HYPOTHESIS] 0.47 / 0.5405 are the FOV parameters fed to projection_build
(FUN_002730a8); semantic (tan half-angle vs focal) to be settled when the projection
is re-derived in SP1.

## Packet/DMA machinery — REAL leads found [LIVE-VERIFIED disasm, roles HYPOTHESIS]

- `0x00232618` (live-verified per-frame rod render entry) is SMALL: builds a rect in
  12.4 fixed point (the `<<4` shifts) into uncached buffer `0x20297220` (+0x20..0x2c),
  `jal 0x00230518`, then tail-jumps `j 0x0022FD00` with a0 = the buffer.
- `0x0022FD00` = the packet-flow spine: `F720(sp)` → `FB28(sp,buf)` → `F7F8(sp)` →
  `F720(sp)` → `FBE8(sp,buf)` → `F7F8(sp)`. [HYPOTHESIS] begin → append → submit, ×2.
  The old "FUN_0022F720 is browser icon-selection logic" decompile is almost certainly
  another WRONG-BOUNDARY artifact — here it initializes a packet context on the stack.
- Helpers at `0x0022fd58+`: write a GIF-tag-shaped 0x4C word, `lq/sq` copy 16-byte
  templates from `0x00296dd0/0x00296de0`, and maintain a linked list via `(a0)`/`+0x14`
  — a **DMA-chain builder**. This family (0x22F720/0x22FB28/0x22FBE8/0x22F7F8 +
  0x22fd58 helpers) is where the vertex→GS-packet emit lives. SP1 decompiles it from
  live RAM/native disasm, NOT from the stale Ghidra DB.
- `0x0026753c` ("fails to decompile / halt_unimplemented") is [FALSIFIED]: it is a
  plain 128-bit `memcpy(dst, src, n)` (lq/sq loop). `FUN_00230518`'s chain therefore
  ends in a staging-buffer copy (dst 0x01860200), not a GS kick.

## Clock-screen capture DONE [LIVE-VERIFIED] (2nd half of session, clock on screen)

- **Full EE RAM image**: `re/ram/clock/eeMemory.bin` (32MB, gitignored) + vu0Memory
  + screenshot, extracted from savestate slot 8 (`...08.p2s`, zstd-in-zip → 7-Zip).
  Byte-verified against live reads at 0x2738a0 (sceVu0 code) and 0x375250 (rod data).
  Backup pre-clock state in slot 9.
- **Rod array 0x375250 is NOT an array of 0x160 rod structs as documented** —
  [FALSIFIED, layout richer]: live data shows groups of 4 vertex records of 0x50
  bytes each (per record: +0x00 world xyz float, +0x10 two floats (u,v / phase),
  +0x20 screen XY float pair ~1900/2100 range, +0x28 z-ish float, +0x30 w≈1.0,
  +0x34/38 the SAME screen coords as 12.4 ints (0x772b=30507→1906.7 ✓), +0x3c
  0xfffff010, +0x40 pass/flag 0x0f|0x10), followed by a 0x50 normal/terminator
  record (unit vector + count). I.e. the TESSELATED QUADS live here, world+screen,
  ready to correlate with prims_sw.json. Second population at ~0x375a90+ with larger
  coords (menu cubes / other group). Full layout analysis = SP1, from the RAM image.
- **Per-frame chain re-confirmed live**: BP at 0x232618 fired; backtrace
  `0x00233928 → 0x00232618` matches [[live-render-chain]].

## Light spots — updater does NOT run on the clock screen [LIVE-VERIFIED]

- Exec BP at `FUN_0020eda0` (0x0020eda0): **never fires** on the crystal-clock
  screen, while a control BP at 0x232618 fires immediately (BPs proven working).
- `0x34c830` content is static idle data `{0, 0, 1160.0, 0} ×2` across reads;
  read/write watchpoints (cached + uncached mirror) never trigger.
- Consequence: the claim "FUN_0020eda0 runs once per frame from 0x211490"
  is [FALSIFIED for the clock screen] — that updater belongs to another
  screen/mode (candidates: version-info/Visor orb variant, or transition states).
  The clock screen's spot/orbit animation must come from the FUN_002354c8-family
  (parameterized copy) instead. Re-hunt in SP1 with the right mode active.
