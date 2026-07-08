# Wallpaper-Engine "PS2 Clock" Port — Implementation Plan (Series)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the Wallpaper Engine scene `1979606285` ("Playstation 2 Clock") into the existing Vulkan core, replacing the retired GS/EE reverse-engineering track, with a procedurally-generated prism and a pluggable clock-rotation rule.

**Architecture:** Keep the generic `core/` + `renderer/` Vulkan wrapper. Add Vulkan-free logic modules (`scene/`, `mesh/`) that are unit-tested, plus an `assets/` loader and a rewritten `app/RenderOrchestrator` that records an offscreen-HDR render graph. Source of truth is the unpacked scene at `C:\Users\dell04\Downloads\1979606285\scene-repkg\`; the approved design is `docs/superpowers/specs/2026-07-08-wallpaper-port-scene-design.md`.

**Tech Stack:** C++23, Vulkan 1.3 (dynamic rendering), VMA, vk-bootstrap, SDL3 (window + audio), GLM, ImGui, glslc, stb_image/stb_truetype.

## Global Constraints

- C++23; CMake ≥ 3.30; Windows / RDNA2; Vulkan SDK required (`find_package(Vulkan)` + `glslc`).
- `GLM_FORCE_DEPTH_ZERO_TO_ONE` is set project-wide; keep it for every new target that links glm.
- Logic modules (`scene/`, `mesh/`) contain **no Vulkan and no SDL symbols** — same rule the retired `gs/`/`clock/` libs followed. They link only glm.
- Tests are plain C++ `int main()` executables returning non-zero on failure, using a local `check(bool, msg)` / `near(a,b,eps)` helper (see `tests/RotationBasisTest.cpp` for the canonical style). Register each with `add_test` under CTest.
- Shaders: glslc `--target-env=vulkan1.3`, globbed from `shaders/` to `bin/shaders/*.spv`.
- Commit convention: `Type(Scope): imperative ≤72 chars`, scopes `Core/Renderer/App/GS/Gsvk/Shaders/CI/Project` plus new `Scene/Mesh/Assets/Audio`. **Never** add a `Co-Authored-By` trailer.
- English only in code/docs; comments only for non-obvious math.
- New logic namespace is `wp` (wallpaper port), keeping it distinct from the retired `ps2clock` namespace.

## Plan Series (execution order)

1. **Plan 1 — Foundation + hero shot (this document):** archive & remove the RE track, preserve the reusable core, add pure `SceneClock`/`SceneCamera`/`PrismMesh`, stand up the offscreen-HDR graph, port the cloud-tunnel shader, and draw a single glowing prism. Deliverable: one procedurally-modeled crystal glowing over the animated cloud tunnel.
2. Plan 2 — Full 12-prism field + framebuffer-copy refraction + tint/glow-by-minute.
3. Plan 3 — Orbs + trails + glow (mode switch).
4. Plan 4 — Background particles + post FX (film grain, gaussian blur, downsample).
5. Plan 5 — Date/time text overlay (font atlas).
6. Plan 6 — Layered white-noise audio + startup one-shot.

Each later plan is written when its predecessor lands and is reviewed.

## Source-Scene Reference Values (Plan 1 subset)

- Clock parent (WE default rule): `yaw = -(sec + ms/1000)*6`, `roll = -(hour%12)*30`, Euler built `ZYX` then read back `XYZ`. One turn per minute.
- Per-prism local spin: `(0, 20, 0)` deg/s.
- Ring: `numPrisms=12`, `prismDist=6.5`, `prismScale=0.9`, prism `i` at Euler `(0,0,i*30°)` then `+6.5` in Y (Plan 1 draws only prism 0).
- `Light` glow value: `(minutes + seconds/60)/60 * 10`.
- Camera: eye starts `(0,0,250)`, target `(0,0,38)` at 53° FOV; accel-limited velocity, accel `100`, converged within `0.1`; up `(0,1,0)`; near `1`, far `10000`.
- Colors: `clearcolor=(0.125,0.098,0.204)`, prism `start=(0.478,0.333,0.780)`, `end=(0.298,0.780,0.757)`, tunnel bg `(0.68,0.54,1.0)`.
- Tunnel (`ps2menu.frag`): 4 noise iters `(0.25,4,0.035)`,`(0.5,2,0.04)`,`(1.5,1,0.0435)`,`(5.5,1,0.0465)`, `TIMESCALE 0.4`, `ZOOM 0.65`, brightness `0.7..1.0`, `col = bg*noise*min(1,.1+.9r)*0.6`.

---

## File Structure (Plan 1)

- Create `src/scene/SceneParams.hpp` — POD defaults (colors, fov, dist, counts). No VK.
- Create `src/scene/RotationRule.hpp` — `IRotationRule` interface + `WeDefaultRule`.
- Create `src/scene/SceneClock.hpp` / `.cpp` — time → parent angles + `Light`. No VK.
- Create `src/scene/SceneCamera.hpp` / `.cpp` — accel-limited eye easing, view/proj. No VK.
- Create `src/mesh/PrismMesh.hpp` / `.cpp` — procedural crystal geometry. No VK.
- Create `shaders/tunnel.vert` / `tunnel.frag` — cloud-tunnel port of `ps2menu`.
- Create `shaders/crystal.vert` / `crystal.frag` — port of the WE crystal shader (single-light path, no NORMALMAP).
- Rewrite `src/app/RenderOrchestrator.cpp` / `.hpp` — offscreen HDR target, tunnel pass, single-prism pass, blit to swapchain.
- Modify `src/main.cpp` — drop GS scene wiring; drive the new orchestrator.
- Modify `CMakeLists.txt` — remove RE libs/targets/tests; add `scene`, `mesh` libs and their tests; add stb include.
- Create `tests/SceneClockTest.cpp`, `tests/SceneCameraTest.cpp`, `tests/PrismMeshTest.cpp`.
- Delete `src/gs/`, `src/ee/`, `src/gsvk/`, `src/clock/`, `src/app/GsScene.*`, `src/app/GsRenderer.*`, `tools/` RE tools, retired `tests/*`, retired `shaders/gsclock.*` + `rod_flat.*`.

---

## Task 1: Archive the RE track and reduce the build to a blank window

**Files:**
- Modify: `CMakeLists.txt` (remove RE libs/targets/tests and their sources from `SOURCES`/`target_link_libraries`)
- Delete: `src/gs/`, `src/ee/`, `src/gsvk/`, `src/clock/`, `src/app/GsScene.*`, `src/app/GsRenderer.*`, `tools/gsdump`, `tools/vramdump`, `tools/eerun`, `tools/clockdump`, `shaders/gsclock.*`, `shaders/rod_flat.*`, retired `tests/*` (all Gs*/Ee*/Swizzle*/Texa*/Parser*/Clock*/Fixed124/Rotation/Projection/RodField tests)
- Modify: `src/main.cpp` (strip GS scene includes/usage; keep window + VulkanContext + swapchain clear)
- Modify: `src/app/RenderOrchestrator.*` (temporary: clear the swapchain to `clearcolor` only)

**Interfaces:**
- Produces: a buildable app that opens an SDL3 window and clears the swapchain to `(0.125,0.098,0.204)`. No scene logic yet.

- [ ] **Step 1: Tag the current RE state so nothing is lost**

```bash
git tag archive/gs-ee-re
git push --tags   # only if a remote exists; skip otherwise
```

- [ ] **Step 2: Delete the retired source trees and tools**

```bash
git rm -r src/gs src/ee src/gsvk src/clock \
          src/app/GsScene.cpp src/app/GsScene.hpp \
          src/app/GsRenderer.cpp src/app/GsRenderer.hpp \
          tools/gsdump tools/vramdump tools/eerun tools/clockdump \
          shaders/gsclock.vert shaders/gsclock.frag \
          shaders/rod_flat.vert shaders/rod_flat.frag
git rm tests/GsBlendTest.cpp tests/GsRecipeTest.cpp tests/SwizzleRoundTripTest.cpp \
       tests/SwizzleAddress32Test.cpp tests/TexaExpandTest.cpp tests/DeswizzleTexa24Test.cpp \
       tests/SwizzleAddress4Test.cpp tests/ParserStqQTest.cpp tests/EeMemoryTest.cpp \
       tests/EeInterpreterTest.cpp tests/EeRealCodeTest.cpp tests/Fixed124Test.cpp \
       tests/RotationBasisTest.cpp tests/ProjectionFitTest.cpp tests/RodFieldTest.cpp \
       tests/ClockStateTest.cpp tests/ClockOrbTest.cpp
```

- [ ] **Step 3: Edit `CMakeLists.txt` — remove RE libs/targets and reduce `SOURCES`**

In `CMakeLists.txt`, set the app sources to only the reusable core plus the app shell:

```cmake
set(SOURCES
    src/main.cpp
    src/core/WindowContext.cpp
    src/core/VulkanContext.cpp
    src/core/RenderDocWrapper.cpp
    src/renderer/SwapchainManager.cpp
    src/renderer/ShaderLoader.cpp
    src/renderer/PipelineBuilder.cpp
    src/renderer/ResourceManager.cpp
    src/renderer/PassRecorder.cpp
    src/renderer/DescriptorAllocator.cpp
    src/renderer/UIRenderer.cpp
    src/app/RenderOrchestrator.cpp
    src/app/TimeSync.cpp
)
```

Change `target_link_libraries(${PROJECT_NAME} PRIVATE ...)` to drop `gsdump_lib gsvk gs clock`:

```cmake
target_link_libraries(${PROJECT_NAME} PRIVATE
    SDL3::SDL3
    vk-bootstrap
    VulkanMemoryAllocator
    glm::glm
    ImGui
)
```

Delete every `add_library`/`add_executable`/`add_test` block for `gsdump_lib`, `gsdump`, `gs`, `ee`, `vramdump`, `eerun`, `gsvk`, the `gsvk_*`/`gs_*`/`ee_*` test foreach/blocks, `clock`, `clock_dump`, and the `clock_*` test foreach. Keep `enable_testing()`.

- [ ] **Step 4: Strip GS wiring from `src/main.cpp` and `RenderOrchestrator`**

Remove `#include` lines referencing `app/GsScene.hpp`, `app/GsRenderer.hpp`, `gs/…`, `gsvk/…`, `clock/…`, and any `--clock`, `--spin`, `--clock3d` handling. In `RenderOrchestrator`, reduce the per-frame record to: begin dynamic rendering on the swapchain image with a clear value of `{0.125f, 0.098f, 0.204f, 1.0f}` and end. (Keep the existing swapchain-acquire/submit/present loop.)

- [ ] **Step 5: Configure and build**

Run: `cmake -B build && cmake --build build`
Expected: configures with no missing-source errors; `CrystalClockVK.exe` links.

- [ ] **Step 6: Run and verify a blank window**

Run: `./bin/CrystalClockVK.exe`
Expected: a window opens filled with the dark purple clear color `(0.125,0.098,0.204)`; closes cleanly.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "Refactor(Project): archive GS/EE track, reduce app to blank window"
```

---

## Task 2: `SceneClock` — pluggable rotation rule + glow value (pure logic)

**Files:**
- Create: `src/scene/SceneParams.hpp`
- Create: `src/scene/RotationRule.hpp`
- Create: `src/scene/SceneClock.hpp`, `src/scene/SceneClock.cpp`
- Test: `tests/SceneClockTest.cpp`
- Modify: `CMakeLists.txt` (add `scene` lib + `scene_clock` test)

**Interfaces:**
- Produces:
  - `struct wp::WallClock { int hour; int minute; int second; int millis; };`
  - `struct wp::ClockState { glm::vec3 parentAnglesDeg; float light; };`
  - `struct wp::IRotationRule { virtual glm::vec3 parentAnglesDeg(const WallClock&) const = 0; virtual ~IRotationRule() = default; };`
  - `class wp::WeDefaultRule : public wp::IRotationRule`
  - `ClockState wp::computeClock(const WallClock&, const IRotationRule&);`
  - `float wp::glowLight(const WallClock&);`  // (minute + second/60)/60 * 10
- Consumes: glm only.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/SceneClockTest.cpp
#include <cstdio>
#include <cmath>
#include <string>
#include "scene/SceneClock.hpp"

static int g_fails = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("  FAIL: %s\n", what.c_str()); g_fails++; }
}
static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

int main() {
    wp::WeDefaultRule rule;

    // At 00:00:00.000 all parent angles are zero.
    wp::ClockState s0 = wp::computeClock(wp::WallClock{0, 0, 0, 0}, rule);
    check(near(s0.parentAnglesDeg.x, 0) && near(s0.parentAnglesDeg.y, 0) && near(s0.parentAnglesDeg.z, 0),
          "midnight angles zero");

    // Light fills over the hour: 30 min → (30/60)*10 = 5.0
    check(near(wp::glowLight(wp::WallClock{3, 30, 0, 0}), 5.0f), "light at :30 == 5");
    check(near(wp::glowLight(wp::WallClock{3, 0, 0, 0}), 0.0f), "light at :00 == 0");

    // One full yaw turn per minute: 30s → yaw magnitude 180 deg (sign per rule).
    wp::ClockState s30 = wp::computeClock(wp::WallClock{5, 10, 30, 0}, rule);
    // Re-expressed XYZ euler; yaw is the dominant Y rotation. Accept |yaw| ~ 180.
    check(near(std::fabs(s30.parentAnglesDeg.y), 180.0f, 0.5f) ||
          near(std::fabs(s30.parentAnglesDeg.y), 180.0f - 360.0f, 0.5f),
          "30s -> half turn yaw");

    // Roll tracks hour%12 * 30 deg (magnitude). At 3 o'clock -> 90 deg.
    wp::ClockState s3 = wp::computeClock(wp::WallClock{3, 0, 0, 0}, rule);
    check(near(std::fabs(s3.parentAnglesDeg.x) + std::fabs(s3.parentAnglesDeg.z), 90.0f, 0.5f),
          "3 o'clock -> 90 deg roll component");

    if (g_fails) { std::printf("scene clock: %d FAILURES\n", g_fails); return 1; }
    std::printf("scene clock: OK\n");
    return 0;
}
```

- [ ] **Step 2: Add the `scene` lib + test to CMake and run to verify it fails to build**

Add to `CMakeLists.txt` (after the reusable-core section):

```cmake
add_library(scene STATIC
    src/scene/SceneClock.cpp
    src/scene/SceneCamera.cpp
)
target_include_directories(scene PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(scene PUBLIC glm::glm)
target_compile_definitions(scene PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)

add_executable(scene_clock_tests tests/SceneClockTest.cpp)
target_link_libraries(scene_clock_tests PRIVATE scene)
set_target_properties(scene_clock_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
add_test(NAME scene_clock COMMAND scene_clock_tests)
```

Run: `cmake -B build && cmake --build build --target scene_clock_tests`
Expected: FAIL — `scene/SceneClock.hpp` not found / `SceneCamera.cpp` missing.

- [ ] **Step 3: Write `SceneParams.hpp` and `RotationRule.hpp`**

```cpp
// src/scene/SceneParams.hpp
#pragma once
#include <glm/glm.hpp>

namespace wp {
struct SceneParams {
    int   numPrisms   = 12;
    float prismDist   = 6.5f;
    float prismScale  = 0.9f;
    glm::vec3 localSpinDegPerSec = {0.0f, 20.0f, 0.0f};

    float fovDeg = 53.0f;
    float nearZ  = 1.0f;
    float farZ   = 10000.0f;

    glm::vec3 clearColor = {0.125f, 0.098f, 0.204f};
    glm::vec3 startColor = {0.478f, 0.333f, 0.780f};
    glm::vec3 endColor   = {0.298f, 0.780f, 0.757f};
    glm::vec3 tunnelBg   = {0.68f, 0.54f, 1.0f};
    float tintPeriodSec  = 20.0f;
};
}  // namespace wp
```

```cpp
// src/scene/RotationRule.hpp
#pragma once
#include <glm/glm.hpp>

namespace wp {
struct WallClock { int hour; int minute; int second; int millis; };

struct IRotationRule {
    virtual glm::vec3 parentAnglesDeg(const WallClock&) const = 0;
    virtual ~IRotationRule() = default;
};

// Wallpaper-Engine default: yaw=-(sec+ms/1000)*6, roll=-(hour%12)*30,
// composed as an intrinsic ZYX euler then read back as XYZ euler.
class WeDefaultRule : public IRotationRule {
public:
    glm::vec3 parentAnglesDeg(const WallClock&) const override;
};
}  // namespace wp
```

- [ ] **Step 4: Write `SceneClock.hpp`**

```cpp
// src/scene/SceneClock.hpp
#pragma once
#include <glm/glm.hpp>
#include "scene/RotationRule.hpp"

namespace wp {
struct ClockState {
    glm::vec3 parentAnglesDeg;  // XYZ euler, degrees
    float     light;            // glow fill 0..10
};

float      glowLight(const WallClock& t);
ClockState computeClock(const WallClock& t, const IRotationRule& rule);
}  // namespace wp
```

- [ ] **Step 5: Write `SceneClock.cpp` (and the rule impl)**

```cpp
// src/scene/SceneClock.cpp
#include "scene/SceneClock.hpp"
#define GLM_ENABLE_EXPERIMENTAL   // required for glm/gtx/euler_angles.hpp
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace wp {

glm::vec3 WeDefaultRule::parentAnglesDeg(const WallClock& t) const {
    float sec = static_cast<float>(t.second) + static_cast<float>(t.millis) * 0.001f;
    float yaw  = -(sec) * 6.0f;                       // deg, one turn per minute
    float roll = -static_cast<float>(t.hour % 12) * 30.0f;

    // Build intrinsic ZYX (yaw about Y, roll about Z), then read back XYZ euler
    // to match the WE script's Mat4.fromEuler(...,"ZYX").toEuler("XYZ").
    const float d2r = glm::radians(1.0f);
    glm::mat4 m = glm::eulerAngleZ(roll * d2r) * glm::eulerAngleY(yaw * d2r);
    glm::vec3 e;  // extractEulerAngleXYZ gives radians
    glm::extractEulerAngleXYZ(m, e.x, e.y, e.z);
    return glm::degrees(e);
}

float glowLight(const WallClock& t) {
    return (static_cast<float>(t.minute) + static_cast<float>(t.second) / 60.0f) / 60.0f * 10.0f;
}

ClockState computeClock(const WallClock& t, const IRotationRule& rule) {
    return ClockState{ rule.parentAnglesDeg(t), glowLight(t) };
}

}  // namespace wp
```

- [ ] **Step 6: Build and run the test**

Run: `cmake --build build --target scene_clock_tests && ./bin/scene_clock_tests.exe`
Expected: `scene clock: OK`

- [ ] **Step 7: Commit**

```bash
git add src/scene/SceneParams.hpp src/scene/RotationRule.hpp \
        src/scene/SceneClock.hpp src/scene/SceneClock.cpp \
        tests/SceneClockTest.cpp CMakeLists.txt
git commit -m "Feat(Scene): pluggable clock rotation rule + glow-by-minute"
```

---

## Task 3: `SceneCamera` — accel-limited eye easing + view/projection (pure logic)

**Files:**
- Create: `src/scene/SceneCamera.hpp`, `src/scene/SceneCamera.cpp`
- Test: `tests/SceneCameraTest.cpp`
- Modify: `CMakeLists.txt` (add `scene_camera` test; `SceneCamera.cpp` already listed in `scene` lib in Task 2)

**Interfaces:**
- Produces:
  - `class wp::SceneCamera` with:
    - `explicit SceneCamera(const SceneParams&);`
    - `void update(float dt);`  // advances eye toward target (accel 100, stop within 0.1)
    - `bool converged() const;`
    - `glm::vec3 eye() const;`
    - `glm::mat4 view() const;`  // lookAt(eye, (0,0,0), (0,1,0))
    - `glm::mat4 projection(float aspect) const;`  // perspective(fovY, aspect, near, far), zero-to-one depth
- Consumes: `wp::SceneParams` (Task 2).

- [ ] **Step 1: Write the failing test**

```cpp
// tests/SceneCameraTest.cpp
#include <cstdio>
#include <cmath>
#include <string>
#include "scene/SceneCamera.hpp"
#include "scene/SceneParams.hpp"

static int g_fails = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("  FAIL: %s\n", what.c_str()); g_fails++; }
}
static bool near(float a, float b, float eps) { return std::fabs(a - b) < eps; }

int main() {
    wp::SceneParams p;
    wp::SceneCamera cam(p);

    // Starts far out on +Z.
    check(near(cam.eye().z, 250.0f, 1.0f), "starts at z=250");
    check(!cam.converged(), "not converged at start");

    // Integrate several seconds; should ease toward z=38 target (53 fov).
    for (int i = 0; i < 600; ++i) cam.update(1.0f / 60.0f);  // 10 seconds
    check(cam.converged(), "converges within 10s");
    check(near(cam.eye().z, 38.0f, 0.5f), "settles near z=38");

    // View maps world origin in front of the camera (negative view-space Z).
    glm::vec4 originVS = cam.view() * glm::vec4(0, 0, 0, 1);
    check(originVS.z < 0.0f, "origin is in front of camera");

    if (g_fails) { std::printf("scene camera: %d FAILURES\n", g_fails); return 1; }
    std::printf("scene camera: OK\n");
    return 0;
}
```

- [ ] **Step 2: Register the test and run to verify it fails**

Add to `CMakeLists.txt`:

```cmake
add_executable(scene_camera_tests tests/SceneCameraTest.cpp)
target_link_libraries(scene_camera_tests PRIVATE scene)
set_target_properties(scene_camera_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
add_test(NAME scene_camera COMMAND scene_camera_tests)
```

Run: `cmake -B build && cmake --build build --target scene_camera_tests`
Expected: FAIL — `scene/SceneCamera.hpp` not found.

- [ ] **Step 3: Write `SceneCamera.hpp`**

```cpp
// src/scene/SceneCamera.hpp
#pragma once
#include <glm/glm.hpp>
#include "scene/SceneParams.hpp"

namespace wp {
class SceneCamera {
public:
    explicit SceneCamera(const SceneParams& params);
    void update(float dt);
    bool converged() const;
    glm::vec3 eye() const { return position_; }
    glm::mat4 view() const;
    glm::mat4 projection(float aspect) const;

private:
    SceneParams params_;
    glm::vec3 position_{0.0f, 0.0f, 250.0f};
    glm::vec3 target_{0.0f, 0.0f, 38.0f};   // FOV 53 target
    glm::vec3 velocity_{0.0f};
    float accel_ = 100.0f;
};
}  // namespace wp
```

- [ ] **Step 4: Write `SceneCamera.cpp`**

```cpp
// src/scene/SceneCamera.cpp
#include "scene/SceneCamera.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace wp {

SceneCamera::SceneCamera(const SceneParams& params) : params_(params) {}

void SceneCamera::update(float dt) {
    dt = std::min(dt, 0.05f);
    glm::vec3 distance = target_ - position_;
    if (glm::length(distance) > 0.1f) {
        glm::vec3 velocityDelta = distance - velocity_;   // targetVelocity = distance
        float step = std::min(glm::length(velocityDelta), accel_ * dt);
        if (glm::length(velocityDelta) > 1e-6f)
            velocity_ += glm::normalize(velocityDelta) * step;
        position_ += velocity_ * dt;
    }
}

bool SceneCamera::converged() const {
    return glm::length(target_ - position_) <= 0.1f;
}

glm::mat4 SceneCamera::view() const {
    return glm::lookAt(position_, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 SceneCamera::projection(float aspect) const {
    glm::mat4 p = glm::perspective(glm::radians(params_.fovDeg), aspect, params_.nearZ, params_.farZ);
    p[1][1] *= -1.0f;  // Vulkan clip-space Y flip
    return p;
}

}  // namespace wp
```

- [ ] **Step 5: Build and run the test**

Run: `cmake --build build --target scene_camera_tests && ./bin/scene_camera_tests.exe`
Expected: `scene camera: OK`

- [ ] **Step 6: Commit**

```bash
git add src/scene/SceneCamera.hpp src/scene/SceneCamera.cpp tests/SceneCameraTest.cpp CMakeLists.txt
git commit -m "Feat(Scene): accel-limited camera easing with VK view/projection"
```

---

## Task 4: `PrismMesh` — procedural crystal geometry (pure logic)

**Files:**
- Create: `src/mesh/PrismMesh.hpp`, `src/mesh/PrismMesh.cpp`
- Test: `tests/PrismMeshTest.cpp`
- Modify: `CMakeLists.txt` (add `mesh` lib + `prism_mesh` test)

**Interfaces:**
- Produces:
  - `struct wp::Vertex { glm::vec3 position; glm::vec3 normal; glm::vec2 uv; };`
  - `struct wp::MeshData { std::vector<Vertex> vertices; std::vector<uint32_t> indices; };`
  - `MeshData wp::buildPrism(int sides = 6, float radius = 1.0f, float halfHeight = 2.0f);`
    - A crystal along +Y: an `sides`-gon belt at `y=0` with an apex at `+halfHeight` and a base tip at `-halfHeight`. `position.y` spans `[-halfHeight, +halfHeight]` so the shader's `v_Height = position.y` drives the glow fill.
- Consumes: glm + `<vector>` + `<cstdint>`.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/PrismMeshTest.cpp
#include <cstdio>
#include <cmath>
#include <string>
#include "mesh/PrismMesh.hpp"

static int g_fails = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("  FAIL: %s\n", what.c_str()); g_fails++; }
}

int main() {
    wp::MeshData m = wp::buildPrism(6, 1.0f, 2.0f);

    // A 6-gon bipyramid: 6 belt verts + 2 apexes = 8 unique positions minimum.
    check(m.vertices.size() >= 8, "has at least 8 vertices");
    // 6 sides * 2 triangles (top + bottom) = 12 tris = 36 indices.
    check(m.indices.size() == 36, "has 36 indices (12 triangles)");
    check(m.indices.size() % 3 == 0, "indices are whole triangles");

    // Height spans exactly [-2, +2].
    float minY = 1e9f, maxY = -1e9f;
    for (const auto& v : m.vertices) { minY = std::fmin(minY, v.position.y); maxY = std::fmax(maxY, v.position.y); }
    check(std::fabs(minY + 2.0f) < 1e-4f, "min y == -2");
    check(std::fabs(maxY - 2.0f) < 1e-4f, "max y == +2");

    // All normals are unit length.
    bool allUnit = true;
    for (const auto& v : m.vertices) if (std::fabs(glm::length(v.normal) - 1.0f) > 1e-3f) allUnit = false;
    check(allUnit, "all normals unit length");

    // All indices in range.
    bool inRange = true;
    for (uint32_t i : m.indices) if (i >= m.vertices.size()) inRange = false;
    check(inRange, "all indices in range");

    if (g_fails) { std::printf("prism mesh: %d FAILURES\n", g_fails); return 1; }
    std::printf("prism mesh: OK\n");
    return 0;
}
```

- [ ] **Step 2: Add the `mesh` lib + test to CMake and run to verify it fails**

```cmake
add_library(mesh STATIC src/mesh/PrismMesh.cpp)
target_include_directories(mesh PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(mesh PUBLIC glm::glm)

add_executable(prism_mesh_tests tests/PrismMeshTest.cpp)
target_link_libraries(prism_mesh_tests PRIVATE mesh)
set_target_properties(prism_mesh_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
add_test(NAME prism_mesh COMMAND prism_mesh_tests)
```

Run: `cmake -B build && cmake --build build --target prism_mesh_tests`
Expected: FAIL — `mesh/PrismMesh.hpp` not found.

- [ ] **Step 3: Write `PrismMesh.hpp`**

```cpp
// src/mesh/PrismMesh.hpp
#pragma once
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

namespace wp {
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

// Faceted crystal along +Y: an `sides`-gon belt at y=0, apex at +halfHeight,
// base tip at -halfHeight. Flat-shaded (per-face normals; belt verts duplicated
// per triangle so each face gets a correct normal).
MeshData buildPrism(int sides = 6, float radius = 1.0f, float halfHeight = 2.0f);
}  // namespace wp
```

- [ ] **Step 4: Write `PrismMesh.cpp`**

```cpp
// src/mesh/PrismMesh.cpp
#include "mesh/PrismMesh.hpp"
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace wp {

MeshData buildPrism(int sides, float radius, float halfHeight) {
    MeshData m;
    const glm::vec3 apex(0.0f, halfHeight, 0.0f);
    const glm::vec3 base(0.0f, -halfHeight, 0.0f);

    auto beltVert = [&](int i) {
        float a = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(sides);
        return glm::vec3(radius * std::cos(a), 0.0f, radius * std::sin(a));
    };

    auto pushTri = [&](const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2) {
        glm::vec3 n = glm::normalize(glm::cross(p1 - p0, p2 - p0));
        uint32_t base_i = static_cast<uint32_t>(m.vertices.size());
        // UV: map y to v (0 at base tip, 1 at apex); u by facet angle fraction.
        auto uvOf = [&](const glm::vec3& p) {
            float v = (p.y + halfHeight) / (2.0f * halfHeight);
            float u = 0.5f + std::atan2(p.z, p.x) / glm::two_pi<float>();
            return glm::vec2(u, v);
        };
        m.vertices.push_back({p0, n, uvOf(p0)});
        m.vertices.push_back({p1, n, uvOf(p1)});
        m.vertices.push_back({p2, n, uvOf(p2)});
        m.indices.push_back(base_i + 0);
        m.indices.push_back(base_i + 1);
        m.indices.push_back(base_i + 2);
    };

    for (int i = 0; i < sides; ++i) {
        glm::vec3 b0 = beltVert(i);
        glm::vec3 b1 = beltVert((i + 1) % sides);
        pushTri(b0, b1, apex);   // upper facet (CCW seen from outside/top)
        pushTri(b1, b0, base);   // lower facet
    }
    return m;
}

}  // namespace wp
```

- [ ] **Step 5: Build and run the test**

Run: `cmake --build build --target prism_mesh_tests && ./bin/prism_mesh_tests.exe`
Expected: `prism mesh: OK`

- [ ] **Step 6: Commit**

```bash
git add src/mesh/PrismMesh.hpp src/mesh/PrismMesh.cpp tests/PrismMeshTest.cpp CMakeLists.txt
git commit -m "Feat(Mesh): procedural faceted crystal along +Y"
```

---

## Task 5: Offscreen HDR target + blit to swapchain (renderer integration)

**Files:**
- Modify: `src/app/RenderOrchestrator.hpp`, `src/app/RenderOrchestrator.cpp`
- Create: `shaders/blit.vert`, `shaders/blit.frag` (if not already present as reusable blit — the repo already has `shaders/blit.*`; reuse them)
- Modify: `src/main.cpp` (construct `SceneParams`, pass to orchestrator)

**Interfaces:**
- Consumes: `wp::SceneParams`; `ResourceManager` (image creation), `PassRecorder`, `PipelineBuilder`, `SwapchainManager` from the existing renderer.
- Produces: `RenderOrchestrator` renders into an offscreen `R16G16B16A16_SFLOAT` color image (+ `D32_SFLOAT` depth), then samples it in a fullscreen blit pass onto the swapchain image. This is the reusable HDR→LDR spine every later pass draws into.

- [ ] **Step 1: Add the offscreen target fields to `RenderOrchestrator.hpp`**

```cpp
// in class RenderOrchestrator (private):
    // Offscreen HDR scene target (all scene passes draw here; blit resolves to swapchain).
    AllocatedImage hdrColor_{};      // VK_FORMAT_R16G16B16A16_SFLOAT
    AllocatedImage sceneDepth_{};    // VK_FORMAT_D32_SFLOAT
    VkExtent2D     hdrExtent_{};
    VkPipeline     blitPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout blitLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout blitSetLayout_ = VK_NULL_HANDLE;
    VkSampler      linearSampler_ = VK_NULL_HANDLE;
    wp::SceneParams params_{};
```

(Use the project's existing `AllocatedImage`/`ResourceManager` types — match the names already in `ResourceManager.hpp`.)

- [ ] **Step 2: Create the HDR target + depth + linear sampler at init**

In `RenderOrchestrator`'s init (where the swapchain size is known), create the images sized to the swapchain extent:

```cpp
hdrExtent_ = swapchain.extent();
hdrColor_ = resources.createImage(hdrExtent_, VK_FORMAT_R16G16B16A16_SFLOAT,
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
sceneDepth_ = resources.createImage(hdrExtent_, VK_FORMAT_D32_SFLOAT,
    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
linearSampler_ = resources.createLinearSampler();  // add if absent: VK_FILTER_LINEAR, CLAMP_TO_EDGE
```

(If `ResourceManager` lacks `createLinearSampler`, add it: a `VkSamplerCreateInfo` with `VK_FILTER_LINEAR` min/mag and `VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE`.)

- [ ] **Step 3: Build the blit pipeline (fullscreen triangle, samples `hdrColor_`)**

Use `PipelineBuilder` with `shaders/blit.vert`+`shaders/blit.frag`, one combined-image-sampler at binding 0, no depth, no blend, swapchain color format. Store into `blitPipeline_`/`blitLayout_`/`blitSetLayout_`.

Ensure `shaders/blit.frag` samples `binding 0` and writes it straight out:

```glsl
#version 450
layout(location=0) in vec2 vUV;
layout(location=0) out vec4 outColor;
layout(set=0, binding=0) uniform sampler2D uScene;
void main() { outColor = texture(uScene, vUV); }
```

`shaders/blit.vert` emits a fullscreen triangle:

```glsl
#version 450
layout(location=0) out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
```

- [ ] **Step 4: Record the frame — clear HDR, then blit to swapchain**

Replace the temporary swapchain-clear from Task 1 with:

```cpp
// Pass A: begin dynamic rendering on hdrColor_ (+ sceneDepth_), clear to params_.clearColor.
//   (No draws yet — later tasks add tunnel + prism here.) End rendering.
// Transition hdrColor_ to SHADER_READ_ONLY_OPTIMAL.
// Pass B: begin dynamic rendering on the swapchain image; bind blitPipeline_ with a
//   descriptor pointing hdrColor_ view + linearSampler_; vkCmdDraw(cmd, 3, 1, 0, 0). End.
// Transition swapchain image to PRESENT_SRC_KHR.
```

Use the existing `PassRecorder` helpers for the barriers/attachment setup (match how the retired orchestrator did dynamic rendering).

- [ ] **Step 5: Build and run**

Run: `cmake --build build && ./bin/CrystalClockVK.exe`
Expected: the window shows the dark-purple clear color — but now routed through the HDR offscreen target and a blit (visually identical to Task 1, proving the spine works).

- [ ] **Step 6: Verify the two passes in RenderDoc (or validation layers clean)**

Run with validation layers enabled; expect zero errors. If RenderDoc is wired via `RenderDocWrapper`, capture one frame and confirm two render passes: an HDR target clear and a swapchain blit sampling it.

- [ ] **Step 7: Commit**

```bash
git add src/app/RenderOrchestrator.hpp src/app/RenderOrchestrator.cpp src/main.cpp \
        src/renderer/ResourceManager.hpp src/renderer/ResourceManager.cpp \
        shaders/blit.vert shaders/blit.frag
git commit -m "Feat(App): offscreen HDR target with fullscreen blit to swapchain"
```

---

## Task 6: Cloud-tunnel background pass (`ps2menu` shader port)

**Files:**
- Create: `shaders/tunnel.vert`, `shaders/tunnel.frag`
- Create: `src/assets/TextureLoader.hpp`, `src/assets/TextureLoader.cpp` (load `util/noise` PNG)
- Modify: `CMakeLists.txt` (add `assets` sources to app; add stb include dir)
- Modify: `src/app/RenderOrchestrator.*` (add the tunnel pass + noise texture + a small UBO)
- Add: `3rdparty/stb/stb_image.h` (vendored single header)

**Interfaces:**
- Consumes: `hdrColor_`/`sceneDepth_` target (Task 5), `wp::SceneParams`.
- Produces: an animated cloud tunnel filling the HDR target before any prism draw. `TextureLoader::loadPng(path) -> AllocatedImage` for reuse by later texture tasks.

- [ ] **Step 1: Vendor `stb_image.h` and add an assets loader**

Download `stb_image.h` into `3rdparty/stb/`. Create `src/assets/TextureLoader.cpp` with `#define STB_IMAGE_IMPLEMENTATION` in exactly one TU, exposing:

```cpp
// src/assets/TextureLoader.hpp
#pragma once
#include <string>
#include "renderer/ResourceManager.hpp"
namespace wp {
AllocatedImage loadPng(ResourceManager& res, const std::string& path);  // RGBA8, uploaded + sampled
}
```

Convert the scene's noise texture to PNG first (the scene ships `util/noise` as a `.tex`; export it to `assets/noise.png` — a tiling grayscale noise). If a ready PNG is unavailable, generate a 256×256 value-noise PNG offline and place it at `assets/noise.png`.

- [ ] **Step 2: Write `shaders/tunnel.vert` (fullscreen, passes clip position)**

```glsl
#version 450
layout(location=0) out vec2 vUV;
layout(location=1) out vec4 vPosition;  // clip-space position for the tunnel math
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vUV = p;
    vec4 clip = vec4(p * 2.0 - 1.0, 0.0, 1.0);
    vPosition = clip;
    gl_Position = clip;
}
```

- [ ] **Step 3: Write `shaders/tunnel.frag` (port of `ps2menu.frag`)**

```glsl
#version 450
layout(location=0) in vec2 vUV;
layout(location=1) in vec4 vPosition;
layout(location=0) out vec4 outColor;

layout(set=0, binding=0) uniform sampler2D uNoise;
layout(set=0, binding=1) uniform TunnelUBO {
    vec2  texelSize;   // (1/w, 1/h)
    float time;
    float fov;         // degrees
    float center;      // 0.4
    vec3  bg;          // background color
} u;

const int numIters = 4;
const vec3 noiseIters[4] = vec3[](
    vec3(0.25, 4.0, 0.035), vec3(0.5, 2.0, 0.04),
    vec3(1.5, 1.0, 0.0435), vec3(5.5, 1.0, 0.0465));

void main() {
    float TIMESCALE = 0.4, ZOOM = 0.65, MINB = 0.7, MAXB = 1.0;
    float time = u.time * TIMESCALE;
    vec2 p = vPosition.xy / vPosition.w;
    float fov = u.fov * -0.02 + 1.4;
    p += vec2(-2.0 * u.center + 1.0, 0.0);
    float aspect = u.texelSize.y / u.texelSize.x;
    p.x *= aspect;
    p *= (1.0 - fov) + length(p) * fov;
    p *= ZOOM;
    float r = length(p);
    float a = atan(p.y / p.x);
    vec2 uv = vec2(0.3 / r, a / 3.1415927);
    float totalWeight = 0.0, noise = 0.0;
    for (int i = 0; i < numIters; ++i) {
        totalWeight += noiseIters[i].y;
        noise += texture(uNoise, noiseIters[i].x * vec2(uv.x + noiseIters[i].z * time, uv.y)).x * noiseIters[i].y;
    }
    noise *= (MAXB - MINB) / totalWeight;
    noise += MINB;
    vec3 col = u.bg * noise * min(1.0, 0.1 + 0.9 * r) * 0.6;
    outColor = vec4(col, 1.0);
}
```

- [ ] **Step 4: Wire the tunnel pass in `RenderOrchestrator`**

Load `assets/noise.png` at init (`wp::loadPng`). Build a tunnel pipeline (fullscreen triangle, additive over the cleared HDR target, no depth test/write). Each frame, update the `TunnelUBO` (`texelSize` from extent, `time` from `TimeSync` elapsed seconds, `fov=params_.fovDeg`, `center=0.4`, `bg=params_.tunnelBg`) and draw 3 vertices inside Pass A, before the (still empty) prism draw.

- [ ] **Step 5: Build and run**

Run: `cmake --build build && ./bin/CrystalClockVK.exe`
Expected: an animated purple cloud tunnel radiating from screen center, drifting over time.

- [ ] **Step 6: Commit**

```bash
git add shaders/tunnel.vert shaders/tunnel.frag src/assets/ 3rdparty/stb/ \
        assets/noise.png src/app/RenderOrchestrator.hpp src/app/RenderOrchestrator.cpp CMakeLists.txt
git commit -m "Feat(App): animated cloud-tunnel background (ps2menu port)"
```

---

## Task 7: Single glowing prism (crystal shader port) — the hero shot

**Files:**
- Create: `shaders/crystal.vert`, `shaders/crystal.frag` (single-light path, no NORMALMAP)
- Modify: `src/app/RenderOrchestrator.*` (upload `buildPrism` mesh; add the prism pass sampling the HDR copy)
- Modify: `src/main.cpp` (feed wall-clock + camera each frame)

**Interfaces:**
- Consumes: `wp::buildPrism` (Task 4), `wp::SceneCamera` (Task 3), `wp::computeClock`/`glowLight` (Task 2), the HDR target + noise texture (Tasks 5–6).
- Produces: one additive, depth-tested crystal at the origin, glowing per the current minute, tinted between start/end colors, refracting the tunnel behind it. (The 12-prism ring and true framebuffer-copy refraction are Plan 2; Plan 1 samples the current HDR target directly for refraction to prove the shader.)

- [ ] **Step 1: Port `shaders/crystal.vert`**

```glsl
#version 450
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aTexCoord;

layout(location=0) out vec3 vNormal;
layout(location=1) out vec3 vScreenPos;
layout(location=2) out vec2 vTexCoord;
layout(location=3) out vec3 vViewDir;
layout(location=4) out vec3 vAmbient;
layout(location=5) out float vHeight;

layout(set=0, binding=0) uniform CrystalVsUBO {
    mat4 model;
    mat4 viewProj;
    vec3 eye;
    vec3 ambientColor;
    vec3 skylightColor;
} u;

void main() {
    vec4 worldPos = u.model * vec4(aPosition, 1.0);
    gl_Position = u.viewProj * worldPos;
    vec3 n = normalize(mat3(u.model) * aNormal);
    vTexCoord = aTexCoord;
    vScreenPos = gl_Position.xyw;
    vViewDir = u.eye - worldPos.xyz;
    vHeight = aPosition.y;
    vAmbient = mix(u.skylightColor, u.ambientColor, dot(n, vec3(0,1,0)) * 0.5 + 0.5);
    vNormal = n;
}
```

(Convention: column-major GLM with `M * v`. This replaces WE's row-vector `mul(v, M)`.)

- [ ] **Step 2: Port `shaders/crystal.frag` (refraction samples the HDR scene copy)**

```glsl
#version 450
layout(location=0) in vec3 vNormal;
layout(location=1) in vec3 vScreenPos;
layout(location=2) in vec2 vTexCoord;
layout(location=3) in vec3 vViewDir;
layout(location=4) in vec3 vAmbient;
layout(location=5) in float vHeight;
layout(location=0) out vec4 outColor;

layout(set=0, binding=1) uniform sampler2D uClouds;   // util/clouds_256 (reuse noise for now)
layout(set=0, binding=2) uniform sampler2D uScene;    // HDR framebuffer copy
layout(set=0, binding=3) uniform CrystalFsUBO {
    vec3  color1;
    vec3  color2;
    float light;
    float time;
    float tintPeriod;
    float fadeAlpha;
} u;

void main() {
    vec3 normal = normalize(vNormal);
    vec3 viewDir = normalize(vViewDir);
    vec2 screenUV = (vScreenPos.xy / vScreenPos.z) * 0.5 + 0.5;

    float rim = 1.0 - max(0.0, dot(viewDir, normal));
    float emissive = smoothstep(vHeight * 0.95, vHeight * 0.95 + 0.5, u.light) * 0.5;
    emissive += rim;
    emissive += step(0.0, u.light) * 0.25;

    vec4 diffuse = texture(uClouds, vTexCoord * 4.0);
    diffuse.rgb *= vAmbient;

    vec2 refrOffset = refract(viewDir, normal, 0.5).xy / vScreenPos.z;
    vec3 refr = texture(uScene, screenUV + refrOffset).rgb;
    refr = refr * 2.0 * (0.75 + emissive * 4.0);

    float refl = texture(uClouds, normal.xy + vec2(vHeight * 0.002)).r;
    refl *= refl; refl *= refl; refl *= refl * 0.6;

    vec3 finalColor = mix(refr, diffuse.rgb, diffuse.r * 0.2);
    float tintLerp = abs(mod(u.time / u.tintPeriod, 1.0) * 2.0 - 1.0);
    finalColor *= mix(u.color1, u.color2, tintLerp);
    finalColor += refl;
    outColor = vec4(finalColor * u.fadeAlpha, 1.0);
}
```

- [ ] **Step 3: Upload the prism mesh and build the prism pipeline**

At init: `wp::MeshData mesh = wp::buildPrism(6, 1.0f, 2.0f);` upload to a vertex + index buffer via `ResourceManager`. Build a pipeline with the `Vertex` layout (pos/normal/uv), **additive** blend, depth test+write, `cullMode = NONE` for Plan 1 (single prism, avoid back-face surprises), HDR color format, samplers for clouds(=noise) and the HDR scene at bindings 1/2.

- [ ] **Step 4: Draw the prism each frame inside Pass A**

After the tunnel draw, before ending Pass A: transition a copy of the HDR color to sampled (or, for Plan 1 simplicity, sample the noise as `uScene` placeholder is NOT allowed — instead do a quick `hdrColor_`→`sceneCopy_` blit as in the design step 2, then bind `sceneCopy_` as `uScene`). Update the VS UBO (`model = translate(scale(prismScale)) * rotate(local spin)`, `viewProj = cam.projection(aspect) * cam.view()`, `eye`, ambient `(0.302)`, skylight `(1,1,1)`), and the FS UBO (`color1=startColor`, `color2=endColor`, `light = glowLight(now)`, `time`, `tintPeriod`, `fadeAlpha=1`). Bind vertex/index buffers; `vkCmdDrawIndexed`.

- [ ] **Step 5: Feed wall-clock + advance camera in `main.cpp`**

Each frame: read local time into `wp::WallClock`, advance `SceneCamera::update(dt)` from `TimeSync` dt, pass both to the orchestrator.

- [ ] **Step 6: Build and run — verify the hero shot**

Run: `cmake --build build && ./bin/CrystalClockVK.exe`
Expected: one faceted crystal at screen center, glowing (fill proportional to the current minute), tinted purple↔teal over ~20 s, softly refracting the cloud tunnel behind it; the camera eases in from far away on startup. Compare against `C:\Users\dell04\Downloads\1979606285\preview.gif` for overall feel.

- [ ] **Step 7: Commit**

```bash
git add shaders/crystal.vert shaders/crystal.frag \
        src/app/RenderOrchestrator.hpp src/app/RenderOrchestrator.cpp src/main.cpp
git commit -m "Feat(App): single glowing refractive crystal over the tunnel"
```

---

## Plan 1 Done — Definition of Done

- `cmake --build build` succeeds; `ctest` runs `scene_clock`, `scene_camera`, `prism_mesh` green.
- `CrystalClockVK.exe` shows: camera ease-in → animated cloud tunnel → one procedurally-modeled crystal glowing by the minute, tinted over time, refracting the background.
- The retired GS/EE track is gone from the tree but preserved under tag `archive/gs-ee-re`.
- No Vulkan/SDL symbols leaked into `src/scene/` or `src/mesh/`.

Plan 2 (12-prism ring + true framebuffer-copy refraction + per-prism inner/main additive passes) is written after Plan 1 review.
```
