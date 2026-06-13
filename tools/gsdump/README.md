# tools/gsdump

GS-dump parser for PCSX2 `.gs` captures. Phase 1 W1.

Two implementations, byte-identical output:

- **`GsDumpParser.{hpp,cpp}` + `main.cpp`** — the C++ tool (canonical). Pure, zero Vulkan;
  parses a DECOMPRESSED `.gs` into a `gs/GsCommandStream` (resolved per-primitive register
  state). Built as the `gsdump` target.
  ```
  cmake --build build --target gsdump --config Debug
  bin/gsdump <decompressed.gs> [--verify] [--json <out>]
  ```
  `--verify` asserts the known `clock_viewer.gs` invariants (CI/self-test). zstd is out of
  scope — feed an already-decompressed dump (`clock_viewer.gs` is decompressed).

- **`inspect.mjs`** — the Node format-validation pass that grounded the format first. Handles
  `.zst` (`zlib.zstdDecompressSync`), prints the A+D register histogram, emits the full-field
  `clock_prims.json` reference. Kept as the cross-check oracle.
  ```
  node tools/gsdump/inspect.mjs <dump.gs|dump.gs.zst>
  ```

Both validate the dump layout against pcsx2-ref (`GSDump.cpp::AddHeader` / `GSLzma.cpp`) and
walk the GIF command stream byte-exact.

## `.gs` byte layout (new format)

```
[crc u32 = 0xFFFFFFFF][header_size u32][GSDumpHeader 9*u32][serial][screenshot w*h*4]
[freeze state_size bytes][GSPrivRegSet 8192 bytes]
then packets, each [id u8]:
  0 Transfer  [path u8][len u32][data len]   (GIF stream)
  1 VSync     [field u8]
  2 ReadFIFO2 [size u32]
  3 Registers [GSPrivRegSet 8192]
```

GIFtag = 16B: `nloop=a&0x7fff`, `pre=(b>>14)&1`, `prim=(b>>15)&0x7ff`, `flg=(b>>26)&3`,
`nreg=(b>>28)&0xf` (0 -> 16). Payload bytes: PACKED `nloop*nreg*16`,
REGLIST `((nloop*nreg+1)>>1)*16`, IMAGE `nloop*16`. A+D unit: value `[0:8]`, addr byte `@8`.

## clock_viewer.gs findings (the Phase-1 target)

96 primitives, all identical config, GS **context 1**:

- PRIM: `SPRITE`, TME=1 (textured), ABE=1 (blend), FST=1 (UV), IIP=0 (flat).
- ALPHA: `(Cs-Cd)*As/128+Cd` (source-over), FIX=128 unused. One blend mode for the whole clock.
- TEST: alpha-test `GREATER AREF=0` (discard transparent), `AFAIL=KEEP`, ZTE=1 ZTST=ALWAYS (Z off).
- TEX0: two textures — `1024x256 PSMCT32` atlas (76 prims) + `64x64 PSMCT32` (20). TFX=MODULATE, TCC=1.
- TEX1: `MMAG=MMIN=1` -> bilinear filtering.
- FRAME: double-buffer `PSMCT32 FBW=640px FBMSK=0` (FBP 0 / FBP 70).
- SCISSOR: `640x224` per field (448 interlaced).
- **DTHE=0 (dither OFF)**, COLCLAMP=1 (clamp), PABE=0, FBA=0. 32-bit FB -> no bit-truncation.

Consequence: the GS-ify pass for the clock collapses to COLCLAMP only; `gsvk` needs ONE
src-over blend recipe (As/128 scale), not a table.
