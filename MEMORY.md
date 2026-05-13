# CrystalClockVK Project Memory & Context Brain dump

## Agent Skill & Instruction Manual 
**AI AGENTS**: Read this file entirely before touching code. This is the master architectural brain for the CrystalClockVK rewrite, tracking exact technical debates, dead-ends avoided, and correct implementation paths decided alongside the lead developer.

---

## 1. Project Pivot: Why We Dropped Raylib / OpenGL
Initially, the project was a C++ Raylib implementation. Through deep native analysis of both the **OSDSYS.elf** via Ghidra-MCP and the official Sony Patent **US6,693,606**, it was discovered that the "Crystal Refraction" effect was NOT achieved via modern Shader Phong/Normal Mapping. 

**The GS Refraction Truth:**
The PS2 GS (Graphics Synthesizer) functions seamlessly as a tile-based-like pipeline without restrictive constraints. It renders the background smoke/tunnel to the VRAM, then immediately renders the Crystal primitives in the *exact same framebuffer*, sampling the pixel data underneath the crystal model and applying a UV offset governed by the vertex normal.

**The OpenGL Problem:**
In standard OpenGL, sampling from a Framebuffer Object (FBO) while writing to it is an *Undefined Behavior* ("Feedback Loop"). While ping-ponging FBOs or using `glCopyTexSubImage2D` could theoretically solve it, it resulted in performance loss and incorrect mapping.

## 2. The Solution: Native Vulkan 1.4 & Khronos Guidelines
Following a debate tracking the official `/Users/jeanxpereira/CodingProjects/Vulkan-Guide`, specifically `tile_based_rendering_best_practices.adoc` and `memory_allocation.adoc`:

1. **Vulkan Memory Architecture**: We strictly avoid native `vkAllocateMemory` per-object. We use **VMA (VulkanMemoryAllocator)** to pre-allocate massive memory blocks.
2. **The Feedback Loop Solution**: We use **Vulkan 1.4 Dynamic Rendering** alongside `VK_KHR_dynamic_rendering_local_read` (or `VK_EXT_shader_tile_image`). 
   - This enables the Fragment Shader to use `subpassLoad()` to read the exact tile memory pixel it is rasterizing over WITHOUT violating synchronization rules. This perfectly maps to the PS2's raw VRAM read/write capabilities!

## 3. Scope & Target Constraints
- **Hardware Target**: Windows / AMD RDNA2 (RX 6750 XT). 
- **macOS Support**: Vulkan portability on Mac relies on **MoltenVK** or **KosmicKrisp** (Mesa-based, Apple Silicon only, Vulkan 1.3 conformant). The Vulkan Loader in SDK 1.4+ selects the best ICD at runtime — no code changes needed. We use `vk-bootstrap` which natively requests `VK_KHR_portability_subset` enabling MoltenVK directly transparently.
- **CI Pipeline**: GitHub Actions builds both Windows and macOS on every push via `jakoch/install-vulkan-sdk-action`.

## 4. The 4-Tier Zero-Bloat Architecture
*See `CrystalClockVK-ImplPlan.md` for full structure.*

1. **`core/`**: Boilerplate lifecycle. `VulkanContext` (initializes vk-bootstrap), `WindowContext` (SDL3), and `RenderDocWrapper`. 
2. **`renderer/`**: The Lean Vulkan wrapper. Exposes modern Vulkan cleanly: `PassRecorder`, `ResourceManager`, `PipelineBuilder`, `DeletionQueue`, and `UIRenderer` (Dear ImGui). NEVER expose `VkDevice` outside contexts—everything accesses via `const VulkanContext&`.
3. **`gs/`**: STRICTLY PS2 logic. No Vulkan definitions `Vk*` exist here. `SwizzleEngine`, `TextureDecoder`, `GsRegisterState`. Returns decoded VRAM vectors `std::vector<uint8_t>` to be consumed by `ResourceManager`. Perfect unit-testability.
4. **`app/`**: Application state loop. Uses push constants (< 128 bytes limit!) for generic variables (`renderPass` ID, `rodAlpha`) and an object-level UBO for matrices (`model`, `mNormal`).

## 5. Ghidra MCP Analysis Discoveries (Re-evaluated 2026-05-13)
Through deep decompilation of `OSDSYS.elf` AND patent US6,693,606 analysis, key findings:
1. **Rotation Chain (REVISED)**: VU0 azimuth/elevation rotation theory from `FUN_002732d8` decode (see `docs/ghidra_analysis/vu0_decode.md`) was visually invalid — produced rod clustering not radial ring. **Patent US6,693,606 claim 8 governs:** ring rotates around longitudinal axis of HIGHLIGHTED block (1 rev/60s for OSDSYS single-group variant). Current chain: `X(25° tilt) * Rotate(groupRot, highlightAxis) * Z(i*-30°)`. Per-rod axial spin layered separately.
2. **Shimmer / Pass 3 (DROPPED)**: Pass 3 with offset rotation produced 12 visible ghost rods in output. Patent does not describe shimmer effect. Raylib also disabled it (commented out). Permanently removed from pipeline.
3. **Color & Meshes**: PS2 uses single mesh definition. Two memory buffers (`0x375250` / `0x377e50`) = array split: first holds 4:3 rods, second holds extra widescreen edge rods. Single vertex buffer + depth-write disabled on additive passes matches. Patent specifies blue-AM / red-PM coloring (not implemented yet).
4. **Hour Slider (REVISED)**: Patent S304 — coloring amount based on **minute data** (drain over 60-minute hour), NOT secondsInHour over 3600s. yScale = `1 - minutesInHour/60`. The `0x150` flag in OSDSYS marks active hour rod (P4/P5 routing).

## 6. Current Implementation Status (Updated 2026-05-13)

### ✅ Milestones 1 & 2 — Complete
- Vulkan 1.3 / Sync2 / Dynamic Rendering / VMA enabled.
- Swapchain Triple-buffer, ShaderLoader, Command recording, Dear ImGui.
- 5-Pass pipeline base registered.

### ✅ Milestone 3 & 4 & 5 — Partial / Mostly Complete
| Component | Description |
|-----------|-------------|
| `app/RenderOrchestrator` | Extracted from main.cpp; owns pipelines + mesh + pass recording |
| `app/CrystalMath` | Patent-aligned: forward X tilt, rotation around highlighted rod axis, per-rod axial spin |
| GS Passes | P1 (Glass), P2 (Specular), P4/P5 (Selected Glass + Reverse Fill). **P3 dropped** — visible doubling, no patent backing. |

### ✅ Bug Fixes Applied (2026-05-13 — Patent Alignment Pass)
- **Rotation Chain**: `buildRodMatrix(rodIndex, groupRot, highlightIndex)` = `X(25°) * Rotate(groupRot, highlightAxis) * Z(rodIndex * -30°)`. Group rotates around longitudinal axis of highlighted rod per patent US6,693,606 claim 8. Replaced earlier VU0 azimuth/elevation chain (caused cone clustering) and raylib Z-Y-Z chain (caused dynamic edge-on tilts).
- **Highlight Index**: `getHighlightedRod(hour) = hour % 12` (was hardcoded 0).
- **Selected Rod Fill**: yScale = `1 - minutesInHour/60` (was secondsInHour/3600). Patent: fill drains over 60-minute hour.
- **Bottom-up Fill**: `buildFullRodModel` order changed to `rodMatrix * transMat * scaleMat * axialSpin` so rod base stays fixed at ring radius and tip drains toward base (raylib was top-to-base anchored).
- **Rod Geometry Scale**: Width 1.3×, Length 0.95× via `ROD_WIDTH_SCALE` / `ROD_LENGTH_SCALE`. PS2 reference aspect ~3:1.
- **Tunnel Mesh Axis**: cylinder mesh regenerated along local Y (was local Z). After `translate(0,0,30) * rotateX(270°)` matrix, tunnel correctly extends along world -Z from camera. Without this fix tunnel rendered along world Y (above camera, invisible).
- **Projection**: `buildGsProjection(fov, aspect, near, far)` uses real window aspect from `params.aspect`. Removed bogus halfWidth/`(2hw/(2hw/16:9))` collapse formula that hardcoded 16:9.
- **Per-rod Axial Spin**: `buildAxialSpin(i, totalTime)` rotates each rod around its local Y at `TAU/8` rad/s with `i*PI/6` phase offset. Visible individual rotation.
- **Pass Color/Alpha**: highlighted rod color = `prismColor * 2.2 + 0.4` with alpha 0.85/0.9. Was dark blue (invisible highlight).
- **Dropped Pass 3**: shimmer offset rotation was producing 12 ghost rods visible in output. Patent has no shimmer concept; raylib also disabled it. Draw count: 25 (was 39).

### 🚧 Remaining Constraints / Next Steps
- **Refraction**: `Crystal.frag` rod color renders near-white instead of glass-blue sampling tunnel. Refraction UV (`fragScreenUV + N.xy * 0.05`) likely broken — Vulkan Y-flip or bgTexture binding. Needs raw-sample debug.
- **Bump Mapping**: Patent S208/S309 requires bump map on blocks. Currently no normal sampling.
- **AM/PM Color**: Patent — blue AM, red PM. Currently 3-color cycle (Deep Blue → Violet → Teal).
- **Inner Sphere + Light Spot Orbs**: Patent FIG. 10 element. Math exists in `CrystalMath::ORBS` but no pipeline/mesh hooked up.
- **FXAA Shader**: Deferred.

*Note (Updated Framebuffer Refraction):* `VK_KHR_dynamic_rendering_local_read` available but Crystal pipeline currently samples `tunnelImageView` as combined image sampler (via Copy → SHADER_READ_ONLY) — simpler path than feedback loop. Switch to subpassLoad once refraction visually verified.

## 7. Milestone Roadmap Summary

| # | Milestone | Risk | Status |
|---|-----------|------|--------|
| 1 | Core Hardening & Swapchain | 🟢 Low | ✅ Complete |
| 2 | Renderer Abstractions | 🟡 Med | ✅ Complete |
| 3 | GS-VM Pure Logic | 🟡 Med | 🟡 Partial |
| 4 | Crystal Geometry & Math | 🟢 Low | 🟡 Partial |
| 5 | **5-Pass Render Pipeline** | 🔴 High | ✅ Complete |
| 6 | ImGui Debug Overlay | 🟢 Low | ✅ Complete |
| 7 | CI Pipeline (Win+Mac) | 🟢 Low | ✅ Complete |
| 8 | Polish & Final Parity | 🟡 Med | ❌ |
