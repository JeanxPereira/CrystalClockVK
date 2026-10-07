# CrystalClockVK — PS2 OSDSYS Crystal Clock, native Vulkan

## Project Overview
Native C++23 / Vulkan 1.4 reimplementation of the PlayStation 2 OSDSYS Crystal Clock: the clock, the menus and the opening, running in an SDL3 window on local time at 59.94 Hz. This branch (`main`) holds only the app. Research and proof (the verified `facts/`, `References/` scripts and model, the tests, the GS parity rule and its tools, the design docs, the agents and skills) live on the long-lived branch `lab`, checked out at `D:/CodingProjects/CrystalClockVK-wt/lab`.
The Version page (Triangle on the main menu) is native: `Menus::versionOpen`/`versionStep`, `Text::versionPage`; the console's rows are `MenusState::versionList` (the cold start's default is the capture's three). Not ported: the front page of a row with sub-rows, the row-enter sound and the sub-page sound (`0x6300` with 4 and 0xA).
Sounds: the scene queues the EE's sound commands where the console's senders do (`scene::SoundCommand`, `Clock::sounds()`, the opening's sounds); `audio::EeSoundQueue` is the EE ring (128 entries, the `0x6300` dedup, carried `a2`/`a3`), drained once per logic frame into `ClockSound`. Wired: main menu, System Configuration (list, entries, 2- and 3-value entries, Clock Adjustment), Square hide/show, Version moves and back, the clock thread start and set-up pair (`kClockInitFrames` after the start), the opening start and stage 2. The `0x60D0` ramp and `0x8070` are sent from `ClockSound`'s table. Open (not wired or not measured): disc-state senders, the Browser's own sounds, the Options dialog, the drain skip cause during the ramp, the IOP spacing of sends within one drain, the ring's start head, `a3` where the EE leaves a stale register (sent as 0).

The main checkout keeps its untracked `References/`, `facts/`, `tests/` and the rest as the data home (dumps, BIOS, fixtures, captures). They are git-ignored here. Never delete them. The lab worktree reads data from here through `CLOCK_REFERENCES`.

## Layers
| Layer | Role |
|---|---|
| `scene/` | Plain C++, templated on `Arithmetic` (`EeArithmetic` exact, `NativeArithmetic` in the app). State, camera, rods, orbs, frame head, text, menus, cubes, the opening. One `scene::Frame` per frame. No Vulkan |
| `render/` | `Device` (instance, device, queues, VMA, swapchain) and `NativeRenderer` (executes a `scene::Frame`: hardware blend, MSAA, any resolution) |
| `assets/` | Plain C++, no Vulkan: the console's raw resource files decoded (ROMDIR, HDD container decrypt, Expand, `func_002344F8`), the BIOS extractor |
| `app/` | `CrystalClock`: SDL3 window, host inputs (`settings.json`, local time), cold start, logic, ImGui debug panel, screens, boot chain |
| `core/` | VMA and stb_image units, the headless GPU context |

Also: `shaders/` (GLSL, compiled to SPIR-V by `glslc`), `3rdparty/` (ImGui submodule, stb).

## Data rule
The console's raw resource files (TEXIMAGE, FNTOSD, SNDIMAGE, ICOIMAGE, JISUCS, SKBIMAGE, hddosd.elf) are committed in `resources/` by Jean's decision (2026-10-05); the build copies them to `bin/resources`. Other Sony data (dumps, BIOS, captures, decoded textures or meshes) is never committed. The app decodes the raw files at each start, no cache: `bin/resources`, or `--resources dir`; `--bios rom.bin` extracts into that folder. Loose files (`--textures`, `--font`, `--program`, `--mesh`, `--cube-mesh`, `--opening-textures`) are used only when named on the command line. With files missing, the app prints what is missing and exits with code 1.

## Build and run
- CMake 4.2+, C++23, Windows only (Vulkan 1.4, `glslc` from the Vulkan SDK, Visual Studio 2026 with Ninja).
- `tools/build.ps1 configure app`, then `tools/build.ps1 build app-debug` (or `app-release`). Binary: `bin/CrystalClock.exe`.
- Check: `bin/CrystalClock.exe --soak 10` (opens with the boot; `--skip-boot` starts at the menu) must report 0 validation errors; `--validate sync` adds synchronization validation.
- Regression gate: `bin/CrystalClock.exe --golden check <file>` replays a fixed time and a scripted pad headless and compares each frame's scene, pixel and audio hash against a recorded run (`--golden record <file>`; `--golden-output x2-msaa4` for the 2x MSAA 4 output). Record a baseline before a change, check after; any differing frame fails. Baselines are per build type and per machine (the RTC mirror depends on the time zone).
- Profile: `--profile` with a soak prints produce, record and present means and worst per screen (`--skip-boot --soak 58 --mute --profile`).
- Debug builds compile the audio emulation (`ClockAudioSpu2`, `ClockAudioDriver`) with `/O2`; the rest keeps `/Od` and runtime checks.
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
