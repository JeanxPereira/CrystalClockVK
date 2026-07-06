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

## Rod arrays — NOT captured yet

0x00375250 read all-zero: the crystal-clock screen was not active at capture time.
Rod structs, packet buffer 0x375230 head, and the full RAM image require the clock
on screen (user navigation) — pending in this session.
