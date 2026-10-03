# CrystalClockVK — PS2 OSDSYS Crystal Clock Recreation

## Project Overview
Recreation of the PlayStation 2 OSDSYS "Crystal Clock" visual effect. 
**NOTE:** This project recently underwent a massive architectural pivot! We have dropped Raylib and OpenGL completely in favor of a **Native Vulkan 1.4 C++** architecture. The goal remains a **1:1 match** with the original GS render pipeline, using modern GPU techniques to legally and accurately emulate the PS2 Graphics Synthesizer (GS) behavior.

## Core Architectural Directives
Read `facts/README.md` first: it is what the PS2 OSD does, verified, and the list of what is left.
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

## Source of truth and method
- **`facts/` is the only trusted store** (`facts/README.md` first; `facts/verification.md` is the verifier × build matrix). `docs/`, `context/` and `MEMORY.md` predate it and mix builds: never cite them as fact. The old "5 passes" table, the Raylib reference and the old docs' addresses are superseded by `facts/`.
- **Two builds**: HDD OSD 1.10U (canon, `hddosd.elf`, SHA-1 `e932f350…`) and ROM 2.30 (second witness). Every address names its build.
- **A fact is a verifier passing**: a script in `References/scripts/` recomputes a function's output from probed inputs, bit for bit, on both builds, survives mutation (`mutate.mjs`) and is in the regression suite (`run_all.mjs`). Skills: `measure` (the method), `capture` (the emulator), `facts-page` (writing a page), `campaign` (many items), `commit`.
- **Instruments**: Watson (`D:\CodingProjects\Watson`, MCP `watson_*`: an instrumented PCSX2 with probes, fast captures, GS traces and GS memory reads); HDD OSD disassembly in `D:\CodingProjects\CrystalOSD\asm\`; Ghidra/IDA MCP when the disassembly is not enough.
- **Agents** (`.claude/agents/`, mid tier by default): `re-scout` (one question, read-only), `verifier-writer` (one verifier), `re-refuter` (adversarial check of a claim or a page), `doc-writer` (one facts page). Prefer them, briefed by file path, over forks of the session.
- **Hooks**: the git rules are refused at the call (`tools/hooks/guard-git.py`); every edit to `facts/` is linted (`tools/hooks/lint-facts.py`).

## Vulkan references
- **US Patent 6,693,606 (`docs/clock_patent/US6693606.pdf`)**: the intent of the effect (refraction of the framebuffer through transparent blocks); it holds no numbers.
- **Vulkan-Docs & Vulkan-Guide**: Local repositories at `/Users/jeanxpereira/CodingProjects/Vulkan-Guide/chapters/` specifically `memory_allocation.adoc` (VMA setup) and `tile_based_rendering_best_practices.adoc` (VK_KHR_dynamic_rendering_local_read for GS FBO feedback loop equivalence). Note: `VK_EXT_attachment_feedback_loop_layout` is largely unsupported on macOS MoltenVK—we therefore explicitly rely on `subpassLoad()` without throwing layout validation errors in `VulkanContext`.
## Rendering
The GS work of every frame (draws, buffers, blends, depth tests, texture state) is in `facts/clock-frame.md`, `facts/clock-rod-draw.md`, `facts/clock-gs-state.md` and the pages they link; the Vulkan design follows those pages, with the verifiers and `References/model/clock_frame.mjs` as its tests.

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