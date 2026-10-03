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

| Group | Before | After | Rule | Kind |
|---|---|---|---|---|
| `sprites flat opaque z-always` | 0 / 0 | 0 / 0 | A sprite covers `[ceil(x0), ceil(x1)) x [ceil(y0), ceil(y1))` inside the scissor, with the second vertex's colour and Z (`GSRasterizer.cpp` `DrawSprite`, `GSDrawScanline.cpp` `CSetupPrim`). Already followed. | — |
| `sprites tex(target,texel,region-clamp,bilinear) opaque z-always` (the copies) | 130413 / 2 | 0 / 0 | A sprite's texture coordinate is stepped, not interpolated: `t = UV << 12` (`GSRendererSW.cpp` `ConvertVertexBuffer`, `fst`), less `0x8000` on both corners when bilinear (`GSRendererSW.cpp` `GetScanlineGlobalData`); corners sorted per axis, `step = (t1 - t0) / (p1 - p0)`, start `t0 + step * (first pixel - p0)` in floats, V adds its step once per row (`DrawSprite`); U is `int(start) + int(step * (lane - skip)) + blocks * int(8 * step)` over eight-pixel blocks (`CSetupPrim`, `CDrawScanline`); weights are the four bits under the integer part, `a + ((b - a) * f >> 4)` (`lerp16_4`); a 24-bit texture's alpha is TA0, zero on black (AEM). Our shader took the GPU's interpolated coordinate, which falls a hair below the texel and takes 15/16 of the neighbour. | arithmetic |
