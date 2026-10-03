# Native Clock — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A clean native C++/Vulkan reimplementation of the PS2 OSD clock screen, live in a window on real time, whose scene is proven equal to the OSD through the existing GS-parity rule.

**Architecture:** `scene/` ports the verified clock logic (state, camera, placement, orbs, rods, frame head) into plain C++ and builds a native `scene::Frame` (named passes, meshes, materials). `render/` draws it with a normal graphics pipeline. A converter turns a `scene::Frame` into the parity rule's `parity::GsFrame`, so the scene is gated by the PCSX2 oracle without the native renderer emulating anything.

**Tech Stack:** C++23, Vulkan 1.4, vk-bootstrap, VMA, SDL3, Dear ImGui, nlohmann/json, CMake/CTest; Node 25 for the fixture exporter.

**Spec:** `docs/superpowers/specs/2026-10-03-native-clock-design.md`

## Global Constraints

- English only. No comments except where the OSD's arithmetic needs one; every scene function names its facts page in one line (`// facts/clock-state.md`).
- Commits `Type(Scope): Short imperative description`, ≤ 72 chars, no attribution lines. Never push. Never `git add` a directory.
- Windows, Vulkan 1.4, RX 6750 XT. No MoltenVK paths.
- Canon build HDD OSD 1.10U, NTSC. Captures: `D:/CodingProjects/Watson/Runtime/captures/hddosd-110U-whole3-clock.*`.
- The parity rule (`src/parity/`, `GsParityRenderer`, `shaders/GsParity.*`, `tools/parity/`, `tools/ParityTool/`, its tests and budgets) is kept and never weakened.
- The JS model (`References/model/clock_*.mjs`, `ee_libm.mjs`) is the executable reference: ports follow its arithmetic and call order exactly; where it reads EE memory, the port reads a named field.
- Arithmetic policy: scene code is templated on `Arithmetic` (`EeArithmetic` = round toward zero, no denormals, the EE `sinf`/`cosf`, the 16385 sine table; `NativeArithmetic` = plain `float`). Tests use `EeArithmetic`; the app uses `NativeArithmetic`.
- Fixtures under `References/fixtures/` (git-ignored); tests that need one are added only when it exists, and configure prints which fixture tests are active.

## Ruling made while planning

- Ports are specified by source (file and function of the JS model) plus exact test data, not by pasted C++: the JS model is the code, and the tests compare to it value for value. An implementer copies arithmetic, not structure.

## File Structure

| Path | Responsibility |
|---|---|
| `src/scene/Arithmetic.hpp` | `EeArithmetic`, `NativeArithmetic`: add/sub/mul/div, sqrt, toInt, sin/cos tables, `sinf`/`cosf` |
| `src/scene/Matrix.hpp` | 4×4 row-vector matrices, apply, multiply, rotations, `viewScreen`, `viewMatrix` |
| `src/scene/ClockState.{hpp,cpp}` | the clock's state block and `step()` (logic + scale) |
| `src/scene/Camera.{hpp,cpp}` | screen and view matrices, approach offset |
| `src/scene/Orbs.{hpp,cpp}` | orbits, rings, sprite fade ramp |
| `src/scene/Rods.{hpp,cpp}` | rod placement, transform, the refracted/textured/reflected faces, split rod, extra passes |
| `src/scene/FrameHead.{hpp,cpp}` | background tube, blur trips, copies, tint, vignette |
| `src/scene/Frame.hpp` | `scene::Frame`, `Pass`, `Mesh`, `Material` |
| `src/scene/Clock.{hpp,cpp}` | `Clock<A>::frame()`: assembles one `scene::Frame` in the OSD's order and steps the state |
| `src/parity/FromScene.{hpp,cpp}` | `scene::Frame` → `parity::GsFrame` |
| `src/render/Device.{hpp,cpp}` | instance, device, queue, VMA, swapchain, frames in flight |
| `src/render/NativeRenderer.{hpp,cpp}`, `shaders/Native.*` | draws a `scene::Frame` |
| `src/app/Main.cpp`, `src/app/DebugPanel.{hpp,cpp}` | window, loop, real time, ImGui |
| `tools/scene/export_fixture.mjs` | per-frame named state and expected values from the JS model on a capture |
| `tests/scene/*.cpp` | exact tests against the exported fixture |

---

### Task 1: Cleanup, CLAUDE.md, carried gate fixes

**Files:** delete the list in the spec's "Cleanup" section; modify `CMakeLists.txt`, `CMakePresets.json`, `CLAUDE.md`, `.github/workflows/build.yml`, `tools/ParityTool/main.cpp`.

- [ ] **Step 1:** Delete, with `git rm` per file (no directory adds): `src/app/*`, `src/gs/*`, `src/core/{VulkanContext,WindowContext,RenderDocWrapper}.*`, `src/renderer/*` except `GsParityRenderer.*`, `src/main.cpp`, `shaders/*` except `GsParity.*`, `resources/**`, `context/**`, `MEMORY.md`, `docs/ANALYSIS-Raylib-vs-Vulkan-vs-OSDSYS.md`, `docs/ghidra_analysis/**`, `.cache/clangd/**`, `imgui.ini`, `run.bat`, `run.sh`. Move `src/renderer/GsParityRenderer.*` to `src/parity/` (update includes). Add `.cache/` and `imgui.ini` to `.gitignore`.
- [ ] **Step 2:** `CMakeLists.txt`: remove the old application target and its source list and the ImGui library block's dependency on the old app (keep ImGui, it is used by Task 10); keep the `ClockRenderer` library, renamed `ClockParity`, and its tests. `CMakePresets.json`: one Windows preset (`Visual Studio 18 2026`, x64), no Mac paths.
- [ ] **Step 3 (carried from the parity review):** `ParityTool` budgets become exact both ways: a listed pixel that is not observed fails with `stale budget entry: <scope> <target> (x,y)`; an observed delta below the listed one fails with `tighten: ...`. In `CMakeLists.txt`, configure prints `parity gates active: <list>` or `parity gates inactive: no fixture at <path>`. Prove both failures by a temporary edit, revert.
- [ ] **Step 4:** Rewrite `CLAUDE.md` from the current truth: overview (native reimplementation; facts/ is the truth; parity is a measuring rule), layers (`scene/`, `render/`, `app/`, `parity/`), source of truth and method (keep the existing section on facts/, verifiers, agents, hooks), build (Windows preset, fixtures), commit convention, code directives, Claude directives. Remove the old layer list, the MoltenVK note and the Vulkan-Guide macOS paths.
- [ ] **Step 5:** CI: Windows job only; build targets `ClockParity` tests and (after Task 10) `CrystalClock`; no ctest (fixtures are not in CI).
- [ ] **Step 6:** Build from a clean `build/`, run `ctest --test-dir build -C Debug` → all parity tests pass. `git grep -n -i -E "raylib|gs/|VulkanContext|RenderOrchestrator|5 passes"` outside `docs/superpowers/` and `References/` → nothing.
- [ ] **Step 7: Commit** (two commits: `Refactor(Project): Delete the code and documents that predate facts` and `Fix(Renderer): Make the parity budgets exact both ways`).

### Task 2: Device, window and an empty frame

**Files:** create `src/render/Device.{hpp,cpp}`, `src/app/Main.cpp`; reuse `src/core/HeadlessContext` ideas but no shared code with the parity device beyond `GpuDevice`.

**Interfaces — Produces:**
```cpp
namespace render {
struct DeviceOptions { bool validation; };
class Device {
public:
    Device(SDL_Window* window, const DeviceOptions&);       // window == nullptr: headless
    VkDevice device() const; VmaAllocator allocator() const; VkQueue queue() const; uint32_t queueFamily() const;
    VkFormat swapchainFormat() const; VkExtent2D swapchainExtent() const;
    struct FrameContext { VkCommandBuffer cmd; VkImage image; VkImageView view; uint32_t index; };
    std::optional<FrameContext> beginFrame();               // nullopt while minimized or out of date (recreates)
    void endFrame(const FrameContext&);                     // submit + present, 2 frames in flight
    uint32_t validationErrors() const;                      // VALIDATION-type messages only
};
}
```
- [ ] **Step 1: Test first** `tests/render/DeviceTest.cpp`: headless `Device` constructs with validation and reports 0 errors; RED (no source).
- [ ] **Step 2:** Implement `Device` (vk-bootstrap, Vulkan 1.4, dynamic rendering + synchronization2, VMA, swapchain FIFO, recreate on resize/out-of-date, minimized = skip). Validation counts only `VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT` errors (this machine has a stale ReShade loader entry).
- [ ] **Step 3:** `src/app/Main.cpp`: SDL3 window 1280×896 "Crystal Clock", loop, each frame clears the swapchain to the OSD's clear colour of the clock screen (read the value from `facts/clock-gs-state.md` or the fixture's `displayClear`) and presents. Executable `CrystalClock`.
- [ ] **Step 4:** Run `CrystalClock` 5 s, resize, minimize, restore, close: no validation error printed, exit code 0. Record in the report.
- [ ] **Step 5: Commit** `Feat(Core): Add the device, the window and the empty frame`.

### Task 3: Scene fixture exporter

**Files:** create `tools/scene/export_fixture.mjs`, `tools/scene/export_fixture.test.mjs`.

**Produces** `References/fixtures/<capture>/scene.json`:
```
{ capture, build: "hdd", frames: [ {
    index,
    input:  { time: {ms, seconds, minutes, hours}, ...every LAYOUT piece the clock screen reads, decoded to named numbers },
    expect: {
      camera: { screen: 4x4, view: 4x4, cameraOffset },
      rods:   [ { number, local: 4x4, matrix: 4x4, faces: [ { refracted: [{x,y,z,u,v,r,g,b,a}], textured: [...], reflected: [...] } ] } ],
      orbs:   [ { position: [x,y,z], ring: { head, full, entries: [[x,y,z,r,g,b]] }, sprites: [...] } ],
      head:   { background: [...strips], blur: [...], copies: [...], tint, vignette },
      after:  { the state block and every piece logic/scale/blurLevel/overlayStep write, decoded }
    } } ] }
```
Decoding: one function per LAYOUT piece the clock screen uses (`time`, `eased`, `orbConstants`, `orbColour(s)`, `mode`, `screen`, `spriteFade`, `state`, `orbRandom`, `template`, `rings`, `scene`, `proportions`, `position`, `direction`, `up`, `rotation`, `clearColour`, `tint`, `cameraOffset`, `zmax`, `cameraFactor`, `greyRamp`, `greys`, `counter`, `tubeConstants`, `level`, `blurRecord`, `copyRecord`, `vignetteRamp`, `vignetteLength`, `overlayLevel`, `fadeRecord`, `ringRecord`, `appearance`, `colours`, `cycleCounters`, `cycleTables`, `logicConstants`, `scaleTarget`, `scaleFactor`, `timeFilled`), with field names from the comments in `clock_memory.mjs` and the facts pages. Floats are written as their 32-bit pattern (`"0x3f800000"`) so the C++ side compares bits.

The `expect` values come from running the JS model (`frame()` and the functions it calls, instrumented by wrapping `transform`, `refracted`, `textured`, `reflected`, `camera`, the orb and head functions) on the capture with `--carry` semantics, the same way `References/scripts/verify_frame.mjs` does.

- [ ] **Step 1: Test first:** on `hddosd-110U-whole3-clock`, `scene.json` has as many frames as `verify_frame.mjs --carry` reports for that capture; every frame has 12 rods with `rods[i].faces.length` equal to the mesh's face count shown above the threshold; floats are 10-character hex strings; re-running gives identical bytes; and `verify_frame.mjs --carry` on the same capture still says FOUND (the instrumentation does not change the model).
- [ ] **Step 2:** Implement. Do not edit `References/model/*` or `References/scripts/*`: wrap their exports.
- [ ] **Step 3:** Generate for `whole3-clock` into the main checkout's `References/fixtures/hddosd-110U-whole3-clock/scene.json`.
- [ ] **Step 4: Commit** `Feat(Project): Export the scene's expected values from the model`.

### Task 4: Arithmetic and matrices

**Files:** create `src/scene/Arithmetic.hpp`, `src/scene/Matrix.hpp`, `tests/scene/ArithmeticTest.cpp`.

Port: `clock_math.mjs` (`f`, `add`, `sub`, `toInt`, `toUnsigned`, `s16`, `apply`, `mul`, `unit`, the sine table and `sin`/`cos` of 16-bit angles, `tickRamp`), `ee_libm.mjs` (`cosf`, `sinf` via its kernels), `clock_camera.mjs` (`viewScreen`, `sineCosine`, `viewMatrix`), and the rotations/normalize/dot at the top of `clock_frame.mjs`. `References/lib/ee-float.mjs` is the reference for the EE cut-toward-zero rules (and its tests `ee-float.test.mjs` give cases).

- [ ] **Step 1: Tests first:** the test vectors of `References/lib/ee-float.test.mjs` (port its fixed cases), the sine table's first, middle and last entries, `sin`/`cos` at 0, 0x4000, 0x8000, −1; `cosf` at the values the camera uses; `viewScreen(512,1,0.47,2048,2048,1,16777215,1,65536)` equal bit for bit to `expect.camera.screen` of frame 0 of `scene.json`. RED.
- [ ] **Step 2:** Implement `EeArithmetic` and `NativeArithmetic` as policy types with the same static functions.
- [ ] **Step 3:** GREEN; commit `Feat(Scene): Port the EE arithmetic and the clock's matrices`.

### Task 5: Clock state and camera

**Files:** `src/scene/ClockState.{hpp,cpp}`, `src/scene/Camera.{hpp,cpp}`, `tests/scene/StateTest.cpp`.

Port `clock_logic.mjs` (`angles`, `chase`, `cycle`, `colours`, `logic`, `scale` with `hdd = true`), `clock_camera.mjs` `camera`, and from `clock_rest.mjs` the state-only steps `blurLevel`, `overlayStep` (clock screen values). `ClockState` holds the named fields of Task 3's `input` that these read and write.

- [ ] **Step 1: Test first:** load `scene.json`; for every frame, `ClockState` built from `input`, `Camera::matrices()` equals `expect.camera` bit for bit, and after `step()` the state equals `expect.after` bit for bit; then the carried run: start from frame 0's input, step N frames feeding only `time` (and the other external inputs `verify_frame.mjs` lists in EXTERNAL) from each frame's input, and compare every frame. RED.
- [ ] **Step 2:** Implement. Field names come from the facts pages (e.g. `currentRod`, `rodAngle`, `secondsAngle`, `progress`, `baseColourStep`).
- [ ] **Step 3:** GREEN (both isolated and carried); commit `Feat(Scene): Port the clock's state and camera`.

### Task 6: Rods

**Files:** `src/scene/Rods.{hpp,cpp}`, `tests/scene/RodsTest.cpp`; the rod mesh is loaded from `facts/data/rod-mesh.json`.

Port from `clock_frame.mjs`: `rodMatrix` and the placement in `scene()`, `readRod`, `transform`, `edgeTerm`, `refracted`, `textured`, `reflected`, the split rod and the two extra passes (`facts/clock-rod-draw.md`, `facts/clock-extra-passes.md`), and the depth-ordered draw list. Output: per rod, per face, the vertices of each send in screen pixels, depth, colour 0..1 and texture coordinates — native units (the GS 12.4 and 0x80 conversions belong to `parity/FromScene`).

- [ ] **Step 1: Test first:** every frame of `scene.json`: rod order, `local`, `matrix`, and every face's vertices equal `expect.rods` after converting expected GS units to native (one documented helper in the test). Exact. RED.
- [ ] **Step 2:** Implement. **Step 3:** GREEN; commit `Feat(Scene): Port the rods`.

### Task 7: Orbs

**Files:** `src/scene/Orbs.{hpp,cpp}`, `tests/scene/OrbsTest.cpp`.

Port the orb part of `scene()` and the functions `facts/clock-orbs.md` names: orbit positions, ring feeding (one point every three frames), the trail strip colours and fade, the sprite sizes, the sprite fade ramp (`spriteFade`, `tickRamp`).

- [ ] **Step 1: Test first:** every frame: positions, ring head/full/entries, trail points and colours, sprites equal `expect.orbs`; carried run over all frames. RED. **Step 2:** Implement. **Step 3:** GREEN; commit `Feat(Scene): Port the orbs`.

### Task 8: Frame head and overlay

**Files:** `src/scene/FrameHead.{hpp,cpp}`, `tests/scene/HeadTest.cpp`.

Port `clock_rest.mjs` `strip`, `background`, `blur`, `copy`, `head`, `vignette`, `overlay`, `rectangle` for the clock screen (no bars on item 0 = 0? follow `bars` as the model does; no text, no column if the model's column is a GS artefact — port it anyway, it is two pixels).

- [ ] **Step 1: Test first:** every frame equals `expect.head`. RED. **Step 2:** Implement. **Step 3:** GREEN; commit `Feat(Scene): Port the frame head and overlay`.

### Task 9: scene::Frame, the clock, and the gate through the rule

**Files:** `src/scene/Frame.hpp`, `src/scene/Clock.{hpp,cpp}`, `src/parity/FromScene.{hpp,cpp}`, `tests/scene/ClockParityTest.cpp`.

**Interfaces — Produces:**
```cpp
namespace scene {
enum class TargetName { Display, RefractionSource, Work };
enum class BlendOp { Opaque, AlphaOver, Add, Subtract, Modulate };
enum class Sampling { Repeat, Clamp, ClampToRegion };
struct Vertex { float x, y, z, u, v, q; float r, g, b, a; };          // pixels at 640x448-frame scale, colour 0..1
struct Material { std::string texture; bool textureIsTarget; Sampling sampling; std::array<float,4> region;
                  BlendOp blend; float blendConstant; bool depthTest; bool depthGreaterEqual; bool depthWrite; bool bilinear; };
enum class Topology { Triangles, TriangleStrip, TriangleFan, Lines, Quads };
struct Pass { std::string name; TargetName target; Topology topology; Material material; std::vector<Vertex> vertices; bool edgeSmoothing; };
struct Frame { std::array<float,4> clearColour; std::vector<Pass> passes; };
template <class A> class Clock {
public:
    explicit Clock(const ClockInputs&);          // from scene.json input, or from defaults + real time
    Frame frame(const ClockTime&);               // the OSD's order: head, rods, orbs, extra passes, overlay; then step
};
}
namespace parity { GsFrame fromScene(const scene::Frame&, const GsFrameLayout&); }
```
`BlendOp` names map the GS ALPHA values the clock uses (`facts/clock-gs-state.md`); `edgeSmoothing` marks the passes the OSD draws with AA1.

- [ ] **Step 1: Test first:** run `Clock<EeArithmetic>` from frame 0 of `scene.json` over all frames (carried), convert each `Frame` with `fromScene`, and (a) compare to the fixture's decoded `GsFrame` of the capture pass by pass: same pass count and order, same state, every vertex equal; (b) render frame 0 with `GsParityRenderer` and compare with the oracle using the clock's existing budget list — must pass the existing gate. RED.
- [ ] **Step 2:** Implement `Clock` assembly in the order of `facts/clock-frame.md` "Order of a frame" (clock-screen parts only) and `fromScene`.
- [ ] **Step 3:** GREEN; commit `Feat(Scene): Assemble the clock frame and gate it through the rule`.

### Task 10: Native renderer and the live window

**Files:** `src/render/NativeRenderer.{hpp,cpp}`, `shaders/Native.{vert,frag}`, `src/app/DebugPanel.{hpp,cpp}`, modify `src/app/Main.cpp`, `tools/ParityTool/` (a `--native` distance report).

- [ ] **Step 1: Test first** `tests/render/NativeRendererTest.cpp`: headless, 640×448: an `AlphaOver` quad over a known colour gives `src·α + dst·(1−α)` with α = a (within 1/255); `Add`, `Subtract` likewise; a `Sampling::ClampToRegion` lookup outside the region returns the region's edge texel; MSAA 4× on a diagonal edge gives intermediate values; 0 validation errors. RED.
- [ ] **Step 2:** Implement: targets as colour attachments sized from the output resolution (frame scale = output height / 448), the refraction source sampled after a barrier, one pipeline per (blend, depth, topology) built lazily, textures from `References/textures/` (the ten clock textures; run `References/scripts/extract_rom_textures.mjs` if absent — document the command), rod mesh from the scene, MSAA resolve. No GS rules: float blending with α = alpha (scene alpha is already 0..1).
- [ ] **Step 3:** Distance report: `ParityTool --native <fixture>` renders `Clock<NativeArithmetic>` frame 0 at 640×224 per target and prints differing share and largest difference against the oracle images — report only, never fails. Record its output in the report.
- [ ] **Step 4:** `Main.cpp`: `Clock<NativeArithmetic>` from defaults + local time (`std::chrono::zoned_time`), fixed 59.94 Hz logic step, `NativeRenderer` to the swapchain; ImGui panel: resolution (native 640×448, window, ×2, ×4), MSAA (off/2/4/8), pause, step, set time, show target. Validation clean over 60 s of running, resizing and toggling.
- [ ] **Step 5:** Screenshot the window at 640×448 and at window size (write PNGs via a panel button) for Jean's eye check; paths in the report.
- [ ] **Step 6: Commit** (`Feat(Renderer): Draw the clock frame natively`, `Feat(App): Run the clock live in a window`).

## Review Focus

- A scene port that matches on isolated frames but drifts on the carried run (state not carried exactly).
- `EeArithmetic` leaking into the app build (the app must use `NativeArithmetic`).
- `parity/FromScene` hiding a scene difference by adapting to the fixture (it must be a pure unit conversion).
- The native renderer silently clamping alphas above 1 (the clock should not have any; assert it in debug).
- Swapchain recreation on resize/minimize without leaks or validation errors.
