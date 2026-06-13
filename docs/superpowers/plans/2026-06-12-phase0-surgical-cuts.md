# Phase 0 Surgical Cuts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Execute the approved cuts from `docs/PHASE0-AUDIT.md` — remove every look-guess module, leave a clean buildable Vulkan plumbing base ready for the GS-spec rebuild.

**Context:** Prerequisite for the approved Phase 1 architecture
(`docs/superpowers/specs/2026-06-12-phase1-gs-vk-architecture-design.md`). Phase 0 produces the
clean base; Phase 1 then adds `gsvk/` + `tools/gsdump/` and rebuilds `app/` on top. The rebuilt
`gs/` constants here already carry the live-verified values (`docs/FOUNDATION-STATUS.md`):
rod stride `0x140`, selection flag `0xF0`, `GS_FIXED_POINT_SCALE=16`. The live render-chain
correction (real render `0x00232618`, not `0x00225E80`) is folded into the doc rewrite (Task 8).

**Architecture:** Staged amputation, build green after every commit. A stubbed `RenderOrchestrator` becomes the seam: once it stops including contaminated headers, all AMPUTATE files delete cleanly. App renders black + ImGui overlay at the end — that is the intended Phase 0 exit state.

**Tech Stack:** C++23, CMake 3.30, Vulkan 1.3 (dynamic rendering, sync2), SDL3, VMA, vk-bootstrap, Dear ImGui.

**Verification command for every task:** `cmake -B build` (only if CMakeLists changed) then `cmake --build build`. Expected: exit 0, no warnings introduced. After Task 2 also run `bin/CrystalClockVK.exe` briefly: black window + "CrystalClock Debug" ImGui panel, no crash.

**Commit convention (CLAUDE.md):** `Type(Scope): Imperative subject ≤72 chars`.

---

### Task 1: Rebuild GsConstants.hpp from verified values

**Files:**
- Rewrite: `src/gs/GsConstants.hpp`

Sources of truth: live PCSX2 trace table in `MEMORY.md §7`; decomp-verified scale constants (FUN_00232da0 → 0x00242630). Everything not traceable to those is dropped.

- [ ] **Step 1: Replace entire file content**

```cpp
#pragma once

#include <cstdint>

namespace GsConstants {

constexpr int ROD_COUNT = 12;

constexpr int ROD_STRUCT_SIZE = 0x140;
constexpr int ROD_OFFSET_MATRIX = 0x30;
constexpr int ROD_OFFSET_POSITION = 0x60;
constexpr int ROD_OFFSET_SCALE = 0x78;
constexpr int ROD_OFFSET_GLASS_RGBA = 0x90;
constexpr int ROD_OFFSET_ADDITIVE_RGBA = 0xD0;
constexpr int ROD_OFFSET_OFFSETPASS_RGBA = 0xE0;
constexpr int ROD_OFFSET_SELECTION_FLAG = 0xF0;

constexpr uint32_t ROD_ARRAY_PTR_ADDR = 0x0034E980;
constexpr uint32_t ROD_DATA_ADDR = 0x0034F9C0;
constexpr uint32_t ROD_GROUP_A_ADDR = 0x375250;
constexpr uint32_t ROD_GROUP_B_ADDR = 0x377E50;

constexpr uint32_t CLOCK_RENDER_ENTRY = 0x00225E80;
constexpr uint32_t ROD_GLASS_RENDER_FUNC = 0x00232640;
constexpr uint32_t ROD_SPECULAR_RENDER_FUNC = 0x00232878;
constexpr uint32_t OSDSYS_GP_VALUE = 0x002AF070;

constexpr float ANGLE_STEP_PASS2 = 0.26f;
constexpr float ANGLE_STEP_PASS3 = 0.33f;
constexpr float ANGLE_STEP_UNKNOWN4 = 0.38f;
constexpr float GP_SCALAR_8338 = 0.80f;
constexpr float GP_SCALAR_833C = 0.65f;
constexpr float REFRACT_DISP_A = 0.037f;
constexpr float REFRACT_DISP_B = 0.031f;
constexpr float REFRACT_DISP_C = 0.028f;

constexpr uint8_t GLASS_RGBA[4] = {45, 87, 102, 128};
constexpr uint8_t ADDITIVE_RGBA[4] = {60, 60, 60, 128};
constexpr uint8_t OFFSETPASS_RGBA[4] = {40, 40, 40, 128};

constexpr float GS_FAR_PLANE = 2048.0f;
constexpr float GS_FIXED_POINT_SCALE = 16.0f;

constexpr int SCREEN_RATIO_16_9 = 0x10;
constexpr int SCREEN_RATIO_4_3 = 0x0E;

constexpr int SCALE_MAX_RODS_MIDDLE_STD = 39;
constexpr int SCALE_MAX_RODS_MIDDLE_WIDE = 31;
constexpr float SCALE_DIVISOR_MIDDLE_STD = 13.0f;
constexpr float SCALE_DIVISOR_MIDDLE_WIDE = 10.0f;

constexpr int SCALE_MAX_RODS_EDGE_STD = 32;
constexpr int SCALE_MAX_RODS_EDGE_WIDE = 26;
constexpr float SCALE_DIVISOR_EDGE_STD = 6.0f;
constexpr float SCALE_DIVISOR_EDGE_WIDE = 5.0f;

constexpr int COUNTDOWN_STD = 40;
constexpr int COUNTDOWN_WIDE = 33;
constexpr float COUNTDOWN_MAX_STD = 40.0f;
constexpr float COUNTDOWN_MAX_WIDE = 33.0f;

} // namespace GsConstants
```

Note `GS_FIXED_POINT_SCALE` changed 65536→16: GS XYZ vertex coords are 12.4 fixed point (FTOI4 = ×16). The old 65536 assumed 16.16 with no provenance.

- [ ] **Step 2: Build**

Run: `cmake --build build` — expected PASS (`GsCrystalMath.hpp` consumes only `SCREEN_RATIO_*`, `SCALE_*`, `COUNTDOWN_*`, all preserved; `main.cpp` consumes `SCREEN_RATIO_*`, preserved).

- [ ] **Step 3: Commit**

```bash
git add src/gs/GsConstants.hpp
git commit -m "GS(GS): Rebuild GsConstants from live PCSX2 trace values"
```

---

### Task 2: Stub RenderOrchestrator + strip guessed pass dispatch from main.cpp

**Files:**
- Rewrite: `src/app/RenderOrchestrator.hpp`
- Rewrite: `src/app/RenderOrchestrator.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Replace `src/app/RenderOrchestrator.hpp` entirely**

```cpp
#pragma once

#include "core/VulkanContext.hpp"
#include "renderer/SwapchainManager.hpp"
#include "renderer/ResourceManager.hpp"
#include "renderer/PassRecorder.hpp"
#include "app/TimeSync.hpp"

struct FrameParams {
    TimeInfo time;
    VkExtent2D extent;
    float aspect;
    float totalTime;
    VkDevice device;
    VkImageView currentImageView;
    uint32_t frameIndex;
};

class RenderOrchestrator {
public:
    void init(const VulkanContext& ctx, const SwapchainManager& swapchain, ResourceManager& resources);
    void recordFrame(PassRecorder& recorder, const FrameParams& params);
    void updateUBO(const FrameParams& params);
    void destroy(VkDevice device, ResourceManager& resources);
};
```

- [ ] **Step 2: Replace `src/app/RenderOrchestrator.cpp` entirely**

```cpp
#include "RenderOrchestrator.hpp"

void RenderOrchestrator::init(const VulkanContext&, const SwapchainManager&, ResourceManager&) {}

void RenderOrchestrator::recordFrame(PassRecorder&, const FrameParams&) {}

void RenderOrchestrator::updateUBO(const FrameParams&) {}

void RenderOrchestrator::destroy(VkDevice, ResourceManager&) {}
```

- [ ] **Step 3: Edit `src/main.cpp` — six hunks**

Hunk A — includes (lines 12-13). Remove:
```cpp
#include "app/CrystalMath.hpp"
#include "gs/GsConstants.hpp"
```

Hunk B — delete tunnelImage creation (lines 117-121):
```cpp
        // Tunnel render target — renders tunnel to this, then crystal reads it as sampled texture
        AllocatedImage tunnelImage = resources.createImage(
            swapchain.extent(),
            swapchain.imageFormat(),
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
```
Keep `mainColorImage` (offscreen target → blit; needed later for pixel-diff capture).

Hunk C — resize block: delete `resources.destroyImage(tunnelImage);` (line 157) and the `tunnelImage = resources.createImage(...)` re-creation (lines 164-166).

Hunk D — delete `params.tunnelImageView = tunnelImage.imageView;` (line 208).

Hunk E — replace the entire block from the `PASS A` banner comment (line 214) through `recorder.endDebugLabel();` of the inter-rod pass (line 306) with:

```cpp
            recorder.transitionImage(mainColorImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

            VkClearValue clear{};
            clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

            recorder.beginDebugLabel("Frame", 0.2f, 0.4f, 1.0f);
            recorder.beginRendering(mainColorImage.imageView, depthImage.imageView,
                                    swapchain.extent(), &clear);
            recorder.setViewportScissor(swapchain.extent());

            orchestrator.recordFrame(recorder, params);

            recorder.endRendering();
            recorder.endDebugLabel();
```

Hunk F — ImGui block: delete lines 309-313 (`hlRod`, `hourCounter`, `isWide`, `screenRatio`, `fillAmt`), delete lines 329-330 (`Highlighted Rod`, `Hour Scale Slide`), delete lines 332-334 (`Separator` + `Tunnel(1)+...` + `Draw Calls`). Keep FPS, RenderDoc button, `Time:` and `Sec in Min:` lines.

Hunk G — cleanup: delete `resources.destroyImage(tunnelImage);` (line 397).

- [ ] **Step 4: Build and run**

Run: `cmake --build build` — expected PASS.
Run `bin/CrystalClockVK.exe` ~5 s: black window, ImGui debug panel with FPS/time, clean exit on close.

- [ ] **Step 5: Commit**

```bash
git add src/app/RenderOrchestrator.hpp src/app/RenderOrchestrator.cpp src/main.cpp
git commit -m "Refactor(App): Stub RenderOrchestrator, strip guessed pass dispatch"
```

---

### Task 3: Delete AMPUTATE source + shader files

**Files:**
- Delete: `src/app/CrystalMath.hpp`, `src/app/CrystalGeometry.hpp`, `src/app/CrystalGeometry.cpp`
- Delete: `shaders/Crystal.vert`, `shaders/Crystal.frag`, `shaders/CrystalSpecular.frag`, `shaders/Tunnel.vert`, `shaders/Tunnel.frag`
- Modify: `CMakeLists.txt:114`
- Keep: `shaders/Triangle.vert`, `shaders/Triangle.frag` (smoke test)

The reusable `Crystal.vert` screen-UV snippet is preserved in `docs/OPUS-HANDOFF.md §5` — the shader files carry nothing else worth keeping.

- [ ] **Step 1: Delete files**

```bash
git rm src/app/CrystalMath.hpp src/app/CrystalGeometry.hpp src/app/CrystalGeometry.cpp
git rm shaders/Crystal.vert shaders/Crystal.frag shaders/CrystalSpecular.frag shaders/Tunnel.vert shaders/Tunnel.frag
```

- [ ] **Step 2: Remove `src/app/CrystalGeometry.cpp` from CMake SOURCES (line 114)**

The `src/app/RenderOrchestrator.cpp` entry stays (stub file exists). Also delete stale `.spv` outputs: `rm bin/shaders/Crystal.* bin/shaders/CrystalSpecular.* bin/shaders/Tunnel.*` (untracked artifacts).

- [ ] **Step 3: Reconfigure + build**

Run: `cmake -B build` then `cmake --build build` — expected PASS; shader glob now compiles only Triangle pair.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "Refactor(App): Delete patent-derived math, geometry and shaders"
```

---

### Task 4: PipelineBuilder SPLIT — explicit blend state

**Files:**
- Modify: `src/renderer/PipelineBuilder.hpp`
- Modify: `src/renderer/PipelineBuilder.cpp`
- Modify: `src/renderer/DescriptorAllocator.hpp:8-11`

- [ ] **Step 1: In `PipelineBuilder.hpp`** delete the `BlendMode` enum (lines 6-13) and replace the declaration `PipelineBuilder& setBlendMode(BlendMode mode);` (lines 39-40 incl. comment) with:

```cpp
    PipelineBuilder& setBlendState(const VkPipelineColorBlendAttachmentState& state);
```

Also replace the class doc comment (lines 15-16) with:
```cpp
// Builder pattern for VkPipeline construction using VK_KHR_dynamic_rendering.
```

- [ ] **Step 2: In `PipelineBuilder.cpp`** replace the whole `setBlendMode` function (lines 91-136) with:

```cpp
PipelineBuilder& PipelineBuilder::setBlendState(const VkPipelineColorBlendAttachmentState& state) {
    m_colorBlendAttachment = state;
    return *this;
}
```

- [ ] **Step 3: In `DescriptorAllocator.hpp`** delete the 3-line refraction-theory comment at lines 8-11.

- [ ] **Step 4: Build**

Run: `cmake --build build` — expected PASS (no remaining `setBlendMode` callers after Task 2 stub).

- [ ] **Step 5: Commit**

```bash
git add src/renderer/PipelineBuilder.hpp src/renderer/PipelineBuilder.cpp src/renderer/DescriptorAllocator.hpp
git commit -m "Refactor(Renderer): Replace BlendMode enum with explicit blend state"
```

---

### Task 5: Rebuild GsRegisterState + trim GsCrystalMath

**Files:**
- Rewrite: `src/gs/GsRegisterState.hpp` (keep GsTex0/GsTest/GsFrame/GsZbuf bodies verbatim)
- Modify: `src/gs/GsCrystalMath.hpp`

- [ ] **Step 1: Replace top of `GsRegisterState.hpp`** — file header comment + `GsAlpha` (lines 5-26) become:

```cpp
// GS register state containers. Values must come from decoded OSDSYS packets
// (pktSetAlphaBlend, pktSetTEST_1, sceGsPutDrawEnv) — never hand-authored.

// ALPHA: Cv = ((A - B) * C >> 7) + D
// A,B,D: 0=Cs 1=Cd 2=0 | C: 0=As 1=Ad 2=FIX
struct GsAlpha {
    uint8_t a;
    uint8_t b;
    uint8_t c;
    uint8_t d;
    uint8_t fix;
};

struct GsColClamp {
    bool clamp;
};

struct GsDthe {
    bool enable;
};

struct GsDimx {
    int8_t dm[4][4];
};
```

(GsTex0, GsTest, GsFrame, GsZbuf: unchanged.)

- [ ] **Step 2: Edit `GsCrystalMath.hpp`:**
  - Delete the `buildRotation` doc block + function (lines 15-52).
  - Delete `#include <cmath>` (line 6) — no longer used.
  - Replace file doc comment (lines 9-11) with:
    ```cpp
    // Pure PS2 GS math — NO Vulkan dependencies.
    // Sources: decomp FUN_00232da0 → 0x00242630 (rod scale), live PCSX2 trace (MEMORY.md §7).
    ```
  - Replace the rod-selection doc block (lines 122-129) with:
    ```cpp
    // Rod selection flag lives at rod+0xF0 (live trace, stride 0x140).
    // Routes the active hour rod to the selected-rod passes.
    ```
  - In `RodState`, fix `// flag at +0x150` → `// flag at +0xF0` (line 131).

- [ ] **Step 3: Build**

Run: `cmake --build build` — expected PASS.

- [ ] **Step 4: Commit**

```bash
git add src/gs/GsRegisterState.hpp src/gs/GsCrystalMath.hpp
git commit -m "GS(GS): Rebuild GsRegisterState, drop invalid VU0 rotation chain"
```

---

### Task 6: Build infra fixes

**Files:**
- Modify: `CMakeLists.txt:86`
- Modify: `CMakePresets.json:5`

- [ ] **Step 1: glslc target env (line 86):**

```cmake
        COMMAND ${GLSLC} --target-env=vulkan1.3 -o ${SHADER_SPV} ${SHADER_SRC}
```

- [ ] **Step 2: CMakePresets `cmakeMinimumRequired.minor`: 25 → 30.**

Note (do NOT change now): both presets hardcode macOS Vulkan SDK env paths; harmless on Windows (find_program falls back to PATH) but worth a per-platform preset split later.

- [ ] **Step 3: Reconfigure + clean shader rebuild**

Run: `cmake -B build` then `cmake --build build --target Shaders` — Triangle shaders recompile with vulkan1.3 env, expected PASS.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt CMakePresets.json
git commit -m "Build(Project): Target Vulkan 1.3 SPIR-V, fix preset version"
```

---

### Task 7: Delete poison docs, fix spec errors

**Files:**
- Delete: `context/opus-rod-analysis.md`, `context/sonnet_chat.txt`, `docs/ANALYSIS-Raylib-vs-Vulkan-vs-OSDSYS.md`, `docs/ghidra_analysis/rod_analysis.md`
- Modify: `docs/ghidra_analysis/vu0_decode.md` (top), `docs/OSDSYS-DECOMP-1to1-STRATEGY.md:45`, `context/CrystalClockVK-ImplPlan.md`

- [ ] **Step 1:** `git rm` the four poison files.

- [ ] **Step 2:** Insert at the very top of `vu0_decode.md`:

```markdown
> **QUARANTINE (Phase 0 audit 2026-06-12):** The raw VU0 instruction decode below is
> hardware fact and safe to reference. The GLM `BuildRotation` reconstruction is
> UNVERIFIED — it is the same azimuth/elevation chain amputated from GsCrystalMath
> (produced rod clustering). Do not port it. Re-validate against sceVu0* PS2SDK
> semantics first. The FOV address claim may be the "Reset" string (MEMORY.md §7).
```

- [ ] **Step 3:** In `OSDSYS-DECOMP-1to1-STRATEGY.md` line 45, change `rod struct 0x160` → `rod struct 0x140`.

- [ ] **Step 4:** In `CrystalClockVK-ImplPlan.md`: change the RenderOrchestrator line `Translates the 5-Pass logic into Vulkan draws` → `Translates decoded GS packet state into Vulkan draws`; change the push-constant line mentioning `(renderPass, rodAlpha)` → `(per-draw scalars; pass routing via pipeline binding, not push constants)`. (Open the file to locate exact lines ~31/34.)

- [ ] **Step 5: Commit**

```bash
git add -A context docs
git commit -m "Docs(Project): Remove poison analysis docs, fix spec errors"
```

---

### Task 8: Rewrite MEMORY.md and CLAUDE.md

**Files:**
- Modify: `MEMORY.md`
- Rewrite: `CLAUDE.md`

- [ ] **Step 1: MEMORY.md surgery** — keep sections 1, 2, 3, 7 verbatim. Delete sections 5, 6, 8 entirely (patent-rotation findings, implementation status of amputated code, milestone table). In section 4, replace the `app/` bullet (`Uses push constants ... renderPass ID, rodAlpha ...`) with:

```markdown
4. **`app/`**: Application loop + GS-state-driven pass dispatch. Pass routing comes from decoded OSDSYS GS packets, never hand-authored tables.
```

Append as the new final section:

```markdown
## 5. Phase 0 Reset (2026-06-12)
Full evidence-based audit + surgical amputation executed. See `docs/PHASE0-AUDIT.md`
(verdicts + evidence) and `docs/OPUS-HANDOFF.md` (rebuild roadmap). All look-guess
code (patent rotation chain, lerpPrismColor, Raylib shaders, BlendMode table,
2-target refraction ping-pong) was deleted; Vulkan/SDL/GS-memory plumbing kept.
App intentionally renders black + ImGui until the GS-spec rebuild lands.
Master spec: `docs/OSDSYS-DECOMP-1to1-STRATEGY.md`. Ground truth: CrystalOSD decomp
+ PCSX2 GS dumps + SW-renderer pixel-diff. Never compare by photo.
```

- [ ] **Step 2: Replace CLAUDE.md entirely** with:

```markdown
# CrystalClockVK — PS2 OSDSYS Visual Layer, 1:1 in Vulkan

## Project Overview
Perceptually 1:1 Vulkan recreation of the PS2 OSDSYS VISUAL layer (clock/opening/UI).
The style IS the GS rasterizer — blend `(A-B)*C/128+D`, dithering, framebuffer-feedback
refraction, additive glow, 12.4 fixed-point coords — replicated IN SHADERS from GS state
decoded out of the byte-perfect OSDSYS decomp. Never guess the look; read it.

## Read First
1. `docs/OSDSYS-DECOMP-1to1-STRATEGY.md` — master strategy (spec = decomp; precision = GS dumps + pixel-diff).
2. `docs/PHASE0-AUDIT.md` — what was cut and why.
3. `docs/OPUS-HANDOFF.md` — current rebuild roadmap + open decisions.
4. `MEMORY.md` — architectural history + PCSX2 live-trace ground truth (§ live trace).

## Architecture (4 strict layers)
1. `core/`: Vulkan bootstrap (vk-bootstrap), SDL3, RenderDoc hook.
2. `renderer/`: Lean Vulkan 1.3 wrapper (VMA, PassRecorder, PipelineBuilder, dynamic rendering, local_read).
3. `gs/`: Pure PS2 logic — NO Vulkan symbols. Unit-testable.
4. `app/`: Loop + pass dispatch driven by decoded GS packet state.

## Ground Truth & Tooling
- **CrystalOSD decomp** (separate repo): the spec. GS-packet builders (`pktSetAlphaBlend`,
  `pktSetTEST_1`, `pktSetAD`, `sceGsPutDrawEnv`) encode the exact blend/test/env state.
- **ghidra-mcp** (MANDATORY): decompile from hddosd.elf for `gs/` logic.
- **pcsx2-mcp** (`.mcp.json`): live EE registers, BPs in OSDSYS code, GS dumps.
  PC reads `0x00081fc0` (BIOS idle) when paused without BP — set BP inside OSDSYS code.
- **PCSX2 software renderer**: reference frames for numeric pixel-diff. Never compare by photo.
- Clock/opening use ZERO VU1 microcode; "VU work" = `sceVu0*` macro lib (known PS2SDK semantics).

## Build
- CMake 3.30+, C++23, Windows (RDNA2) + macOS (MoltenVK). `cmake -B build && cmake --build build`.
- SDL3/VMA/vk-bootstrap/GLM via FetchContent; ImGui submodule. CI: GitHub Actions Win+Mac.
- Shaders: glslc `--target-env=vulkan1.3`, glob-compiled to `bin/shaders/`.

## Commit Convention
`Type(Scope): Short imperative description` — Types: Fix, Feat, Refactor, Perf, Build, Docs, GS.
Scopes: Core, Renderer, App, GS, Shaders, CI, Project. Max 72 chars, imperative mood.

## Code Directives
- English only. Zero comments except reverse-engineered GS math that needs explanation.
- PascalCase module boundaries; never expose `VkDevice` outside contexts; `gs/` stays Vulkan-free.

## Claude Directives
- Use short, 3-6 word sentences. No filter, preamble, or pleasantries.
- Run tools first, show result, then stop. Do not narrate. Drop articles.
- Keep CLAUDE.md and MEMORY.md updated.
- Fan-out subagents run on Sonnet/Haiku, not the top-tier model.
```

- [ ] **Step 3: Commit**

```bash
git add MEMORY.md CLAUDE.md
git commit -m "Docs(Project): Rewrite MEMORY and CLAUDE for GS-spec restart"
```

---

## Exit criteria
- `cmake --build build` green; app runs: black frame + ImGui (FPS/time), clean shutdown.
- `grep -ri "CrystalMath\|BlendMode\|lerpPrism\|tunnelImage" src/ shaders/` → zero hits.
- Repo contains no doc contradicting `MEMORY.md` live-trace values.
- Working tree ready for Phase 1 (see `docs/OPUS-HANDOFF.md`).
