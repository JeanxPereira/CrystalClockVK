# Phase 0 Audit — KEEP / AMPUTATE / REBUILD

> Evidence-based audit (2026-06-12) by 7 module auditors + entanglement verifier +
> adversarial challenger (ultracode workflow `phase0-audit`, run `wf_31e22957-18e`).
> No cuts executed yet. This document is the approved cut plan input.

## Verdict legend
- **KEEP** — plumbing orthogonal to visual correctness.
- **AMPUTATE** — exists only to fake the PS2 look without decomp/GS-dump provenance. Delete.
- **REBUILD** — right architectural slot, wrong body. Regenerate from decomp spec.
- **SPLIT** — file mixes both; cut line specified.

## 1. Verdict table

### src/core/ — all KEEP (high)
`VulkanContext.{hpp,cpp}`, `WindowContext.{hpp,cpp}`, `RenderDocWrapper.{hpp,cpp}`.
Clean vk-bootstrap/SDL3/VMA/RenderDoc plumbing. Zero look constants.

### src/main.cpp — SPLIT (high)
- KEEP: `SwapchainSync` (20-52), `transitionDepthImage` (54-91), init wiring, frame loop
  fence/acquire/submit/present (131-196, 340-390), ImGui FPS + RenderDoc button, cleanup.
- AMPUTATE: tunnelImage/mainColorImage 2-target refraction theory (118-128, 160-170),
  PASS A/PASS B dispatch + inter-rod ping-pong copy block (214-306) — patent-derived guess,
  not decomp GS packet sequence; CrystalMath debug calls (309-313); hardcoded
  `Tunnel(1)+Glass(12)+Spec(12)+Fill(1)` draw-count string (333).

### src/renderer/ — 13/16 KEEP
- KEEP (high): `DeletionQueue`, `DescriptorAllocator.cpp`, `FrameData`, `PassRecorder.{hpp,cpp}`
  (fully generic, local-read helpers included), `ResourceManager`, `ShaderLoader`,
  `SwapchainManager`*, `UIRenderer`.
- SPLIT `PipelineBuilder.{hpp,cpp}` (high): class body = pure boilerplate, KEEP.
  `BlendMode` enum (hpp 1-13) + `setBlendMode` switch (cpp 91-136) hardcode 4 VkBlendFactor
  combos sourced from known-bad `opus-rod-analysis.md` — AMPUTATE. Replace with
  `setBlendState(VkPipelineColorBlendAttachmentState)` fed by decoded `pktSetAlphaBlend`.
- SPLIT `DescriptorAllocator.hpp` (minor): 3-line refraction-theory comment (8-11), delete comment.
- *Adversarial flag on `SwapchainManager.cpp:18`: `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`
  applies compositor gamma to linear GS pixels — latent visual-correctness bug. Decide
  color-space policy (pass-through vs explicit gamma in final blit) during rebuild.

### src/gs/
- KEEP (high): `SwizzleEngine.{hpp,cpp}`, `TextureDecoder.{hpp,cpp}`, `VramBuffer.{hpp,cpp}` —
  genuine GS page/block/column geometry matching PCSX2/GSdx reference tables.
- REBUILD `GsConstants.hpp`: ≥5 confirmed-wrong values (ROD_STRUCT_SIZE 0x160→real 0x140;
  ROD_OFFSET_SELECTION_FLAG 0x150→real 0xF0; ANGLE_STEP_* 0.10472→real 0.26/0.33 rad;
  `fGpffff8c28` labelled FOV = ASCII "Reset"; DAT_002973a0/c0 labelled GS prims = Shift-JIS text).
  Correct values to retain: SCREEN_RATIO_*, SCALE_*, COUNTDOWN_*. Add ROD_COUNT=12 here.
- REBUILD `GsRegisterState.hpp`: GsTex0/GsTest/GsFrame/GsZbuf layouts correct (salvage);
  all 3 GsAlpha factory presets wrong (sourced from bad docs); missing COLCLAMP + DTHE.
- SPLIT `GsCrystalMath.hpp`: `buildRotation()` (30-52) = discarded VU0 azimuth/elevation chain,
  AMPUTATE. `buildGsProjection`, `computeRodScale`, `computePassAngle`, `RodState` = keep slots,
  fix constants + +0x150→+0xF0 comment.

### src/app/
- AMPUTATE `CrystalMath.hpp` (high): patent rotation chain (X 25°, groupRot, Z·-30°),
  `lerpPrismColor` invented palette, Raylib orb block, ROD_WIDTH_SCALE=1.3/ROD_LENGTH_SCALE=0.95.
  All contradicted by live PCSX2 trace.
- REBUILD `CrystalGeometry.{hpp,cpp}`: `CrystalVertex` + attribute descriptors = correct plumbing;
  mesh bodies are Raylib ports with fudge dims (baseRadius 0.866, height 10, radius 20, length 100).
- REBUILD `RenderOrchestrator.{hpp,cpp}`: Vulkan plumbing (descriptors, VMA upload, pipeline calls,
  draw-loop shape) correct; visual content (5-pass alpha theory, pass colors, groupRot, wrong FOV,
  `0.004f` tunnel scroll scalar, +0x150 RodData comment) replace from decoded GS state.
  Preserve the two trace-confirmed colors: glass RGBA(45,87,102,128) (+0x90), additive
  RGBA(60,60,60,128) (+0xD0).
- KEEP `TimeSync.{hpp,cpp}`: pure wall-clock plumbing.

### shaders/
- AMPUTATE: `Crystal.frag` (Fresnel/Phong, magic 0.025/0.65/0.18/80; real displacement is
  per-channel 0.028–0.037 from live trace), `CrystalSpecular.frag`, `Tunnel.frag`
  (Blinn-Phong + Raylib colors).
- SPLIT `Crystal.vert`: screen-space UV derivation (44-47) + descriptor/push-constant skeleton
  reusable; inverse-transpose normal path + `inUV*2.0` AMPUTATE.
- REBUILD `Tunnel.vert`: transform passthrough valid; drop lit-mesh normal pipeline.
  **Latent bug (both Tunnel shaders): FrameUBO missing `view` mat4 → viewPos/prismColor read at
  wrong offsets vs C++ struct.**
- KEEP `Triangle.{vert,frag}`: bootstrap smoke-test, unused by orchestrator.

### Build/infra — all KEEP, 4 fixes
1. **Build-break on cut**: CMakeLists.txt 114-115 name `CrystalGeometry.cpp` / `RenderOrchestrator.cpp`.
2. **Blocker for Phase 1**: glslc (CMakeLists.txt:86) lacks `--target-env vulkan1.3` — any
   `subpassLoad()` shader will fail to compile. Fix before shader rebuild.
3. `.mcp.json` registers pcsx2 only; ghidra-mcp lives in user config (onboarding gap).
4. CMakePresets.json `cmakeMinimumRequired` 3.25 vs CMakeLists 3.30.
Shader glob (73-76) is safe — shader deletions need no CMake edit.

### Docs / context
- AMPUTATE (poison — wrong addresses/values per MEMORY.md §7): `context/opus-rod-analysis.md`,
  `context/sonnet_chat.txt`, `docs/ANALYSIS-Raylib-vs-Vulkan-vs-OSDSYS.md`,
  `docs/ghidra_analysis/rod_analysis.md`.
- REBUILD: `MEMORY.md`, `CLAUDE.md` — keep §7-class ground truth; purge patent rotation chain,
  5-pass blend table, milestone statuses claiming amputated code "Complete".
- KEEP: `docs/OSDSYS-DECOMP-1to1-STRATEGY.md` (master spec — **but fix §3 line 45: rod struct
  is 0x140, not 0x160**), `docs/clock_patent/*` (method-level, not numeric spec),
  `context/CrystalClockVK-ImplPlan.md` (**fix lines 31/34: "5-Pass logic" + `renderPass` push
  constant encode the old theory**).
- QUARANTINE `docs/ghidra_analysis/vu0_decode.md`: raw instruction decode = hardware fact, KEEP;
  the GLM `BuildRotation` reconstruction (156-175) is the same formula amputated from
  GsCrystalMath — mark it untrusted until re-validated against sceVu0* semantics.

## 2. Entanglement (why cuts must be staged)
Tightest knot: `RenderOrchestrator.hpp` includes all six contaminated headers and is included by
`main.cpp` — every AMPUTATE symbol flows into the KEEP frame loop TU. Compile-time binds:
- `main.cpp:309-313` calls `CrystalMath::*` directly.
- `RenderOrchestrator.hpp:100` uses `CrystalMath::ROD_COUNT` as array bound — migrate to GsConstants first.
- `m_passAlpha` (GsAlpha) is write-only — droppable with zero functional loss.
- Deleting AMPUTATE shaders without removing their `loadModule` calls = runtime crash at init.

## 3. Cut order (build green at every commit)
1. **Rebuild `GsConstants.hpp`** — keep verified constants, add ROD_COUNT=12, drop/sentinel wrong values.
2. **Stub `RenderOrchestrator.{hpp,cpp}`** — FrameParams (no tunnelImageView), no contaminated
   includes, empty method bodies; CMake SOURCES stays valid.
3. **Delete `CrystalMath.hpp` + `CrystalGeometry.{hpp,cpp}`**; strip main.cpp 309-313 + pass-dispatch
   block; update CMakeLists 114-115.
4. **SPLIT PipelineBuilder** — remove BlendMode, add `setBlendState(...)`.
5. **Rebuild `GsRegisterState.hpp`** (correct GsAlpha, add COLCLAMP/DTHE) + SPLIT GsCrystalMath
   (drop buildRotation).
6. **Delete `Crystal.frag`, `CrystalSpecular.frag`, `Tunnel.frag`** + their loadModule calls.
7. **SPLIT/REBUILD `Crystal.vert` / `Tunnel.vert`** (strip normal pipeline, fix FrameUBO layout).
8. **Delete poison docs** (4 files above).
9. **Rewrite MEMORY.md + CLAUDE.md**; fix strategy doc §3 + ImplPlan lines 31/34; add
   `--target-env vulkan1.3` to glslc; bump CMakePresets to 3.30.

## 4. Counts
38 KEEP · 6 SPLIT · 10 REBUILD · 9 AMPUTATE (delete outright).
The Vulkan/SDL/GS-memory plumbing survives almost intact; everything that *produces the look*
(shaders, blend table, rotation/color/geometry math, pass dispatch, poison docs) goes.
