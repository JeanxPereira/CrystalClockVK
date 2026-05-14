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

## 7. PCSX2 Live Trace Findings (2026-05-14)

Live RAM/register dump via PCSX2 DebugServer (MCP `pcsx2`) while OSDSYS rendered the crystal clock. **Corrects multiple errors in `docs/ghidra_analysis/rod_analysis.md`**.

### Key live values
- **GP register = `0x002AF070`** (captured at BP inside OSDSYS thread)
- **Real master clock render = `0x00225E80`** (`clock_orb_rendering_func` in CrystalOSD decomp at `D:\CodingProjects\CrystalOSD\asm\clock\clock_orb_rendering_func.s`). The docs' `0x00223f78` was a mid-function tail.
- **Rod render entries = `func_00232640` and `func_00232878`** (called from `clock_orb_rendering_func`).

### GP-relative constants — REAL values
| Symbol | Absolute | Float value | Purpose |
|--------|----------|-------------|---------|
| `fGpffff832c` | `0x002A739C` | **0.26 rad (~15°)** | Pass 2 specular angle step / rod |
| `fGpffff8330` | `0x002A73A0` | **0.33 rad (~19°)** | Pass 3 offset angle step / rod |
| `fGpffff8334` | `0x002A73A4` | 0.38 rad | (unknown 4th step) |
| `fGpffff8338` | `0x002A73A8` | 0.80 | scalar |
| `fGpffff833c` | `0x002A73AC` | 0.65 | scalar |
| `fGpffff8340` | `0x002A73B0` | **π = 3.14159** | literal |
| `fGpffff8358` | `0x002A73D8` | 0.037 | refraction displacement candidate |
| `fGpffff835c` | `0x002A73DC` | 0.031 | refraction displacement candidate |
| `fGpffff8360` | `0x002A73E0` | 0.028 | refraction displacement candidate |

### CORRECTIONS to prior docs
1. **`DAT_002973a0` / `DAT_002973c0` are NOT GS primitive packets.** They are pointer tables into a Shift-JIS Japanese UI text block (e.g. `"初期化"`, `"%04d/%02d/%02d  %2d:%02d:%02d"`, `"キロバイト"`). Used for OSDSYS clock-screen text formatting, not crystal rod render. `rod_analysis.md` Section 3 / `ANALYSIS-Raylib-vs-Vulkan-vs-OSDSYS.md` Section 2 are wrong about these addresses.
2. **`fGpffff8c28` (= `0x002A7C98`) is NOT FOV.** It holds the ASCII string `"Reset"`. Old documentation extrapolated this address from incomplete Ghidra context.
3. **Per-pass angle step is NOT 30° (360/12) per rod.** It is `0.26 rad` (P2) / `0.33 rad` (P3) — much smaller, creating subtle specular shimmer not full re-rotation. Old `ANGLE_STEP = 360/60 = 6°` and current `rodIndex * (-PI/6) = -30°` in `app/CrystalMath::buildRodMatrix` are both wrong inputs for the specular passes.
4. **Master render entry is at `0x00225E80`, not `0x00223f78`.** Decomp confirms the call chain: `clock_orb_rendering_func` → `func_00232640` (rod glass) + `func_00232878` (rod specular) + orb/trail functions.

### Live rod struct layout (0x0034F9C0, stride 0x140)
**Corrects** `rod_analysis.md` which claimed 0x160 stride and active flag at +0x150.
| Offset | Type | Meaning |
|--------|------|---------|
| +0x00 | ptr | next/prev rod (linked list) |
| +0x04 | float | (probably radius or sort key) |
| +0x10 | int | counter |
| +0x18..0x24 | 3×ptr | per-rod resource handles (0x0027Exxx) |
| +0x30..0x5F | 12×float | 3×4 transform matrix (3 basis rows × xyzw) |
| +0x60..0x6F | vec4 | Position in world (matches a2 arg passed to render) |
| +0x70..0x77 | 2×ptr | (0x00348Dxx range) |
| +0x78..0x84 | vec3+pad | Scale (1,1,1) |
| +0x90..0x9F | RGBA u32×4 | **Pass 1 glass color (45,87,102,128)** — desaturated, NOT bright blue |
| +0xB0..0xBF | u32×4 | (8,8,8,128) – possibly per-rod multiplier flags |
| +0xC0..0xCF | float×4 | (-0.008, -0.008, 1.0, ...) – tiny displacement |
| +0xD0..0xDF | RGBA u32×4 | (60,60,60,128) – additive pass color |
| +0xE0..0xEF | RGBA u32×4 | (40,40,40,128) – offset pass color |
| **+0xF0** | **int** | **Selection flag (active hour rod = 1)** |

**Rod array base in live RAM = `0x0034E980`** (which holds pointer `0x0034F9C0` to actual rod data linked list). Stride between rod structs = `0x140` bytes.

### Real GS primitive color (from rod struct +0x90)
First rod's Pass 1 glass tint: `RGBA(45, 87, 102, 128)` ≈ `(0.176, 0.341, 0.400, 0.502)` normalized. Desaturated cool-blue, NOT the bright `(0.04, 0.23, 0.46)` Deep Blue currently lerped in `CrystalMath::lerpPrismColor`. The PS2 rod is far less saturated than current Vulkan port.

### PCSX2 MCP usage notes
- BP-by-address works (after one warm-up frame). Setting BP at function entry sometimes fails to hit if Ghidra-named start is mid-function — pick an address that contains a real prologue (`addiu sp, -N` + `sd ra, ...`).
- `pcsx2_pause` always reports PC=`0x00081fc0` (BIOS idle wait) regardless of game state — the EE yields between frames. To read live GPR / GP from OSDSYS context, set a BP inside OSDSYS code and let it fire.
- `.mcp.json` at project root contains the server config: `node D:\DownloadLibrary\PCSX2-MCP-v1.0.0-win64\...\index.js`.

## 8. Milestone Roadmap Summary

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
