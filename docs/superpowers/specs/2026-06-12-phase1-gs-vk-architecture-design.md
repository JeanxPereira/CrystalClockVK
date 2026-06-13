# CrystalClockVK Phase 1 — GS→VK Architecture Design

> Status: approved design (brainstorm 2026-06-12). Feeds the implementation plan (writing-plans).
> Supersedes the rendering approach in the pre-Phase-0 code. Prerequisite: execute the Phase 0
> cut plan (`docs/superpowers/plans/2026-06-12-phase0-surgical-cuts.md`) first.

## 1. Goal & scope

Perceptually 1:1 Vulkan recreation of the PS2 OSDSYS **crystal clock** (Visor mode — the radial
ring of prism rods). The style IS the GS rasterizer, replicated in shaders, driven by evidence:
the `clock_viewer.gs` GS dump + the CrystalOSD decomp + live PCSX2 trace. Ground truth is the
PCSX2 software renderer (bit-accurate), compared numerically via pixel-diff. No photo comparison.

**In scope (Phase 1):** render the crystal clock to perceptual parity with the SW-renderer
reference; build the reusable GS→VK framework that later receives the config menu and opening.

**Out of scope (deferred):** config menu (cubes screen — `config_menu.gs` captured for later),
opening sequence, CDVD/config/sound logic. No full GS emulator — only the GS subset OSDSYS uses.

## 2. Decided constraints (brainstorm answers)

| Decision | Choice |
|---|---|
| Blend / rasterizer emulation | **Hybrid**: fixed-function `VkBlend` where the GS formula maps exactly; `local_read` shader emulation only where it diverges (refraction feedback); dither + bit-depth + COLCLAMP in a final "GS-ify" pass. |
| Playable-app look | **GS-authentic native**: internal ~640×448 NTSC, dither + 16/24-bit color + flat linear GS output, integer-scaled to the window. App and pixel-diff share ONE renderer. Modern toggle deferred. |
| Build order | **Rasterizer-first**: validate the GS→VK rasterizer on the dump's exact primitives before porting the live geometry logic. |
| Framework shape | **General GS-command player** (not clock-specific): consumes a decoded GS command stream from any source; clock is the first consumer. |
| Asset source | **GS dump VRAM freeze** (primary), decoded by `gs/SwizzleEngine` + `TextureDecoder`. Our own clean ROM extractor if a fuller source is needed later. The old `C:\...\OSDSYS` project's extracted `.bin` files are cross-reference only; its extractor code is NOT reused. |

Ground-truth anchors (live-verified, see `docs/FOUNDATION-STATUS.md`): BIOS `ps2-0230a-20080220`;
real per-frame rod render chain top `0x00221408 → … → 0x00232618` (NOT the doc's 0x00225E80);
GS coords are 12.4 fixed-point (×16) with +2048 XYOFFSET; GS blend is `(A-B)*C/128+D`, alpha 0–128.

## 3. Layered architecture (post Phase 0 cuts)

```
core/      Vulkan bootstrap (vk-bootstrap), SDL3, RenderDoc hook.            [KEEP]
renderer/  Lean generic VK wrapper: PipelineBuilder, PassRecorder, VMA,      [KEEP, PipelineBuilder SPLIT]
           ShaderLoader, DescriptorAllocator, SwapchainManager, UIRenderer.
gs/        Pure PS2 logic, ZERO Vulkan: GsState register structs,            [KEEP decoders; REBUILD state/consts]
           SwizzleEngine, TextureDecoder, VramBuffer, GsConstants, VU0 math.
gsvk/      NEW bridge layer: GsState → VK pipeline/blend/shader config.       [NEW]
           The GS→VK translator. Depends on gs/ (structs) + renderer/ (VK builders).
app/       Orchestration: feeds the GS command stream, dispatches passes,    [REBUILD]
           owns the native-res target + GS-ify + present + pixel-diff hook.
tools/gsdump/  Standalone GS-dump parser (W1). No Vulkan; emits the command  [NEW]
           stream consumed by gsvk/. Reusable offline.
```

Layer rule (unchanged directive): `gs/` never references `Vk*`. `gsvk/` is the only place GS and
VK meet. `renderer/` stays effect-agnostic.

## 4. Components & interfaces

### 4.1 `tools/gsdump/` — GS dump parser (W1)
- **Does:** decompress (zstd) + parse a PCSX2 `.gs` dump into a structured **GS command stream**:
  the GS state freeze (VRAM + privileged registers) followed by an ordered list of transfers
  (GIFtags → register writes A+D, and primitive submissions PRIM/RGBAQ/XYZ/UV/ST).
- **Output:** `GsCommandStream { GsVramImage vram; GsPrivRegs regs; vector<GsPrimitive> prims; }`
  where each `GsPrimitive` carries its resolved register state (ALPHA, TEST, TEX0, FRAME, ZBUF,
  COLCLAMP, DTHE/DIMX, SCISSOR, XYOFFSET) + vertices (12.4 fixed coords, colors, UVs).
- **Depends on:** nothing Vulkan. Pure parse. Validated against the decomp builders
  (`pktSetAlphaBlend` etc.) that emit these same registers.
- **Note:** `.gs` decompression uses zstd; on this box use Node `zlib.zstdDecompressSync`
  (no zstd CLI / python-zstandard). A C++ zstd dep (already a transitive of PCSX2 tooling) or a
  pre-decompressed `.gs` input is acceptable for the tool.

### 4.2 `gs/` — pure PS2 state & decode (mostly KEEP)
- `GsState` structs (REBUILD per audit: correct `GsAlpha` with no baked presets; add `GsColClamp`,
  `GsDthe`, `GsDimx`). `GsConstants` (REBUILD: live-verified values, `GS_FIXED_POINT_SCALE=16`).
- `SwizzleEngine` + `TextureDecoder` (KEEP): decode GS VRAM (PSM formats, CLUT) → RGBA bytes.
- `VU0Math` (NEW, W2): ported `sceVu0*` subset (`RotTransPers`, `Clip*`, `*LightMatrix`,
  `FTOI4/ITOF4`, matrices). Pure, unit-tested vs live PCSX2 register outputs.

### 4.3 `gsvk/` — the GS→VK translator (NEW)
- **Does:** map one `GsPrimitive`'s resolved GS state to VK draw config:
  - `GsAlpha` → either a `VkPipelineColorBlendAttachmentState` (exact-map cases: additive `one/one`;
    src-over with `As/128` scale applied via push constant) OR a flag that the primitive needs the
    `local_read` refraction-feedback pipeline.
  - `GsTest`/`GsZbuf` → depth/stencil + alpha-test (emulated in shader where GS alpha-test has no
    VK equivalent).
  - `GsTex0` + CLUT → selects the decoded texture (via gs/) bound for the draw.
  - 12.4 fixed coords + XYOFFSET → clip-space vertices at native res.
- **Output:** a small `VkDrawRecipe { pipeline, descriptorSet, pushConstants, vertexRange }` per
  primitive, plus the set of distinct pipelines to prebuild.
- **Does NOT** apply dither/bit-depth/COLCLAMP — that is the GS-ify pass (4.5).
- **Depends on:** `gs/` (structs/decoders) + `renderer/PipelineBuilder` (now `setBlendState`).

### 4.4 `renderer/` — generic VK wrapper (KEEP)
- `PipelineBuilder`: `BlendMode` enum removed; `setBlendState(VkPipelineColorBlendAttachmentState)`
  fed by `gsvk/`.
- `PassRecorder`: exposes `local_read` input-attachment helpers (already present) for the
  refraction-feedback pipeline.
- Swapchain: present in linear color space (no sRGB gamma lift on the GS-flat output); the GS-ify
  output is the final color, integer-scaled.

### 4.5 `app/` — orchestration, GS-ify, present, diff (REBUILD)
- Owns a **native-res offscreen target** (~640×448, higher-precision intermediate) + depth.
- Per frame: iterate the command stream → for each primitive, bind the `gsvk` recipe → draw.
  Refraction primitives use the `local_read` pipeline that samples the in-progress target.
- **GS-ify pass:** one fullscreen pass over the native target applying ordered dither (DIMX 4×4),
  16/24-bit color truncation, and COLCLAMP → produces the true GS output.
- **Present:** integer-scale the GS-ified native target to the swapchain.
- **Pixel-diff hook:** optionally dump the GS-ified native target to disk for numeric comparison.

### 4.6 Pixel-diff harness (W4, may live in `tools/`)
- Render the same OSDSYS frame in the PCSX2 **software renderer** → bit-accurate reference PNG.
- Compare our GS-ified native output vs the reference: per-channel mean and max absolute error,
  plus a heatmap. Automatable; defines a pass threshold (start: report mean+max, tune with user).

## 5. Data flow

```
clock_viewer.gs ─► tools/gsdump parser ─► GsCommandStream {vram, regs, prims}
                                              │
   VRAM freeze ─► gs/SwizzleEngine+TextureDecoder ─► VkImage textures
                                              │
   prims + state ─► gsvk translator ─► VkDrawRecipe[] + pipelines
                                              ▼
                app: draw into native-res target (local_read for refraction)
                                              ▼
                        GS-ify pass (dither + bit-depth + COLCLAMP)
                                   ├─► integer-scale ─► swapchain (present)
                                   └─► dump ─► pixel-diff vs PCSX2 SW reference
```

## 6. Validation split (decoupled correctness)

- **Stage 1 — rasterizer (this phase's core):** feed the dump's EXACT primitives through
  `gsvk` + the renderer + GS-ify. Geometry is taken as ground-truth from the dump, so any
  pixel-diff failure isolates to the RASTERIZER (blend/dither/refraction/texture decode). Target:
  the static clock frame matches the SW reference within threshold.
- **Stage 2 — geometry/logic:** port `sceVu0*` + the render chain (`0x00221408 → … → 0x00232618`)
  to GENERATE the primitives per frame; cross-check the generated primitives against the dump's,
  then animate (rods rotating, hour fill). Failures here isolate to MATH, since the rasterizer is
  already trusted.

This split is the reason for "rasterizer-first": it makes every divergence attributable.

## 7. Testing strategy

- `tools/gsdump`: unit tests parsing a known dump → expected GIFtag/register counts; cross-check
  a few primitives' ALPHA/TEST against the decomp builders.
- `gs/VU0Math`: unit tests vs live PCSX2 register outputs for known inputs (pure functions).
- `gsvk`: unit tests asserting each GS `ALPHA` config maps to the documented VK blend state OR the
  feedback flag; an 8-bit integer-math test comparing emulated blend to the GS `(A-B)*C/128+D`.
- End-to-end: pixel-diff vs SW reference (Stage 1 gate).

## 8. Project structuring (Phase 0 prerequisite)

Execute `docs/superpowers/plans/2026-06-12-phase0-surgical-cuts.md` first: it rebuilds
`GsConstants`/`GsRegisterState`, stubs then rebuilds `app/`, splits `PipelineBuilder` to
`setBlendState`, deletes the patent-derived math + Raylib shaders + poison docs, and fixes build
infra (glslc `--target-env vulkan1.3`). The result is a clean plumbing base. This design then adds
`gsvk/` and `tools/gsdump/` and rebuilds `app/` per §4–§5. Fold the live render-chain correction
(`0x00232618`) and `GS_FIXED_POINT_SCALE=16` into the rebuilt `gs/` constants.

## 9. Non-goals / YAGNI

- No full GS emulator; only OSDSYS's subset (the dump enumerates it).
- No modern-presentation toggle yet (GS-authentic path only).
- No config-menu / opening rendering yet (framework must not preclude them).
- No reuse of the old `OSDSYS` project's extractor code.

## 10. Open items pinned during implementation (not blockers)

- Exact GS subset (which ALPHA/TEST/PSM/DTHE values) — **enumerated by W1** parsing `clock_viewer.gs`.
- Whether DTHE (dither) is enabled for the clock — read from the dump; GS-ify applies it only if so.
- Native framebuffer resolution — read from the dump's FRAME/DISP registers (≈640×448 expected).
- Pixel-diff pass threshold — set with the user after the first Stage-1 numbers.

## 11. Reference sources (PCSX2 GS subtree — read on demand, never bulk-load)

Local sparse clone at `C:\CodingProjects\Personal\pcsx2-ref` (`pcsx2/GS/` only, partial + shallow).
PCSX2 already solved several of our exact problems; use it as an authoritative reference. Read
specific files with Grep/Read when a concrete question arises — do NOT load wholesale into context.

- **`.gs` dump format** (W1 parser spec): `pcsx2/GS/GSDump.{cpp,h}`, `GSLzma.{cpp,h}`,
  `GSState.cpp` (the freeze/serialize). Read the writer to know the reader.
- **GS register layout** (`gs/GsRegisterState` rebuild): `pcsx2/GS/GSRegs.h`, `GS.h` — the GIFReg
  bitfields (ALPHA, TEST, FRAME, TEX0, ZBUF, COLCLAMP, DIMX, SCISSOR, XYOFFSET).
- **GS→VK blend mapping** (`gsvk/` translator): `pcsx2/GS/Renderers/Vulkan/` + `GS/GSDevice.*`
  blend tables — PCSX2's solved GS-ALPHA→VkBlend mapping. Anchor the hybrid blend here instead of
  deriving from scratch.
- **Bit-exact blend + dither + COLCLAMP** (the GS-ify pass): `pcsx2/GS/Renderers/SW/GSDrawScanline*.cpp`.
- **Swizzle / PSM** (cross-check `SwizzleEngine`/`TextureDecoder`): `pcsx2/GS/GSLocalMemory.cpp`.
- **NOT used:** EE/VU/IOP recompilers, UI, audio, netplay — the crystal-clock LOGIC comes from the
  CrystalOSD decomp + ghidra-mcp, not from PCSX2 (PCSX2 emulates the hardware, not the OSDSYS code).
