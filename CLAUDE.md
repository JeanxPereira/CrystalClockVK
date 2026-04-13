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
- **CMake 4.3.1**, C++26 standard
- SDL3, VulkanMemoryAllocator (VMA), vk-bootstrap, GLM, Dear ImGui via `FetchContent`/Submodules
- Target Environment: Windows (AMD RDNA2 / RX 6750 XT baseline). Support for macOS exists via MoltenVK automatically injected by vk-bootstrap portability subset.

## Documentation Reference
To guarantee a 1:1 cleanroom port of the OSDSYS effects, rely on these resources:
- **`MEMORY.md`**: Contains the complete historical context, architectural decisions, and Vulkan strategies established during the planning phase.
- **US Patent 6,693,606 (`docs/clock_patent/US6693606.pdf`)**: Proves the exact method of refraction (rendering framebuffer background, sampling it distorted across transparent blocks).
- **Vulkan-Docs & Vulkan-Guide**: Local repositories at `/Users/jeanxpereira/CodingProjects/Vulkan-Guide/chapters/` specifically `memory_allocation.adoc` (VMA setup) and `tile_based_rendering_best_practices.adoc` (VK_KHR_dynamic_rendering_local_read for GS FBO feedback loop equivalence). Note: `VK_EXT_attachment_feedback_loop_layout` is largely unsupported on macOS MoltenVK—we therefore explicitly rely on `subpassLoad()` without throwing layout validation errors in `VulkanContext`.
- **`ghidra-mcp`**: MANDATORY server for disassembling logic directly from `OSDSYS.elf` to construct the `gs/` logic correctly.

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

## Code Directives
- **English Only**: The codebase (variables, structures) and all text must be strictly in English.
- **Zero Comments**: Avoid unnecessary comments. Use comments ONLY when it is expressly necessary to explain highly complex logic (e.g. GS reverse-engineered math).

## Claude Directives
- Use short, 3-6 word sentences.
- No filter, preamble, or pleasantries.
- Run tools first, show the result, then stop. DO not narrate.
- Drop articles(“Me fix code” not “will fix the code”).
- Mantain CLAUDE.md and MEMORY.md updated