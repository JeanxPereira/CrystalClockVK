# CrystalClockVK — PS2 OSDSYS Visual Layer, 1:1 in Vulkan

## Project Overview
Perceptually 1:1 Vulkan recreation of the PS2 OSDSYS VISUAL layer (crystal clock first; config
menu and opening later). The style IS the GS rasterizer — blend `(A-B)*C/128+D` (alpha 0–128),
ordered dither, framebuffer-feedback refraction, additive glow, 12.4 fixed-point coords (×16) —
replicated IN SHADERS from GS state decoded out of the byte-perfect OSDSYS decomp. Never guess
the look; read it from evidence.

## Read First
1. `docs/OSDSYS-DECOMP-1to1-STRATEGY.md` — master strategy (spec = decomp; precision = GS dumps + pixel-diff).
2. `docs/FOUNDATION-STATUS.md` — verified tooling state + live-trace corrections (ground truth).
3. `docs/superpowers/specs/2026-06-12-phase1-gs-vk-architecture-design.md` — approved Phase 1 architecture.
4. `docs/PHASE0-AUDIT.md` — what was cut and why.
5. `MEMORY.md` — architectural history + ground-truth pointers.

## Architecture (strict layers)
1. `core/`: Vulkan bootstrap (vk-bootstrap), SDL3, RenderDoc hook.
2. `renderer/`: Lean generic Vulkan 1.3 wrapper (VMA, PassRecorder, PipelineBuilder with
   `setBlendState`, dynamic rendering, `local_read`).
3. `gs/`: Pure PS2 logic — NO Vulkan symbols. GsState structs, SwizzleEngine, TextureDecoder,
   VramBuffer, VU0 math. Unit-testable.
4. `gsvk/` (Phase 1): the GS→VK translator — GsState in, VK pipeline/blend/shader config out.
   The only place GS and VK meet.
5. `app/`: loop + pass dispatch driven by decoded GS packet state.
6. `tools/gsdump/` (Phase 1): standalone GS-dump parser.

## Ground Truth & Tooling
- **CrystalOSD decomp** (`C:\CodingProjects\Personal\CrystalOSD`): the spec. GS-packet builders
  (`pktSetAlphaBlend`, `pktSetTEST_1`, `pktSetAD`, `sceGsPutDrawEnv`) encode the exact state.
- **ghidra-mcp** (MANDATORY): clock addresses live in `OSDSYS.elf` — always pass
  `program="OSDSYS.elf"` (the active program defaults to `hddosd.elf`).
- **pcsx2-mcp** (`.mcp.json`): run the bundled patched `C:\CodingProjects\Personal\PCSX2-MCP\pcsx2-qt.exe`
  (DebugServer on 21512). BIOS must be `ps2-0230a-20080220` (matches decomp). Do NOT reset with
  OSDSYS BPs armed (recLUT crash). See `docs/FOUNDATION-STATUS.md`.
- **PCSX2 software renderer**: bit-accurate reference frames for numeric pixel-diff. Never compare by photo.
- **PCSX2 GS source** (`C:\CodingProjects\Personal\pcsx2-ref`, `pcsx2/GS/` only): reference for the
  `.gs` format, GS registers, GS→VK blend mapping, swizzle. Read on demand, never bulk-load.
- **Patent digest** (`docs/clock_patent/US6693606-DIGEST.md`): canonical patent reference — scene
  structure (rods + central sphere + light spots + after-image + optional blur), pass ORDERING
  (refraction → additive spots → blur post), and time→visual semantics. Method/ordering ONLY,
  never numbers (those come from decomp + trace). Read the digest, not the 88KB `US6693606.md`.
- Clock/opening use ZERO VU1 microcode; "VU work" = the `sceVu0*` macro lib (known PS2SDK semantics).

## Build
- CMake 3.30+, C++23, Windows (RDNA2). Vulkan SDK required (`find_package(Vulkan)` + glslc).
  `cmake -B build && cmake --build build` (Windows: plain config, presets are macOS-pathed).
- SDL3/VMA/vk-bootstrap/GLM via FetchContent; ImGui submodule (`git submodule update --init`).
- Shaders: glslc `--target-env=vulkan1.3`, glob-compiled to `bin/shaders/`.

## Commit Convention
`Type(Scope): Short imperative description` — Types: Fix, Feat, Refactor, Perf, Build, Docs, GS.
Scopes: Core, Renderer, App, GS, Gsvk, Shaders, CI, Project. Max 72 chars, imperative mood.
**NEVER add a `Co-Authored-By` trailer.**

## Code Directives
- English only. Zero comments except reverse-engineered GS math that needs explanation.
- PascalCase module boundaries; never expose `VkDevice` outside contexts; `gs/` stays Vulkan-free.

## Claude Directives
- Short sentences. No preamble. Run tools, show result, stop. Keep CLAUDE.md and MEMORY.md updated.
- Fan-out subagents run on Sonnet/Haiku, not the top-tier model.
