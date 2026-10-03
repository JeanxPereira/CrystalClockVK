# Parity baseline, hddosd-110U-whole3-clock frame 0

Produced by bin/ParityTool.exe built from branch feat/native-renderer at 47de6b5 plus the commit that adds this file (Debug, RX 6750 XT). Oracle images are the top-left bounding-box corner of each buffer, so each pass is compared on that corner.

```
group                                                                          passes    pixels    differ   max     depth
lines flat opaque aa z-greater                                                     14    619020      6671   255      3374
sprites flat (Cs-Cd)*As+Cd z-always                                                 1    143360         0     0         0
sprites flat opaque z-always                                                        4    573440         0     0         0
sprites tex(image,texel,clamp,bilinear) (Cs-0)*As+Cd z-always                      28    958958        33     4         0
sprites tex(image,texel,clamp,bilinear) (Cs-Cd)*As+Cd z-always                      1     10600        14     4         0
sprites tex(target,texel,region-clamp,bilinear) (Cs-0)*As+Cd z-always               2    286720      9185     3         0
sprites tex(target,texel,region-clamp,bilinear) opaque z-always                     3    430080    130413     2         0
triangles tex(image,projective,repeat,bilinear) (0-Cs)*As+Cd z-always              24   1004920        15     1      5378
triangles tex(image,projective,repeat,bilinear) (0-Cs)*As+Cd z-gequal              12    502460        97   125      2627
triangles tex(image,projective,repeat,bilinear) (Cs-0)*As+Cd z-gequal              12    502460        79    11      2627
triangles tex(image,projective,repeat,bilinear) opaque z-gequal                     1    143360    118606     3       113
triangles tex(image,texel,clamp,bilinear) opaque z-always                          22    848680         6     2      4952
triangles tex(image,texel,clamp,bilinear) opaque z-gequal                           2    156240         0     0       426
triangles tex(target,texel,region-clamp,bilinear) opaque aa z-always               12    507869      7903   200      2720
triangles tex(target,texel,region-clamp,bilinear) opaque aa z-gequal               24   1015738     10803   237      5265
chained fb0000: 60620 of 143360 pixels differ, largest 255
chained fb1a40: 128220 of 143360 pixels differ, largest 243
chained fb2300: 13 of 143360 pixels differ, largest 2
skipped passes taken from the oracle: 26; validation errors: 0
```

## Groups brought to the software renderer's rule

Each row: the group, its `differ` / `max` before and after, the rule (file, function of `pcsx2/GS/Renderers/SW/`), and the kind of the difference.

Oracle build: SSE4.1 (`_M_SSE 0x401`); rules read from the C path (`CSetupPrim`, `CDrawScanline`) and confirmed equivalent in the JIT (`GSDrawScanlineCodeGenerator.all.cpp` `Init`). The block width (4) is one constant, `GsBlockWidth` in `GsParityRenderer.cpp`, given to the fragment shader as specialization constant 0.

Step 7 deviation (accepted by the controller): instead of flat vertex values with barycentric weights, triangles are walked on the CPU as `DrawTriangle` does, because barycentrics cannot reproduce the per-row `ceil` of the edges and the `prestep` order that set each row's start; the fragment shader then steps each row as `CDrawScanline` does.

| Group | Before | After | Rule | Kind |
|---|---|---|---|---|
| `sprites flat opaque z-always` | 0 / 0 | 0 / 0 | A sprite covers `[ceil(x0), ceil(x1)) x [ceil(y0), ceil(y1))` inside the scissor, with the second vertex's colour and Z (`GSRasterizer.cpp` `DrawSprite`, `GSDrawScanline.cpp` `CSetupPrim`). Already followed. | — |
| `sprites tex(target,texel,region-clamp,bilinear) opaque z-always` (the copies) | 130413 / 2 | 0 / 0 | A sprite's texture coordinate is stepped, not interpolated: `t = UV << 12` (`GSRendererSW.cpp` `ConvertVertexBuffer`, `fst`), less `0x8000` on both corners when bilinear (`GSRendererSW.cpp` `GetScanlineGlobalData`); corners sorted per axis, `step = (t1 - t0) / (p1 - p0)`, start `t0 + step * (first pixel - p0)` in floats, V adds its step once per row (`DrawSprite`); U is `int(start) + int(step * (lane - skip)) + blocks * int(4 * step)` over four-pixel blocks with `skip = left & 3` (`CSetupPrim`, `CDrawScanline`; the four-pixel width was found in the next row's commit); weights are the four bits under the integer part, `a + ((b - a) * f >> 4)` (`lerp16_4`); a 24-bit texture's alpha is TA0, zero on black (AEM). Our shader took the GPU's interpolated coordinate, which falls a hair below the texel and takes 15/16 of the neighbour. | arithmetic |
| `sprites tex(target,texel,region-clamp,bilinear) (Cs-0)*As+Cd z-always` | 9185 / 3 | 0 / 0 | Fixed by the sprite coordinate rule above (same commit); modulate `(tex * c) >> 7` and blend `((A - B) * C >> 7) + D`, alpha written = source alpha (`CDrawScanline`, `modulate16<1>`, the block after `sel.abe`, `mix16(gas)`) were already followed. | arithmetic |
| `sprites tex(image,texel,clamp,bilinear) (Cs-Cd)*As+Cd z-always` | 14 / 4 | 0 / 0 | Fixed by the sprite coordinate rule above (same commit). | arithmetic |
| `sprites tex(image,texel,clamp,bilinear) (Cs-0)*As+Cd z-always` | 33 / 4 (17 / 2 after the copies' commit) | 0 / 0 | The oracle's PCSX2 is built by MSVC without `/arch:AVX2`, so `_M_SSE` is `0x401` (`common/VectorIntrin.h`) and the scanline works in blocks of four pixels (`GSScanlineConstantData128B::m_shift`): U at a pixel is `int(start) + int(step * (lane - (left & 3))) + blocks * int(4 * step)`. The remaining 17 pixels were all at a lane-0 column where blocks of eight truncate differently. | arithmetic |
| `triangles tex(image,projective,repeat,bilinear) opaque z-gequal` (the background), colour | 118606 / 3, depth 113 | 5347 / 2, depth 113 | Gouraud colour is carried as `c << 7` (`ConvertVertexBuffer`), stepped in 16 bits (`CSetupPrim`, `rbf`/`gaf`), and modulated `(texel << 2) * c >> 16` (`CDrawScanline`, `modulate16<1>`); our shader truncated the colour to 8 bits first. | arithmetic |
| the same group, rest | 5347 / 2, depth 113 | 0 / 0, depth 0 | The rest was the GPU interpolating colour, S T Q and depth (3.7% of the group's pixels, above the 1% of Step 7), so triangles are now walked as `GSRasterizer.cpp` `DrawTriangle` (SSE4.1 build) does: sort by y, `dscan`/`dedge` from the negated cross product, one or two sections, per row `[ceil(left), ceil(right))` in the scissor and the row's values `edge + dedge * dy + dscan * prestep`. Each row is drawn as a rectangle over exactly its pixels; the fragment steps the row as `CDrawScanline` does in blocks of four: integers (colour, FST coordinates) are the truncated row start plus the truncated lane step plus truncated block steps, colour wraps in 16 bits and is floored at 0 after each block (`max_i16`); S T Q are floats added once per block and `u = int(s / q)` with a single-precision division rounded to nearest (`divps`, done by long division of the mantissas); depth is a double, `zbase + float(dz) * lane` plus `4 * dz` per block, truncated (`f64toi32`), done with integer arithmetic on the double's bits. | interpolation, now computed per pixel |
| `triangles tex(image,projective,repeat,bilinear) (0-Cs)*As+Cd z-always` | 15 / 1, depth 5378 | 0 / 0, depth 0 | The triangle walk above. | interpolation, now computed per pixel |
| `triangles tex(image,projective,repeat,bilinear) (0-Cs)*As+Cd z-gequal` | 97 / 125, depth 2627 | 0 / 0, depth 0 | The triangle walk above; the large `max` was a pixel whose depth test went the other way. | interpolation, now computed per pixel |
| `triangles tex(image,projective,repeat,bilinear) (Cs-0)*As+Cd z-gequal` | 79 / 11, depth 2627 | 0 / 0, depth 0 | The triangle walk above. | interpolation, now computed per pixel |
| `triangles tex(image,texel,clamp,bilinear) opaque z-always` (orb strips) | 6 / 2, depth 4952 | 0 / 0, depth 0 | The triangle walk above with FST coordinates `UV << 12` less `0x8000`, stepped as truncated integers; clamp `min(max(i, 0), size - 1)` on both taps (`CDrawScanline`, `sat_i16`) was already followed. | interpolation, now computed per pixel |
| `triangles tex(image,texel,clamp,bilinear) opaque z-gequal` | 0 / 0, depth 426 | 0 / 0, depth 0 | The triangle walk above. | interpolation, now computed per pixel |

Groups with `aa` are left to Task 9; the triangle walk changed them as a side effect: `triangles ... opaque aa z-always` 7903 / 200, depth 2720 to 7901 / 200, depth 0; `triangles ... opaque aa z-gequal` 10803 / 237, depth 5265 to 10708 / 237, depth 0; `lines flat opaque aa z-greater` unchanged (lines still use the GPU rasterizer).

After these rows: chained fb0000 7252 of 143360 pixels differ (was 60620), fb1a40 10418 (was 128220), fb2300 0 (was 13); validation errors 0.

Cost of the per-pixel walk (Step 7): the frame's 2377 triangles become 44236 rows, each six vertices of 160 bytes, so the triangle vertex data grows from 0.34 MB to about 42 MB per frame; the fragment shader loops once per block of four pixels to the left of the pixel inside its row (a double addition in 32-bit integers per block). The parity tool over the whole frame (188 passes isolated, then chained, Debug) went from about 2.5 s to about 2.75 s. Moving the row data to per-instance attributes would cut the vertex data by about six times.

## Edge antialiasing (PRIM.AA1)

Measured with edges ignored (Step 1, at 2b8cdc1): `lines flat opaque aa z-greater` 6671 / 255, depth 3374; `triangles ... opaque aa z-always` 7901 / 200, depth 0; `triangles ... opaque aa z-gequal` 10708 / 237, depth 0; chained fb0000 7252, fb1a40 10418, fb2300 0. Per pass the buckets were almost all in the top one (for example draw-054: 876 differing, 836 of them by 16 or more), and every one of those pixels lies outside the interiors this renderer already drew: after the change below the interiors are unchanged (`0x44` with alpha 0x80 writes `Cs`, as before) and the same passes reach 0, so every differing pixel was an edge pixel. The rings and the trails are visible at 1:1, so the rule is implemented (controller ruling), not recorded.

The rule (PCSX2 v2.9.94 `pcsx2/GS/`, read for this table; the full reading is [2026-10-03-aa1-rule.md](2026-10-03-aa1-rule.md)):

- Switch: `GSState.cpp` `IsOpaque` is false whenever AA1 is set and `IsCoverageAlpha` is AA1 on a line or triangle; `GSRendererSW.cpp` then sets `sel.aa1` and takes `ALPHA` as it stands even with ABE 0, and `GSDrawScanline.cpp` blends when `abe || aa1`. Sprites ignore AA1.
- Which pixels, triangles (`GSRasterizer.cpp` `DrawTriangle`, `DrawEdgeTriangle`): the interior as usual, then per triangle the edges v0v1, v0v2, v1v2 of the y-sorted vertices, before the next triangle. Per edge, one pixel per major-axis step from one pixel before the first vertex to one past the second (`ceil(a - 1)` .. `floor(b + 1)`), the first pixel outside the edge on the minor axis, chosen by an integer DDA (`D` in `[-scaleD/2, scaleD/2)`, `scaleD = 512 * |major delta|`) and the `tl`/`side` table, kept only inside the two other integer edge functions (12.4 positions, negated when clockwise, +1 on top-left edges), the scissor and the edge's box (x not widened, y widened by 1).
- Which pixels, lines (`DrawEdgeLine`): every pixel is an edge pixel; endpoints round to the nearest pixel and the diamond rule decides the first and last; per major step the pixel on the line and its minor neighbour toward the line.
- Coverage: triangles `int(0xffff * (1 - distance))` in doubles from `d = float(D) / scaleD`; lines `int(0xffff * |D / scaleD|)` in floats, `0xffff - cov` on the line pixel, `cov` on the neighbour. Alpha is `cov >> 9` (0..127) on an edge pixel and 0x80 inside (`CDrawScanline`, the `sel.aa1` block), replacing the source alpha when ABE is 0.
- Blend: with ABE 0 the blend is forced with `ALPHA`: `((A - B) * As >> 7) + D` with `As` the replaced alpha, so a `0x44` interior writes `Cs` exactly and an edge pixel is a coverage blend; the orb trail's `0x48` is additive weighted by coverage. The frame alpha written is that alpha (FBMSK 0 and FBA 0 on all 50 passes, from `frame.json` and the oracle `context.txt`).
- Depth: edge pixels are tested with the edge's Z and never write Z (`CDrawEdge`: `zwrite = 0`), so an AA1 line writes no Z at all. The edge's values (colour, Z, texture) are the edge line's at the pixel's major coordinate, stepped from the first vertex in floats (Z in doubles) and clamped (`ClampVertex`).
- Interior of a line: none.

| Group | Before | After | Rule | Kind |
|---|---|---|---|---|
| `lines flat (Cs-0)*As+Cd aa z-greater` (orb trails; `opaque` before the fixture carried the forced blend) | 6671 / 255, depth 3374 | 0 / 0, depth 0 | AA1 lines as above: all pixels from `DrawEdgeLine`, additive by coverage, no Z written. | arithmetic (edge pixels), now computed per pixel |
| `triangles tex(target,texel,region-clamp,bilinear) (Cs-Cd)*As+Cd aa z-always` | 7901 / 200, depth 0 | 0 / 0, depth 0 | AA1 triangles as above: interior alpha 0x80, then the three edges' pixels with coverage alpha, per triangle. | arithmetic (edge pixels), now computed per pixel |
| `triangles tex(target,texel,region-clamp,bilinear) (Cs-Cd)*As+Cd aa z-gequal` | 10708 / 237, depth 0 | 4 / 103, depth 0 | The same, plus the coherent images below (663 / 144 without them). The 4 pixels left have a known cause, outside the GS: see below. | arithmetic; remainder is the oracle's texture cache |

Implementation: the edge walk is on the CPU next to the triangle walk (`walkTriangleEdges`, `walkLine` in `GsParityRenderer.cpp`); each edge pixel is a one-pixel row with zero steps whose vertex carries `1 + (cov >> 9)`, emitted after its triangle's rows, so the ordered interlock blends it in the GS order. The brief's `flags.w = 1` and coverage in the vertex stage were not used: `flags.w` already selects the walked rows, and the integer DDA with its float and double coverage is exact on the CPU. The fixture now carries the forced blend for AA1 lines and triangles (`make_fixture.mjs`).

Found on the way, `Fix(Shaders)`: the target and depth storage images were not `coherent`, so a fragment inside the interlock could read a stale cached value of an earlier fragment of the same pixel. Two hundred screen-sized sprites under `Z >=` showed 15 to 25 such pixels per run; in the clock it was 659 of the 663 `aa z-gequal` pixels (edge pixels that passed a depth test against the Z before the neighbouring interior's write). Additive-only tests could not see it, since their order does not matter.

The 4 pixels left, (255,155) and (255,156) of draw-027 and draw-028, are pixels no interior covers, blended only by edge pixels extrapolated one pixel past the vertex (255.8125, 155.5), whose bilinear taps include texel column 255 of `fb2300`. The draw's texture coordinates start at texel 256, and PCSX2's software texture cache converts only the draw's texture rectangle, block-aligned (`GSRendererSW.cpp` `GetTextureMinMax(...).coverage`, `GSTextureCacheSW.cpp` `Texture::Update(rect)`), so column 255 is read from the cache's stale contents (zero here) rather than from memory. Taking texel column 255 as zero reproduces all 4 pixels, every channel, on both passes (for example draw-027 (255,155): ours 110 119 158, with column 255 zero 32 37 55, oracle 32 37 55). This is the emulator's cache, not a GS rule; the real GS reads memory, so the renderer does not imitate it.

After these rows: chained fb0000 2 of 143360 pixels differ (was 7252), fb1a40 2 (was 10418), fb2300 0; validation errors 0. Every group without `aa` stays at 0 / 0, depth 0. The parity tool over the frame went from about 2.8 s to about 3.2 s (Debug).

Deviations from the Task 9 brief (accepted by the controller): (1) the brief's second draw of the edges as lines with `flags.w = 1` and coverage from the vertex stage is replaced by the CPU edge DDA, whose pixels are one-pixel rows in the same ordered draw, after their triangle's rows; (2) Step 1's count of differing pixels on primitive boundaries was inferred, not counted from `worst-*.png`: the interiors draw exactly as before and the passes reach 0 once edge pixels are added, so every differing pixel was an edge pixel. ABE 1 with AA1 on lines or triangles is refused by the fixture (`antialiasing with alpha blending`): the GS then replaces the alpha only where it is 0x80, which the renderer does not do; no clock pass has it (still 26 skips, all the paletted text).

## Task 8b: the whole3-config and whole2-to-clock captures

| Group | Before | After | Rule | Kind |
|---|---|---|---|---|
| `triangles tex(image,projective,repeat,bilinear) (0-Cs)*As+Cd z-gequal` | config 115 / 1, to-clock 71 / 1, clock 0 / 0 (depth 0) | 0 / 0 on all three | PCSX2 rounds an STQ draw's coordinates down before any renderer sees them when the draw is a sprite or every vertex has the same Z (`GSState.cpp` `FlushPrim`, "Texel coordinate rounding", with `CalcMask`; `m_vt.m_eq.z` from `GSVertexTrace`): Q loses its low 8 bits, S and T their low `9 + (exp(max(S or T, Q)) - exp(S or T))` bits, at most 23. The oracle's own vertex dump shows it (draw-175: `Qi 0x3ca36100` where the capture holds `0x3ca36168`). Q 0x3ca36168 against 0x3ca36100 is a relative 3e-5 on S/Q, about 100 of 65536 per texel at u near 48 texels; every differing pixel had a bilinear fraction within that distance above a four-bit weight step (draw-175 (218,160): u fraction 90/4096 over weight 9, the oracle's weight 8 gives texel 128 and Cs 8 instead of 124 and 7). The rule is in the GS state, shared by the scanline's C path and its JIT. | arithmetic |
| `triangles tex(image,projective,repeat,bilinear) (Cs-0)*As+Cd z-gequal` | config 101 / 1, to-clock 138 / 1, clock 0 / 0 | 0 / 0 on all three | The same rule. | arithmetic |

AA remainders on the new captures (whole3-config `aa z-gequal` 6 / 52 in draw-084 and draw-085; whole2-to-clock `aa z-always` 2 / 28 in draw-177 and `aa z-gequal` 2 / 10 in draw-063 and draw-064) have the same cause as the 4 pixels of whole3-clock: PCSX2's software texture cache, not a GS rule. Evidence: a throw-away switch in the parity tool (isolated passes only, reverted) gave each texture that is a target a buffer that starts zero-filled and is updated, per draw, only over that draw's texture rectangle (`GSState.cpp` `GetTextureMinMax(TEX0, CLAMP, linear, true)`: UV range widened by half a texel when bilinear, cut to the region clamp, then aligned outward to 8 x 8 blocks as `GSTextureCacheSW::Texture::Update` does), and sampled that buffer instead of memory. With it every group of all three captures is 0 / 0, depth 0: the 4 + 6 + 4 remaining pixels match the oracle in every channel and nothing else changes. Zero-filling the outside of each rectangle without keeping earlier draws' conversions does not match (it breaks draw-044 of whole3-clock, draw-027 of config, draw-033 and draw-171 of to-clock, whose texels were converted by an earlier draw), which confirms the cache's memory across draws rather than a fixed rule. The renderer does not imitate it.

Task 8b record, in full. Experiment: the switch gave every texture that is a target a zero-initialised buffer, updated per draw only over that draw's `GetTextureMinMax` rectangle (half-texel widening when bilinear, region clamp cut, aligned outward to 8 x 8 blocks as `Texture::Update` does), and sampled it. Result on all three captures: every group 0 / 0, depth 0 (clock `aa z-gequal` 4 to 0, config `aa z-gequal` 6 to 0, to-clock `aa z-always` 2 to 0 and `aa z-gequal` 2 to 0), nothing else changed. Control: plain zero-fill outside each rectangle, with no memory across draws, fixed those pixels but broke clock draw-044, config draw-027, to-clock draw-033 and draw-171. The experiment is not kept in the tree.

STQ rounding implementation: `GsParityRenderer::draw` rounds a copy of the pass's vertices when the rule applies (`roundsCoordinates`, `withRoundedCoordinates`). Test: S 0.75, Q 0x3f8000ff on three vertices with equal Z: Q becomes 1.0, u = 3112960, weight 8, red 120 on the stripes; with a different Z the bits stay, u = 3112864, weight 7, red 135. Diagnosis: a JS recompute of config draw-175 reproduced ours exactly; every differing pixel had a bilinear fraction within about 110/4096 above a four-bit weight step.

Chained after Task 8b: whole3-clock fb0000 2 of 143360 (largest 103), fb1a40 2 (largest 103), fb2300 0; whole3-config fb08c0 0, fb1a40 3 (largest 52), fb2300 0; whole2-to-clock fb0000 0, fb1a40 2 (largest 28), fb2300 0. Validation errors 0 on all three.

Limits of the STQ rule: `m_eq.z` is taken as "every vertex has the same Z"; PCSX2 derives it from a float min/max plus a check of the lowest bit (`GSVertexTrace` `CorrectDepthTrace`), which could call large Z values equal when they differ in bits 1..7. Not met in these captures. The rule is PCSX2's model of hardware ("from hardware tests" in its comment). The remaining chained pixels are the texture cache's; matching them would need imitating the emulator's cache.

## Regenerating the fixtures

Fixtures are git-ignored (`References/fixtures/`). Each is one frame of a Watson capture plus its oracle (PCSX2's software renderer run per draw, `tools/parity/oracle.mjs`), written by `tools/parity/make_fixture.mjs`. From the main checkout (`WATSON_DIST` overrides the Watson build the dump parser is loaded from):

```
node tools/parity/make_fixture.mjs D:/CodingProjects/Watson/Runtime/captures/hddosd-110U-whole3-clock.gs References/fixtures/hddosd-110U-whole3-clock/f0 0
node tools/parity/make_fixture.mjs D:/CodingProjects/Watson/Runtime/captures/hddosd-110U-whole3-config.gs References/fixtures/hddosd-110U-whole3-config/f0 0
node tools/parity/make_fixture.mjs D:/CodingProjects/Watson/Runtime/captures/hddosd-110U-whole2-to-clock.gs References/fixtures/hddosd-110U-whole2-to-clock/f0 0
```

CTest adds a fixture's tests only when its `frame.json` exists; `-DCLOCK_FIXTURE=<dir>` points the clock tests at another location, and the three Parity tests read `${CLOCK_FIXTURE}/../../<capture>/f0`.
