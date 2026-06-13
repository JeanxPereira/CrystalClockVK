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

## Progress / learnings (2026-06-13)
- DONE: swizzle-by-buffer-width + PSMCT24 decode + PSMCT24-feedback routing → the
  radial **tunnel background now renders** (was flat purple). Committed 86fa888.
- The remaining broken assets (text, orbital rings, exact refraction) are the
  **unified-memory feedback**: feedback textures sample v up to ~470, into the VRAM
  rows BELOW a 224-tall framebuffer, where the glyph/sprite **atlas** lives (verified:
  `tools/vramdump` of FBP 280 as 640×512 shows button glyphs + text + sprites at rows
  224+). Two naive fixes were tried and REVERTED:
  - seed-from-freeze (init the 224 target from VRAM): no visible text gain.
  - tall 640×512 targets (let feedback read the atlas rows): **regressed** — the
    scattered atlas pixels bleed onto the rods as colorful noise.
  Conclusion: faithful feedback needs the proper VRAM model (PCSX2 texture-cache
  style: VRAM as one memory; textures/framebuffers are regions; reads resolve to the
  current contents with correct stride+offset, NOT separate clamped 224 images).
  This is the substantial GS-core mile.

## Decision: A then B (2026-06-13)
"Style IS the GS rasterizer", so faithful refraction/glow/blend are the clock's
VISUAL STYLE, not just UI. Build the faithful GS memory model (A) first; W2 (B)
then feeds generated geometry into the SAME renderer — inherently hybrid (A is B's
backend; the GS style carries into B).

## A — VRAM texture-cache model (the root fix)
1. **VRAM = one mutable buffer** (4MB, init from freeze) — source of truth.
2. **Deswizzle on-demand**: before sampling a texture (TBP0/TBW/PSM), decode the
   VRAM region → linear VkImage (SwizzleEngine math).
3. **Write-back (swizzle)**: after rendering a framebuffer, re-encode it into VRAM,
   so later texture reads crossing that region see current content (kills the bleed).
4. **Ordered replay** with deswizzle-before-read / swizzle-after-write → refraction,
   trail, and the glyph atlas resolve correctly.
Impl options: GPU (2 compute shaders, port SwizzleEngine address math to GLSL) or
CPU (per-run readback + re-swizzle, render once & cache — simpler, slower, fine for
a static dump). First verifiable step either way: a deswizzle that matches
`tools/vramdump` output, then wire write-back into the replay.

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
