# Native clock — design

Date: 2026-10-03. Status: first native slice.

## Goal

A clean reimplementation of the PS2 OSD crystal clock screen as native C++ and Vulkan: scene code
with named objects, a native renderer with a normal graphics pipeline, running live in a window on
real time. It looks like the PS2 OSD at any resolution (the faithful base); modern improvements
come later, each one a switch on top of that base.

The GS-parity renderer of `2026-10-03-native-vulkan-renderer-design.md` stays as a measuring rule
only. Nothing in this slice emulates the GS.

## Decisions

| Decision | Choice |
|---|---|
| Visual target | Faithful base at any resolution first; improvements later as switches over it |
| Scope | The clock screen only: background, 12 rods (refracted, textured, reflection, extra passes), 7 orbs (trail, sprites), head blur and copies, tint, vignette. No text, menus, cubes, transitions, PAL |
| Platform | Windows, Vulkan 1.4, RX 6750 XT baseline |
| Canon build | HDD OSD 1.10U, NTSC |
| Source of truth | `facts/` (rules), `References/model/clock_*.mjs` (executable model), `whole3-clock` captures |
| Old code and documents | Deleted. Clean reimplementation |

## Cleanup (first task)

Delete everything that predates `facts/` and would mislead a reader:

- `src/app/`, `src/gs/`, `src/core/{VulkanContext,WindowContext,RenderDocWrapper}.*`,
  `src/renderer/` except `GsParityRenderer.*`, `src/main.cpp`.
- `shaders/` except `GsParity.*`; `resources/`.
- `context/`, the repository `MEMORY.md`, `docs/ANALYSIS-Raylib-vs-Vulkan-vs-OSDSYS.md`,
  `docs/ghidra_analysis/`.
- `.cache/clangd/` (tracked by mistake), `imgui.ini`, `run.bat`, `run.sh`, logs.
- The old application target in `CMakeLists.txt`; `CMakePresets.json` rewritten for Windows.

Keep: `facts/`, `References/`, `docs/clock_patent/`, `docs/superpowers/`, `.claude/`,
`tools/`, `watson.json`, `3rdparty/`, the parity rule (`src/parity/`, `GsParityRenderer`,
`shaders/GsParity.*`, `tools/parity/`, `tools/ParityTool/`, its tests).

`CLAUDE.md` is rewritten from the current truth: the layers below, the method, the commit rules.
The CI workflow builds the new targets.

## Architecture

| Layer | Role | Depends on |
|---|---|---|
| `scene/` | Plain C++. The clock: state, camera, placement, ramps, scale, frame assembly. Builds one `scene::Frame` per frame | nothing |
| `render/` | `Device` (instance, device, queues, VMA, swapchain), `NativeRenderer` (executes a `scene::Frame`) | `scene/` |
| `app/` | Window (SDL3), real time, the frame loop, ImGui debug panel | all |
| `parity/` | Measuring rule only: `GsFrame`, `GsParityRenderer`, fixture loader, converter `scene::Frame` → `GsFrame` | `scene/` |

### `scene/`

Each part ports rules that are verified on both builds, with the facts page named beside it:

| Part | Rules | Facts page |
|---|---|---|
| `ClockState` | current rod `(int)hours % 12`; rod and seconds angles eased 0.1 per frame on 16-bit differences; progress `t` chases `1 − minutes/60` by 0.004; base colour walks a 5-colour table one unit every 9th frame; rod colours chase targets one unit per frame; appearance ramp | `clock-state.md` |
| `Camera` | screen matrix `ViewScreen(512, 1, 0.47, 2048, 2048, 1, 16777215, 1, 65536)`; view from position and rotation, the approach offset × 0.97 per frame | `clock-camera.md` |
| `Placement` | rod matrix chain, orb orbits, sine table divisor 16385, scene scale easing | `clock-camera.md`, `clock-scene.md` |
| `Orbs` | ring of 50 entries, one point every 3 frames, fade ramp, sprite sizes | `clock-orbs.md` |
| `Rods` | the five sends of a rod, the split rod, texture offsets, the two extra passes | `clock-rod-draw.md`, `clock-extra-passes.md` |
| `FrameHead` | background tube, blur trips, copies to the work buffers, tint, vignette | `clock-frame-rest.md` |

Arithmetic: plain `float` by default. An `EeFloat` policy (round toward zero, no denormals, the
EE's `sinf`/`cosf`) is a template parameter of the state and placement code, so the same code runs
exact for the tests and plain for the app.

### `scene::Frame` (native)

```
Frame
  targets : Display, RefractionSource, Work           (sizes relative to the output resolution)
  passes  : Pass[]
Pass
  name     : Background | HeadBlur | Copy | Tint | RodRefraction | RodTextured | RodReflection |
             ExtraPass | OrbTrail | OrbSprite | Vignette
  target   : target name
  mesh     : Mesh (positions in clip space or view space, uv, colour 0..1)   or   a full-screen quad
  material : texture (named image or target), sampling (repeat | clamp | clamp to a region),
             blend (Opaque | AlphaOver | Add | Subtract | Modulate), depth (test, write)
```

No GS registers, block addresses, 12.4 integers, 0x80-means-one alphas, AA1 or skips.

### `NativeRenderer`

- Graphics pipeline with dynamic rendering; one pipeline per (blend, depth, topology) used.
- Targets as colour attachments at the output resolution; the refraction samples its source
  target as a texture after a barrier.
- Blend by fixed function: the GS formulas the clock uses map to `src·α + dst·(1−α)`,
  `src·α + dst`, `dst − src·α` with α = alpha/128 converted at load.
- MSAA (off, 2×, 4×, 8×) replaces edge antialiasing.
- Resolution: 640×448 (NTSC frame), window size, or a multiplier.
- Textures: the ten clock textures from `References/textures/` (extracted by
  `References/scripts/extract_rom_textures.mjs`), uploaded once.
- Rod mesh from `facts/data/rod-mesh.json`.

### `app/`

SDL3 window, swapchain present, real local time feeding `ClockState`, fixed 59.94 Hz logic step,
ImGui panel: resolution, MSAA, pause, step one frame, set the time, show a target.

## Testing

| Level | Compares | Against | Criterion |
|---|---|---|---|
| Logic | `scene/` state per frame, `EeFloat` policy | state fixtures exported from the `whole3-clock` captures by the JS model | exact |
| Scene through the rule | `scene::Frame` → converter → `GsFrame` → `GsParityRenderer` | PCSX2 software renderer (existing fixtures and budgets) | the existing gate |
| Native | `NativeRenderer` at 640×224 | the same oracle images | report only: differing share, largest difference, per target |
| Eye | the window | — | Jean's verdict |

The second level proves the native scene produces the same frame as the OSD without the native
renderer emulating anything.

## Slices of this design

1. Cleanup and the new skeleton: `Device`, window, an empty frame presented.
2. `scene/` logic with its exact tests (state, camera, placement, orbs).
3. Frame assembly and the converter; the scene passes the parity gate.
4. `NativeRenderer` drawing the frame; the distance report; the live window.

## Out of scope

Text, menus, System Configuration cubes, transitions, opening, PAL, languages, CRT filter,
improvements beyond resolution and MSAA, macOS.
