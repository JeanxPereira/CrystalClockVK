# CrystalClockVK — Memory & Ground Truth

Master architectural brain for the rewrite. Detail lives in the authoritative docs; this file is
the durable context + pointers. Read entirely before touching code.

## 0. Authoritative docs
- `docs/OSDSYS-DECOMP-1to1-STRATEGY.md` — master strategy.
- `docs/FOUNDATION-STATUS.md` — verified tooling + live-trace corrections.
- `docs/PHASE0-AUDIT.md` — KEEP/AMPUTATE/REBUILD audit.
- `docs/superpowers/specs/2026-06-12-phase1-gs-vk-architecture-design.md` — Phase 1 design.

## 1. The core insight (why the rewrite)
The distinctive look is the **GS rasterizer**, not modern shading. Earlier Raylib/OpenGL and the
first Vulkan attempt guessed the look (patent-derived rotation, hand-tuned colors, ad-hoc blend) →
"too modern". The GS behavior is **encoded in the decomp's GS-packet builders** and must be
replicated in shaders driven by decoded state. Refraction = framebuffer feedback (render bg, then
sample it distorted under the crystal) — proven by patent US6,693,606 and the decomp.

## 2. GS facts (live-verified, no guessing)
- Blend: `Cv = ((A-B)*C >> 7) + D`. Alpha is 0–128 (128 = "1.0"). Rounding bias +0x7F. Seen
  directly in the decompiled rod render (the `>> 7`, `+0x7f`, alpha clamp to 128).
- Vertex coords: 12.4 fixed-point (`*16`) with +2048 XYOFFSET. → `GS_FIXED_POINT_SCALE = 16`.
- Clock/opening use the `sceVu0*` MACRO lib only — zero VU1 microcode, zero VU0 micro mode.

## 3. Live PCSX2 trace — ground truth (2026-06-12, BIOS ps2-0230a-20080220)
- **Real per-frame rod render = `0x00232618`** (NOT the old docs' `0x00225E80`, which is
  builder/init and does not execute per-frame). Backtrace:
  `0x00221408 → 0x00221060 → 0x00221558 → 0x0022e738 → 0x0022c8d0 → 0x00233928 → 0x00232618`.
- Live regs at the hit: `s3 = 0x00375250` (ROD_GROUP_A ✅), `s0 = 0x20297220` (GS packet buffer,
  uncached), `v1 = 0x1000A000` (VIF1 FIFO), `gp = 0x002CFEF0`.
- Rod struct: stride **0x140**, selection flag at **+0xF0**, glass color RGBA(45,87,102,128) at
  +0x90, additive RGBA(60,60,60,128) at +0xD0.
- **WARNING:** GP-relative ABSOLUTE addresses from any prior session are stale (gp shifts per
  boot, e.g. 0x002AF070 → 0x002CFEF0). Only OFFSETS are stable; re-resolve absolutes from live gp.
  Per-pass specular angle steps ≈ 0.26 / 0.33 rad (the smaller shimmer steps, not 30°/rod).

## 4. Ground-truth artifacts
- `C:\Users\dell04\Documents\PCSX2\snaps\clock_viewer.gs` (6.17 MB) — the crystal clock frame
  (Phase 1 target) + matching `.png` reference. `config_menu.gs` = config screen (deferred).
- Decompress `.gs.zst` with Node 26 `zlib.zstdDecompressSync` (no zstd CLI / python-zstandard here).

## 5. Phase 0 reset (2026-06-12)
Evidence-based audit + surgical amputation executed (branch `phase0-surgical-cuts`). Deleted all
look-guess code (patent rotation chain, lerpPrismColor, Raylib shaders, BlendMode table, 2-target
refraction ping-pong) and poison docs; kept Vulkan/SDL/GS-memory plumbing. `GsConstants`/
`GsRegisterState` rebuilt with live values; `RenderOrchestrator` stubbed; `PipelineBuilder` →
`setBlendState`. App intentionally renders black + ImGui until the Phase 1 GS-spec rebuild lands.

## 6. Conventions
- Commits: `Type(Scope): subject` — **never** a `Co-Authored-By` trailer.
- `gs/` is Vulkan-free. ghidra clock decompiles need `program="OSDSYS.elf"`. Compare by pixel-diff,
  never by photo.
