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
