# CrystalClockVK — PS2 OSDSYS Crystal Clock Recreation

## Project Overview
Recreation of the PlayStation 2 OSDSYS "Crystal Clock" visual effect. 
**NOTE:** This project recently underwent a massive architectural pivot! We have dropped Raylib and OpenGL completely in favor of a **Native Vulkan 1.4 C++** architecture. The goal remains a **1:1 match** with the original GS render pipeline, using modern GPU techniques to legally and accurately emulate the PS2 Graphics Synthesizer (GS) behavior.

## Core Architectural Directives
Read `MEMORY.md` and `CrystalClockVK-ImplPlan.md` in this directory immediately upon entering the project.
The codebase is structured into four strict layers:
1. `core/`: Vulkan Bootstrap via vk-bootstrap, SDL3.
2. `renderer/`: Lean Vulkan 1.4 Wrapper (VMA, PassRecorder, Dynamic Rendering).
3. `gs/`: PS2-specific pure math and memory logic (No Vulkan API calls allowed here).
4. `app/`: Crystal Clock orchestration and pass dispatch.

## Build System
- **CMake 3.30+**, C++23 standard (cross-platform compatible with MSVC/Clang/GCC)
- SDL3, VulkanMemoryAllocator (VMA), vk-bootstrap, GLM, Dear ImGui via `FetchContent`/Submodules
- Target Environment: Windows (AMD RDNA2 / RX 6750 XT baseline) + macOS (Apple Silicon via MoltenVK/KosmicKrisp)
- **CI**: GitHub Actions auto-builds Windows + macOS on every push (`.github/workflows/build.yml`)

## Documentation Reference
To guarantee a 1:1 cleanroom port of the OSDSYS effects, rely on these resources:
- **`MEMORY.md`**: Contains the complete historical context, architectural decisions, and Vulkan strategies established during the planning phase.
- **US Patent 6,693,606 (`docs/clock_patent/US6693606.pdf`)**: Proves the exact method of refraction (rendering framebuffer background, sampling it distorted across transparent blocks).
- **Vulkan-Docs & Vulkan-Guide**: Local repositories at `/Users/jeanxpereira/CodingProjects/Vulkan-Guide/chapters/` specifically `memory_allocation.adoc` (VMA setup) and `tile_based_rendering_best_practices.adoc` (VK_KHR_dynamic_rendering_local_read for GS FBO feedback loop equivalence). Note: `VK_EXT_attachment_feedback_loop_layout` is largely unsupported on macOS MoltenVK—we therefore explicitly rely on `subpassLoad()` without throwing layout validation errors in `VulkanContext`.
- **`ghidra-mcp`**: MANDATORY server for disassembling logic directly from `OSDSYS.elf` to construct the `gs/` logic correctly.
- **`pcsx2-mcp`**: Live PCSX2 DebugServer bridge (`.mcp.json`). Use to read live EE registers (esp. GP for resolving `fGpffff*` globals), set conditional BPs inside OSDSYS code, and trace GIF/VIF DMA packets during real frames. PC always reads as `0x00081fc0` (BIOS idle) when paused without a BP — set BP inside OSDSYS code to capture true context.
- **CrystalOSD decomp**: `D:\CodingProjects\CrystalOSD\asm\clock\` contains spimdisasm output of clock module. **`clock_orb_rendering_func.s`** is the master per-frame render at `0x00225E80`, not `0x00223f78` as earlier docs claimed.

**WARNING — known-bad docs**: `docs/ghidra_analysis/rod_analysis.md` and `docs/ANALYSIS-Raylib-vs-Vulkan-vs-OSDSYS.md` contain errors corrected in `MEMORY.md §7`:
- `DAT_002973a0` / `DAT_002973c0` are Shift-JIS text pointers, NOT GS primitives.
- `fGpffff8c28` is the `"Reset"` ASCII string, NOT FOV.
- Per-pass angle step is `0.26 rad` / `0.33 rad`, NOT 30° / rod.
- Master clock render entry is `0x00225E80`, NOT `0x00223f78`.
Ignore Raylib `crystal.fs` as a visual reference — pixel-accurate parity must come from PCSX2 live trace, not Raylib port.

## Shader Pipeline & GS Blending Setup
The PS2 GS renders the clock in 5 distinct passes using the same `DAT_002973*` primitives with different `ALPHA` register values. We emulate this by binding different prebuilt Vulkan Pipelines:
| Pass | PS2 Alpha / Purpose | Vulkan Blend Mode |
|---|---|---|
| **1** | `(1,0,1)` - Base Glass FB Refraction | `VK_BLEND_FACTOR_SRC_ALPHA` / `DST_ALPHA` + `VK_KHR_dynamic_rendering_local_read` (read tile memory) |
| **2** | `(2,1,2)` - Additive Edge Highlight | `VK_BLEND_FACTOR_ONE` / `ONE` |
| **3** | `(0,2)` - Additive Angular Offset | `VK_BLEND_FACTOR_ONE` / `ONE` (with normal offsets pushed via push constants) |
| **4** | `(1,0,1)` - Active Rod Base Refract | Same as Pass 1 |
| **5** | `(0,1,1)` - Active Rod Slider Fill | `VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA` / `SRC_ALPHA` (Reverse Alpha fill) |

*Always follow PascalCase boundaries, explicit RHI-less lean wrappers, and strictly isolated GS testability.*

## Commit Convention
Format: `Type(Scope): Short imperative description`

**Types** (PascalCase):
| Type | When |
|---|---|
| `Fix` | Bug fix, broken rendering, crash |
| `Feat` | New feature, pass, shader, mesh |
| `Refactor` | Code restructure, no behavior change |
| `Perf` | Performance optimization |
| `Build` | CMake, CI, dependencies, SDK |
| `Docs` | MEMORY.md, CLAUDE.md, comments |
| `GS` | PS2 GS/VU0 reverse-engineering changes |

**Scopes** (PascalCase, match directory/module):
`Core`, `Renderer`, `App`, `GS`, `Shaders`, `CI`, `Project`

**Rules**:
- Max 72 chars in subject line
- Imperative mood ("Fix" not "Fixed", "Add" not "Added")
- Body optional, separated by blank line, explains *why* not *what*

**Examples**:
```
Fix(Shaders): Restore actual lighting output from debug hardcodes
Feat(CI): Add GitHub Actions Windows+macOS build pipeline
GS(App): Implement VU0 azimuth rotation from OSDSYS decode
Build(Project): Downgrade C++26 to C++23 for MSVC compat
Refactor(Renderer): Extract depth transition into PassRecorder
```

## Code Directives
- **English Only**: The codebase (variables, structures) and all text must be strictly in English.
- **Zero Comments**: Avoid unnecessary comments. Use comments ONLY when it is expressly necessary to explain highly complex logic (e.g. GS reverse-engineered math).

## Claude Directives
- Use short, 3-6 word sentences.
- No filter, preamble, or pleasantries.
- Run tools first, show the result, then stop. DO not narrate.
- Drop articles(“Me fix code” not “will fix the code”).
- Mantain CLAUDE.md and MEMORY.md updated