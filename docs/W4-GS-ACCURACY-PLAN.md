# W4 — GS Accuracy Plan (PCSX2-guided, pixel-diff-gated)

> Decision (2026-06-13): make the GS replay faithful FIRST (Stage 1 done right),
> then port the real generation logic (W2). Reference = PCSX2 GS source (the logic
> spec) + the captured `.png` next to each `.gs` (the hardware frame, for pixel-diff).

## Where we are
The multi-target replay renders the crystal clock recognizably (rods + glow + orb),
but asset fidelity is incomplete: text/UI render as gray rectangles, the tunnel
background is flat, the orb trail is missing, refraction is approximate. Root cause
is GS-memory/texture faithfulness, not the pipeline (which works).

## Methodology
1. **Spec = PCSX2 GS source** (`C:\CodingProjects\Personal\pcsx2-ref\pcsx2\GS\`). For
   each broken effect, read how PCSX2 does it and align ours.
2. **Measure = pixel-diff** vs the captured `.png` (the real frame). Build a harness:
   render → readback → per-channel mean/max error + heatmap. "Perfect" = diff → 0.
3. **Fix in dependency order** (texture read → unified memory → blend), each verified
   by extracting the asset (`tools/vramdump`) and/or the pixel-diff.

## Comparative findings so far (vs GSLocalMemory.h)
- **Swizzle uses BUFFER WIDTH, not texture width.** PCSX2 `PixelAddress32(x,y,bp,bw)`
  addresses by `bw` (TBW). Our `SwizzleEngine::deswizzle` passes the texture width as
  the stride → wrong whenever `TBW*64 != 2^TW` (e.g. the 1024-wide / 640-buffer cases).
  FIX: add an explicit `bufferWidth = TBW*64` param; iterate `2^TW x 2^TH`, address by stride.
- **PSMCT24/16 have no stored alpha — TEXA expands it.** `Expand24To32`/`Expand16To32`
  use `TEXA.TA0/TA1/AEM`; `ReadFrame24` forces alpha `0x80`. We skip non-PSMCT32
  textures entirely → the display-feedback (PSMCT24) draws render untextured/wrong.
  FIX: decode PSMCT24 (= PSMCT32 address, alpha from TEXA / 0x80) and PSMCT16 if used.
- **Unified memory: textures alias framebuffers.** `tbp0 == fbp*32`; a texture base can
  point at a framebuffer being rendered. PCSX2 has a texture cache resolving this. Our
  feedback path binds the target image, but the sampled region can exceed the 224-tall
  framebuffer (v up to ~470) — the buffer is part of taller VRAM. FIX: model framebuffers
  as regions of one taller VRAM image, or seed/sample with correct stride+offset.
- **TEXA / TFX / COLCLAMP exactness**: fold in once the above land.

## Task order
1. Pixel-diff harness (render→PNG→compare to the `.png`). The measurement gate.
2. Texture read: `bufferWidth` swizzle + PSMCT24(+TEXA). Re-verify assets via vramdump.
3. Unified-memory framebuffer addressing (tunnel bg, orb trail, refraction feedback).
4. Blend/COLCLAMP exactness; drive diff → threshold.
5. THEN W2 (generate geometry; reuse this faithful renderer + extracted assets).

## Notes
- The `.png` reference is 640x480 (NTSC, black borders); diff the active region.
- `tools/vramdump` already extracts/views VRAM regions (PNG via `rgba2png.mjs`).
- Reference files: `GSLocalMemory.{h,cpp}` (read/swizzle/TEXA), `GSState.cpp` (freeze),
  `Renderers/SW/GSDrawScanline*.cpp` (bit-exact blend/dither), `GSRegs.h` (registers).
