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
  `--verify` asserts the known `clock_viewer.gs` invariants (CI/self-test). `--json <out>`
  writes per-draw state. zstd is out of scope — feed an already-decompressed dump.

  The full per-vertex `clock_prims.json` (Node) is ~7.7 MB and regenerable, so it is
  git-ignored; regenerate it with `inspect.mjs` when needed.

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

## clock_viewer.gs findings (the Phase-1 target, 2 frames captured)

The full vertex-kick decode (NOT just PRE-bit draws) gives **3936 draws / 21200 vertices**
(~1968 draws, ~10600 verts per frame), GS **context 1**. Geometry is dominated by textured
**TRI_STRIP quads** (4 verts each = the prism rods), plus SPRITE and LINE_STRIP.

Draw sizes (nverts: count): `2:288  4:3528  49:56  56:12  58:12  60:40`.

**Three blend modes** (`(A-B)*C/128+D`, all `C=As` so As/128 scaling is universal):

| ABCD | formula | role | n |
|---|---|---|---|
| `(Cs-Cd)*As/128+Cd` | source-over | normal | 2108 |
| `(0-Cs)*As/128+Cd`  | subtractive | shadow/refraction | 1176 |
| `(Cs-0)*As/128+Cd`  | additive    | glow spots | 652 |

Uniform across ALL draws (dump-verified, so they hold for the whole clock):

- **DTHE=0 (dither OFF)** — the GS-ify pass needs no dither.
- **COLCLAMP=1** (clamp overflow).
- **FRAME PSMCT32** (32-bit) — no bit-depth truncation in GS-ify either.
- SCISSOR `640x224` per field (448 interlaced); FRAME FBW=640px; double-buffered.
- Textures all PSMCT32, TFX=MODULATE, TCC=1; TEX1 MMAG=MMIN=1 -> **bilinear**.

Consequence: the GS-ify pass collapses to **COLCLAMP only** (no dither, no 16-bit), but `gsvk`
needs **three blend recipes** (additive/src-over/subtractive), all sharing the As/128 scale.

NOTE: an earlier pass that counted only PRE-bit draws saw "96 uniform src-over sprites" — that
was a small subset. The kick-based decode above is the complete picture.
