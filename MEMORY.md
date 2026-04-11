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
- **macOS Exception**: Developed partly on a Hackintosh. Vulkan portability on Mac relies on **MoltenVK**. Instead of building bloated multi-platform fallback RHI interfaces (Rendering Hardware Interfaces), we use `vk-bootstrap` which natively requests `VK_KHR_portability_subset` enabling MoltenVK directly transparently. We assume no ultra-low-end fallback; `VK_KHR_dynamic_rendering_local_read` is preferred and assumed native on RDNA2/Apple Silicon.

## 4. The 4-Tier Zero-Bloat Architecture
*See `CrystalClockVK-ImplPlan.md` for full structure.*

1. **`core/`**: Boilerplate lifecycle. `VulkanContext` (initializes vk-bootstrap), `WindowContext` (SDL3), and `RenderDocWrapper`. 
2. **`renderer/`**: The Lean Vulkan wrapper. Exposes modern Vulkan cleanly: `PassRecorder`, `ResourceManager`, `PipelineBuilder`, `DeletionQueue`, and `UIRenderer` (Dear ImGui). NEVER expose `VkDevice` outside contexts—everything accesses via `const VulkanContext&`.
3. **`gs/`**: STRICTLY PS2 logic. No Vulkan definitions `Vk*` exist here. `SwizzleEngine`, `TextureDecoder`, `GsRegisterState`. Returns decoded VRAM vectors `std::vector<uint8_t>` to be consumed by `ResourceManager`. Perfect unit-testability.
4. **`app/`**: Application state loop. Uses push constants (< 128 bytes limit!) for generic variables (`renderPass` ID, `rodAlpha`) and an object-level UBO for matrices (`model`, `mNormal`).

## 5. Ghidra MCP Analysis Discoveries (New Truths vs Raylib)
Through deep decompilation of `OSDSYS.elf`, we discovered several critical flaws in the old Raylib math:
1. **Rotation Matrix (Azimuth/Elevation):** The PS2 `BuildRotation` uses two angles fed into a VU0 macro utilizing `VOPMSUB` (Cross Product) for proper orthogonalization. **The rods are NOT laid out as a radial ring with tangent tilts.** They use a proper 2-angle Azimuth/Elevation matrix.
2. **The Shimmer Ghosting Effect:** Pass 2 receives two IDENTICAL angles. Pass 3 receives two DIFFERENT angles offset by system clock parameters (`param_3[0x2c/0x2d]`). This difference forces the VU0 cross-product to generate a slightly skewed rotation, creating the native PS2 "double image" shimmer.
3. **Color & Meshes:** The PS2 uses **two different meshes**. Passes 1, 4, 5 (Glass/Alpha) use a transparent uncolored mesh. Passes 2, 3 (Specular/Additive) use a colored lit mesh. The color is identical for all rods at any given moment and cycles `Deep Blue -> Violet -> Teal` every 10 seconds.
4. **Hour Slider (Prism Scale):** The OSDSYS uses `0x150` flag to mark the active hour rod, enabling Passes 4 and 5 which apply a `yScale` that counts down over the span of the 3600 seconds.

## 6. Current Implementation Status (Updated 2026-04-11)

### ✅ Milestones 1 & 2 — Complete
- Vulkan 1.3 / Sync2 / Dynamic Rendering / VMA enabled.
- Swapchain Triple-buffer, ShaderLoader, Command recording, Dear ImGui.
- 5-Pass pipeline base registered.

### ✅ Milestone 3 & 4 & 5 — Partial / Mostly Complete
| Component | Description |
|-----------|-------------|
| `app/RenderOrchestrator` | Extracted from main.cpp; owns pipelines + mesh + 5-pass recording |
| `app/CrystalMath` | Procedural math wrapper (Currently needs Azimuth/VU0 update) |
| GS Passes | P1 (Glass), P2 (Specular), P3 (Offset), P4/P5 (Fill/Highlight) established |

### 🚧 Remaining Constraints / Next Steps
- Implement VU0 Azimuth Matrix Math in `CrystalMath`.
- Implement Orbs/Trails and FXAA shaders.

*Note (Updated Framebuffer Refraction):* `VK_KHR_dynamic_rendering_local_read` has been successfully implemented using SubpassLoad on the exact Tile pixel for Pass 1 Glass. We learned that `VK_EXT_attachment_feedback_loop_layout` is largely unsupported on macOS/MoltenVK, so we enforce the feedback loop behavior explicitly without it to bypass device selector validation errors. We simulate refraction via an algorithmic chromatic displacement directly on the fetched pixel's luminance instead of physical UV offsets, due to TBDR `subpassLoad()` constraints.

## 7. Milestone Roadmap Summary

| # | Milestone | Risk | Status |
|---|-----------|------|--------|
| 1 | Core Hardening & Swapchain | 🟢 Low | ✅ Complete |
| 2 | Renderer Abstractions | 🟡 Med | ✅ Complete |
| 3 | GS-VM Pure Logic | 🟡 Med | 🟡 Partial |
| 4 | Crystal Geometry & Math | 🟢 Low | 🟡 Partial |
| 5 | **5-Pass Render Pipeline** | 🔴 High | ✅ Complete |
| 6 | ImGui Debug Overlay | 🟢 Low | ✅ Complete |
| 7 | Polish & Final Parity | 🟡 Med | ❌ |
