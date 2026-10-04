# CrystalClockVK — PS2 OSDSYS Crystal Clock, native Vulkan

## Project Overview
Native C++23 / Vulkan 1.4 reimplementation of the PlayStation 2 OSDSYS Crystal Clock: the clock, the menus and the opening, running in an SDL3 window on local time at 59.94 Hz. This branch (`main`) holds only the app. Research and proof (the verified `facts/`, `References/` scripts and model, the tests, the GS parity rule and its tools, the design docs, the agents and skills) live on the long-lived branch `lab`, checked out at `D:/CodingProjects/CrystalClockVK-wt/lab`.

The main checkout keeps its untracked `References/`, `facts/`, `tests/` and the rest as the data home (dumps, BIOS, fixtures, captures). They are git-ignored here. Never delete them. The lab worktree reads data from here through `CLOCK_REFERENCES`.

## Layers
| Layer | Role |
|---|---|
| `scene/` | Plain C++, templated on `Arithmetic` (`EeArithmetic` exact, `NativeArithmetic` in the app). State, camera, rods, orbs, frame head, text, menus, cubes, the opening. One `scene::Frame` per frame. No Vulkan |
| `render/` | `Device` (instance, device, queues, VMA, swapchain) and `NativeRenderer` (executes a `scene::Frame`: hardware blend, MSAA, any resolution) |
| `assets/` | Plain C++, no Vulkan: the console's raw resource files decoded (ROMDIR, HDD container decrypt, Expand, `func_002344F8`), the BIOS extractor, the `assets.bin` cache |
| `app/` | `CrystalClock`: SDL3 window, local time, logic, ImGui debug panel, screens, boot chain |
| `core/` | VMA and stb_image units, the headless GPU context |

Also: `shaders/` (GLSL, compiled to SPIR-V by `glslc`), `resources/` (start states, JSON), `3rdparty/` (ImGui submodule, stb).

## Data rule
Sony data (textures, font, ELF, meshes, dumps, cipher tables) is never committed. The app decodes the console's raw resource files at start-up: `--bios rom.bin` extracts them from a ROM, `--resources dir` names a folder (TEXIMAGE, FNTOSD, hddosd.elf), the decoded cache is `%LOCALAPPDATA%/CrystalClockVK/assets.bin` (`--assets`). Loose files (`--textures`, `--font`, `--program`, `--mesh`, `--cube-mesh`, `--opening-textures`) are used only when named on the command line. With nothing decoded and no flags, the app prints what is missing and exits with code 1.

## Build and run
- CMake 4.2+, C++23, Windows only (Vulkan 1.4, `glslc` from the Vulkan SDK, Visual Studio 2026 with Ninja).
- `tools/build.ps1 configure app`, then `tools/build.ps1 build app-debug` (or `app-release`). Binary: `bin/CrystalClock.exe`.
- Check: `bin/CrystalClock.exe --boot --soak 10` must report 0 validation errors.
- One `CMakeLists.txt` serves both branches. The root hooks `tests/CMakeLists.txt` in only when `-DCLOCK_BUILD_TESTS=ON` and the file exists (lab).
- CI (`.github/workflows/build.yml`): Windows app build only.

## Lab branch
- Worktree: `D:/CodingProjects/CrystalClockVK-wt/lab`. Tests: `tools/build.ps1 configure tests -DCLOCK_REFERENCES=D:/CodingProjects/CrystalClockVK/References -DCLOCK_DUMPS=<References/dumps/hddosd-host>`, then `build tests-debug`, `test tests-debug`.
- Lab takes main with `git merge main`. Main never takes lab.
- Method, facts, verifiers, agents and the research CLAUDE.md are on lab.

## Commit Convention
Format: `Type(Scope): Short imperative description`

**Types** (PascalCase): `Fix`, `Feat`, `Refactor`, `Perf`, `Build`, `Docs`.
**Scopes** (PascalCase, match directory/module): `Core`, `Renderer`, `Scene`, `App`, `Assets`, `Shaders`, `CI`, `Project`.

**Rules**:
- Max 72 chars in subject line
- Imperative mood ("Fix" not "Fixed")
- Body optional, separated by blank line, explains *why* not *what*
- Never push without being asked.

## Code Directives
- **English Only**: the codebase and all text.
- **Zero Comments**: only where reverse-engineered arithmetic needs one.
- PascalCase file and type names; lean explicit wrappers; `scene/` and `assets/` testable without a window.
- The app must build and run without `References/` or `facts/`.

## Claude Directives
- Use short, 3-6 word sentences.
- No filter, preamble, or pleasantries.
- Run tools first, show the result, then stop. Do not narrate.
- Drop articles ("Me fix code", not "will fix the code").
- Keep CLAUDE.md and the auto-memory MEMORY.md updated.
