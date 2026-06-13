# CrystalClockVK Phase 1: Rod Geometry & Projection — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax.
>
> **Goal:** Stand up the first executable procedural chunk of the crystal-clock port: the rod-field geometry, the rotation (cross-product orthonormal basis) and GS-native projection math, their unit tests, and a minimal flat (vertex-color) Vulkan draw of the rods, gated by the captured rod screen positions and a `--dump-rgba` pixel-diff sanity check. This is LOGIC FIRST — no refraction/glow shaders yet, no GS replay.
>
> **Architecture:** New pure-logic library `clock/` (Vulkan-free, unit-testable, strict layer) holding `ClockMath` (Vec/Mat via GLM), `RodField` (geometry generation), and `Projection` (rotation + GS-native projection). A new `app/` renderer `ClockRenderer` draws the rod field flat onto the kept `src/renderer` foundation (`ResourceManager`/`PassRecorder`/`PipelineBuilder`). The math is verified by EVIDENCE: rotation by an orthonormality test, projection by FIT against the live-captured rod world→screen oracle pairs, fixed-point by a 12.4 round-trip test. RenderDoc is a debug aid only; the numeric tests are the gate.
>
> **Tech Stack:** C++23, Vulkan 1.3, CMake, CTest, GLM.

---

## Ground-truth oracle values (cite these, do not re-derive)

From `docs/ghidra_analysis/runtime-trace.md` (live PCSX2 capture, rod array @ `0x00375250`, stride `0x50`):

| Rod | world X (+0x00) | world Y (+0x04) | world Z (+0x08) | screen X (+0x20) | screen Y (+0x24) | 12.4 fixed X (+0x30) | 12.4 fixed Y (+0x34) |
|----:|---------:|---------:|---------:|---------:|---------:|------:|------:|
| 0 | -13.039 | 14.666 | 50.271 | 1915.20 | 2118.20 | `0x77b3` | `0x8463` |

Supplementary world X seen across the ring (same trace, §"Rod array"): rod1 X=-13.347, rod2 X=-7.916, rod3 X=-8.224, rod4 X=0.234. Screen Y ≈ 2118 for all (they sit on a ring). Only rod0 has a fully decoded world→screen→fixed row; that single row is the hard numeric oracle. The fit tolerance is generous (±3.0 px) because the additional free parameters (FOV global `*gp[-0x73d8]`, near global `*gp[-0x7b78]`, exact column order) are still being resolved (`vu0-math-pipeline.md` §Blockers 1 & 3). The test ASSERTS the projection reproduces rod0 within tolerance; it is a regression lock, not a derivation.

Confirmed evidence-grade projection constants (`vu0-math-pipeline.md` §"Confirmed constants"):
- `far = 2048.0f`, `aspect = 1.0f` (hardcoded square), `scale = 65536.0f`, `unk1 = unk2 = 1.0f`.
- 12.4 fixed-point: `fixed = trunc(screen * 16)` → `0x77b3 = 30643 = 1915.19 * 16`, `0x8463 = 33891 = 2118.19 * 16`.

Rotation is a direct orthonormal basis build from two direction vectors via two cross products (`vu0-math-pipeline.md` §"Rotation Build"): `right = normalize(cross(forward, up))`, `upOrtho = cross(right, forward)`, columns `(right, upOrtho, forward, (0,0,0,1))`. NOT Euler, NOT Rodrigues.

---

## FILE STRUCTURE

Create:
- `src/clock/ClockMath.hpp` — math aliases (`Vec3`/`Vec4`/`Mat4` = GLM column-major) + `Fixed124` 12.4 encode/decode helpers. Vulkan-free.
- `src/clock/Projection.hpp` / `Projection.cpp` — `BuildRotationMatrix(forward, up)`, `BuildProjectionMatrix(ProjectionParams)`, `ProjectWorldToScreen(...)`. Vulkan-free.
- `src/clock/RodField.hpp` / `RodField.cpp` — `Rod` struct (world pos, scale, angle), `RodField::Generate()` producing the rod ring, `RodVertex` flat-render vertex (pos + color). Vulkan-free.
- `src/clock/ClockRenderer.hpp` / `ClockRenderer.cpp` — `app`-layer flat renderer: builds vertex/index buffers from a `RodField`, a pipeline via `PipelineBuilder`, records a flat vertex-color draw, and reads back the target image for pixel-diff. Uses kept `ResourceManager`/`PassRecorder`.
- `shaders/rod_flat.vert` / `shaders/rod_flat.frag` — minimal MVP transform + vertex-color passthrough.
- `tests/RotationBasisTest.cpp` — orthonormality + determinant of `BuildRotationMatrix`.
- `tests/Fixed124Test.cpp` — 12.4 encode/decode round-trip and exact oracle bit patterns.
- `tests/ProjectionFitTest.cpp` — projection reproduces the rod0 world→screen oracle within tolerance; asserts the column-order/fit constants are locked.
- `tests/RodFieldTest.cpp` — `RodField::Generate()` produces the expected rod count and rod0 world position matches the trace.

Modify:
- `CMakeLists.txt` — add `clock` static lib target; link it into the app; register the four CTest executables using the existing loop pattern.

(Not in scope for this chunk: dropping `gs`/`gsvk`/`GsScene`/`GsRenderer` from the app link, and rewiring `main.cpp` to call `ClockRenderer` in the frame loop. This chunk delivers the library + tests + a standalone renderable + its own dump-rgba harness. The `main.cpp` swap is the next chunk.)

---

## TASK 1 — Math types + 12.4 fixed-point conversion

**Files:**
- create `src/clock/ClockMath.hpp`
- create `tests/Fixed124Test.cpp`
- modify `CMakeLists.txt`

- [ ] **Step 1 — Add the `clock` lib target + the test wiring to CMake.** Append to `CMakeLists.txt`, after the `gsvk` block and before `enable_testing()` (or just after it; `enable_testing()` already exists at line 195):

```cmake
# ──────────────────────────────────────────────────────────────────────────────
# clock — pure procedural clock logic (geometry + projection math). NO Vulkan.
# ──────────────────────────────────────────────────────────────────────────────
add_library(clock STATIC
    src/clock/Projection.cpp
    src/clock/RodField.cpp
)
target_include_directories(clock PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(clock PUBLIC glm::glm)
target_compile_definitions(clock PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)
```

Then add the four pure-logic tests using the existing loop pattern (mirrors the `gsvk_blend:GsBlendTest` loop at line 196):

```cmake
# clock pure-logic tests (math/geometry/projection — Vulkan-free).
foreach(t clock_fixed124:Fixed124Test clock_rotation:RotationBasisTest clock_projection:ProjectionFitTest clock_rodfield:RodFieldTest)
    string(REPLACE ":" ";" parts ${t})
    list(GET parts 0 tname)
    list(GET parts 1 tsrc)
    add_executable(${tname}_tests tests/${tsrc}.cpp)
    target_link_libraries(${tname}_tests PRIVATE clock)
    set_target_properties(${tname}_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
    add_test(NAME ${tname} COMMAND ${tname}_tests)
endforeach()
```

- [ ] **Step 2 — Write the failing test.** Create `tests/Fixed124Test.cpp`:

```cpp
// 12.4 fixed-point conversion: GS encodes screen XY as trunc(value * 16).
// Oracle bit patterns from runtime-trace.md rod0: 1915.19 -> 0x77b3, 2118.19 -> 0x8463.

#include <cstdint>
#include <cstdio>
#include <string>

#include "clock/ClockMath.hpp"

namespace {

int g_fails = 0;
void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("  FAIL: %s\n", what.c_str()); g_fails++; }
}

}  // namespace

int main() {
    using clock::Fixed124;

    // Encode: trunc(value * 16).
    check(Fixed124::encode(1915.1875f) == 0x77b3, "encode 1915.1875 -> 0x77b3");
    check(Fixed124::encode(2118.1875f) == 0x8463, "encode 2118.1875 -> 0x8463");
    check(Fixed124::encode(0.0f) == 0, "encode 0.0 -> 0");
    check(Fixed124::encode(1.0f) == 16, "encode 1.0 -> 16");

    // Decode: fixed / 16.
    check(Fixed124::decode(0x77b3) == 30643.0f / 16.0f, "decode 0x77b3");
    check(Fixed124::decode(16) == 1.0f, "decode 16 -> 1.0");

    // Round-trip: decode(encode(x)) reproduces x to within 1/16 (the quantum).
    for (float x = 0.0f; x < 2200.0f; x += 0.37f) {
        float rt = Fixed124::decode(Fixed124::encode(x));
        check(x - rt < 1.0f / 16.0f + 1e-4f && rt - x <= 1e-4f,
              "round-trip within quantum at " + std::to_string(x));
    }

    if (g_fails) { std::printf("fixed12.4: %d FAILURES\n", g_fails); return 1; }
    std::printf("fixed12.4: OK\n");
    return 0;
}
```

- [ ] **Step 3 — Run it; expect a CONFIGURE/COMPILE failure (no `ClockMath.hpp` yet).**

```
cmake -B build
cmake --build build --target clock_fixed124_tests
```

Expected: build fails — `fatal error: clock/ClockMath.hpp: No such file or directory`. This is the RED state.

- [ ] **Step 4 — Minimal implementation.** Create `src/clock/ClockMath.hpp`:

```cpp
#pragma once

#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>

namespace clock {

using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using Mat4 = glm::mat4;  // column-major, matching PS2 VU0 column-major storage

// GS hardware encodes screen-space XY as 12.4 fixed-point: trunc(value * 16).
// Evidence: runtime-trace.md rod0 +0x30/+0x34 (0x77b3 = 1915.19*16, 0x8463 = 2118.19*16).
struct Fixed124 {
    static int32_t encode(float value) {
        return static_cast<int32_t>(value * 16.0f);  // truncation toward zero (FTOI4)
    }
    static float decode(int32_t fixed) {
        return static_cast<float>(fixed) / 16.0f;  // VITOF4
    }
};

}  // namespace clock
```

- [ ] **Step 5 — Run it; expect PASS.**

```
cmake --build build --target clock_fixed124_tests
ctest --test-dir build -R clock_fixed124 --output-on-failure
```

Expected: `fixed12.4: OK` and `1/1 Test #N: clock_fixed124 ... Passed`.

- [ ] **Step 6 — Commit.**

```
git add src/clock/ClockMath.hpp tests/Fixed124Test.cpp CMakeLists.txt
git commit -m "Feat(App): Add clock math lib + 12.4 fixed-point round-trip test"
```

---

## TASK 2 — Rotation: cross-product orthonormal basis

**Files:**
- create `src/clock/Projection.hpp`
- create `src/clock/Projection.cpp`
- create `tests/RotationBasisTest.cpp`

- [ ] **Step 1 — Write the failing test.** Create `tests/RotationBasisTest.cpp`. It asserts the rotation matrix built from two direction vectors is orthonormal (columns unit-length, mutually perpendicular), right-handed (det = +1), and that the NaN guard zeroes the matrix:

```cpp
// rotation_build (FUN_002732d8): direct orthonormal basis from two direction
// vectors via two cross products. right = normalize(cross(fwd, up)),
// upOrtho = cross(right, fwd), columns (right, upOrtho, fwd, (0,0,0,1)).
// NOT Euler, NOT Rodrigues. Evidence: vu0-math-pipeline.md §"Rotation Build".

#include <cstdio>
#include <string>

#include <glm/gtc/matrix_access.hpp>

#include "clock/ClockMath.hpp"
#include "clock/Projection.hpp"

namespace {

int g_fails = 0;
void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("  FAIL: %s\n", what.c_str()); g_fails++; }
}
bool near(float a, float b, float eps = 1e-5f) { return (a - b < eps) && (b - a < eps); }

void assertOrthonormal(const clock::Mat4& m, const std::string& tag) {
    clock::Vec3 c0 = clock::Vec3(glm::column(m, 0));
    clock::Vec3 c1 = clock::Vec3(glm::column(m, 1));
    clock::Vec3 c2 = clock::Vec3(glm::column(m, 2));

    check(near(glm::length(c0), 1.0f), tag + " col0 unit");
    check(near(glm::length(c1), 1.0f), tag + " col1 unit");
    check(near(glm::length(c2), 1.0f), tag + " col2 unit");

    check(near(glm::dot(c0, c1), 0.0f), tag + " col0.col1 perpendicular");
    check(near(glm::dot(c1, c2), 0.0f), tag + " col1.col2 perpendicular");
    check(near(glm::dot(c0, c2), 0.0f), tag + " col0.col2 perpendicular");

    check(near(glm::determinant(clock::Mat4(clock::Vec4(c0, 0), clock::Vec4(c1, 0),
                                            clock::Vec4(c2, 0), clock::Vec4(0, 0, 0, 1))),
               1.0f),
          tag + " right-handed det=+1");

    // W column is (0,0,0,1) — no translation in the rotation matrix.
    clock::Vec4 c3 = glm::column(m, 3);
    check(near(c3.x, 0) && near(c3.y, 0) && near(c3.z, 0) && near(c3.w, 1), tag + " no translation");
}

}  // namespace

int main() {
    // Axis-aligned: forward = -Z, up = +Y → identity-like basis.
    assertOrthonormal(clock::BuildRotationMatrix(clock::Vec3(0, 0, -1), clock::Vec3(0, 1, 0)),
                      "forward=-Z up=+Y");

    // Skewed inputs (up not perpendicular to forward) must still orthonormalize.
    assertOrthonormal(clock::BuildRotationMatrix(clock::Vec3(1, 0.2f, -0.3f), clock::Vec3(0.1f, 1, 0)),
                      "skewed");

    // Arbitrary rod-ish direction.
    assertOrthonormal(clock::BuildRotationMatrix(clock::Vec3(-13.0f, 14.0f, 50.0f), clock::Vec3(0, 1, 0)),
                      "rod0 direction");

    // NaN guard (bc1t -> memclr): degenerate input (forward parallel to up) zeroes the matrix.
    clock::Mat4 z = clock::BuildRotationMatrix(clock::Vec3(0, 1, 0), clock::Vec3(0, 1, 0));
    check(near(glm::column(z, 0).x, 0) && near(glm::column(z, 0).y, 0) && near(glm::column(z, 0).z, 0),
          "degenerate input zeroes matrix");

    if (g_fails) { std::printf("rotation basis: %d FAILURES\n", g_fails); return 1; }
    std::printf("rotation basis: OK\n");
    return 0;
}
```

- [ ] **Step 2 — Run it; expect a COMPILE failure (no `Projection.hpp` yet).**

```
cmake -B build
cmake --build build --target clock_rotation_tests
```

Expected: build fails — `clock/Projection.hpp: No such file or directory`. RED.

- [ ] **Step 3 — Implement the header and the rotation function.** Create `src/clock/Projection.hpp`:

```cpp
#pragma once

#include "clock/ClockMath.hpp"

namespace clock {

// GS-native projection parameters. Evidence-grade constants from
// vu0-math-pipeline.md §"Confirmed constants": far=2048, aspect=1, scale=65536.
// fov/near are runtime globals (gp[-0x73d8]/gp[-0x7b78]); fitted in ProjectionFitTest.
struct ProjectionParams {
    float fov;        // radians, *gp[-0x73d8] (fitted)
    float near;       // *gp[-0x7b78] (fitted)
    float halfWidth;  // *gp[-0x7b80] (4:3) / *gp[-0x7b7c] (16:9)
    float far = 2048.0f;
    float aspect = 1.0f;
    float screenCenterX = 1024.0f;  // GS draw area 0..2048, center 1024
    float screenCenterY = 1024.0f;
};

// rotation_build (FUN_002732d8): orthonormal basis from two direction vectors.
Mat4 BuildRotationMatrix(const Vec3& forward, const Vec3& up);

// projection_build (FUN_002730a8): custom GS-native perspective that embeds the
// viewport transform (NDC -> GS [0,2048]). Returns a column-major Mat4.
Mat4 BuildProjectionMatrix(const ProjectionParams& p);

// Full world -> GS screen pixel projection (proj * world, perspective divide,
// NDC -> GS pixel). Returns screen XY in GS pixels (pre-12.4-fixed).
glm::vec2 ProjectWorldToScreen(const Mat4& proj, const Vec3& world);

}  // namespace clock
```

Create `src/clock/Projection.cpp` with the rotation function (projection follows in Task 3):

```cpp
#include "clock/Projection.hpp"

#include <cmath>

namespace clock {

Mat4 BuildRotationMatrix(const Vec3& forward, const Vec3& up) {
    // bc1t -> memclr overflow guard: degenerate inputs zero the matrix.
    Vec3 fwd = glm::normalize(forward);
    Vec3 rightRaw = glm::cross(fwd, up);          // VOPMSUB @ 0x273318
    float rl = glm::length(rightRaw);
    if (!(rl > 1e-6f) || std::isnan(fwd.x) || std::isnan(rl)) {
        return Mat4(0.0f);
    }
    Vec3 right = rightRaw / rl;
    Vec3 upOrtho = glm::cross(right, fwd);        // VOPMSUB @ 0x273370

    return Mat4(
        Vec4(right, 0.0f),     // col 0
        Vec4(upOrtho, 0.0f),   // col 1
        Vec4(fwd, 0.0f),       // col 2
        Vec4(0, 0, 0, 1.0f));  // col 3 — no translation
}

}  // namespace clock
```

- [ ] **Step 4 — Run it; expect PASS.**

```
cmake --build build --target clock_rotation_tests
ctest --test-dir build -R clock_rotation --output-on-failure
```

Expected: `rotation basis: OK`, test Passed.

- [ ] **Step 5 — Commit.**

```
git add src/clock/Projection.hpp src/clock/Projection.cpp tests/RotationBasisTest.cpp
git commit -m "Feat(App): Add cross-product orthonormal rotation + basis test"
```

---

## TASK 3 — GS-native projection, FIT against the rod0 oracle

**Files:**
- modify `src/clock/Projection.cpp`
- create `tests/ProjectionFitTest.cpp`

- [ ] **Step 1 — Write the failing fit test.** Create `tests/ProjectionFitTest.cpp`. It asserts the projection maps the live rod0 world position to its captured screen pixel within tolerance, then re-encodes to 12.4 and checks the fixed-point oracle bits. The fitted `fov`/`near`/`halfWidth` are locked here:

```cpp
// Projection FIT gate. The custom GS-native projection (FUN_002730a8) maps
// world -> GS screen [0,2048]. Oracle from runtime-trace.md rod0:
//   world (-13.039, 14.666, 50.271) -> screen (1915.20, 2118.20)
//                                    -> 12.4 fixed (0x77b3, 0x8463).
// fov/near/halfWidth are runtime globals (still being resolved upstream); they
// are FITTED here against this single fully-decoded oracle row and locked as a
// regression. Tolerance is +/-3.0 px (free-parameter slack, vu0-math §Blockers 1&3).

#include <cstdio>
#include <string>
#include <cmath>

#include "clock/ClockMath.hpp"
#include "clock/Projection.hpp"

namespace {

int g_fails = 0;
void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("  FAIL: %s\n", what.c_str()); g_fails++; }
}

// Fitted parameters (locked). If a future ground-truth read changes fov/near,
// update these three lines and the tolerance comment together.
constexpr float kFitFovRadians = 1.047f;  // ~60 deg, vu0-math §Blockers hypothesis
constexpr float kFitNear       = 1.0f;
constexpr float kFitHalfWidth  = 512.0f;  // 4:3 path, fitted

clock::ProjectionParams fittedParams() {
    clock::ProjectionParams p;
    p.fov = kFitFovRadians;
    p.near = kFitNear;
    p.halfWidth = kFitHalfWidth;
    return p;  // far=2048, aspect=1, center (1024,1024) from defaults
}

}  // namespace

int main() {
    const clock::Mat4 proj = clock::BuildProjectionMatrix(fittedParams());

    const clock::Vec3 rod0World(-13.039f, 14.666f, 50.271f);
    const glm::vec2 screen = clock::ProjectWorldToScreen(proj, rod0World);

    const float kOracleX = 1915.20f;
    const float kOracleY = 2118.20f;
    const float kTolPx = 3.0f;

    std::printf("  rod0 projected screen = (%.3f, %.3f), oracle = (%.3f, %.3f)\n",
                screen.x, screen.y, kOracleX, kOracleY);

    check(std::fabs(screen.x - kOracleX) <= kTolPx, "rod0 screen X within 3.0 px");
    check(std::fabs(screen.y - kOracleY) <= kTolPx, "rod0 screen Y within 3.0 px");

    // 12.4 fixed-point oracle bits (after the same projection).
    const int32_t fx = clock::Fixed124::encode(screen.x);
    const int32_t fy = clock::Fixed124::encode(screen.y);
    // Within the +/-3px*16 = +/-48 fixed-quantum band around the captured bits.
    check(std::abs(fx - 0x77b3) <= 48, "rod0 12.4 X near 0x77b3");
    check(std::abs(fy - 0x8463) <= 48, "rod0 12.4 Y near 0x8463");

    if (g_fails) { std::printf("projection fit: %d FAILURES\n", g_fails); return 1; }
    std::printf("projection fit: OK\n");
    return 0;
}
```

- [ ] **Step 2 — Run it; expect FAIL (projection not implemented — link error or assertion fail).**

```
cmake -B build
cmake --build build --target clock_projection_tests
ctest --test-dir build -R clock_projection --output-on-failure
```

Expected: link error (`undefined reference to clock::BuildProjectionMatrix` / `ProjectWorldToScreen`). RED.

- [ ] **Step 3 — Implement projection + projection helper, then FIT.** Append to `src/clock/Projection.cpp`:

```cpp
Mat4 BuildProjectionMatrix(const ProjectionParams& p) {
    // Custom GS-native perspective: NDC [-1,1] -> GS [0, 2048], embedding the
    // viewport transform. f = 1/tan(fov/2); aspect=1 so sx==sy.
    // Z maps near->0, far->1 (GLM_FORCE_DEPTH_ZERO_TO_ONE), then perspective Q.
    const float f = 1.0f / std::tan(p.fov * 0.5f);
    const float qz = p.far / (p.far - p.near);
    const float tz = -(p.far * p.near) / (p.far - p.near);

    const float sx = f * p.halfWidth;
    const float sy = (f * p.halfWidth) / p.aspect;

    // Column-major (PS2 VU0 stores columns via SQC2). Row 3 carries the GS-space
    // offset (center) and Row 2/3 the perspective Z; the W row pulls -z for divide.
    return Mat4(
        Vec4(sx, 0, 0, 0),                                   // col 0
        Vec4(0, sy, 0, 0),                                   // col 1
        Vec4(p.screenCenterX, p.screenCenterY, qz, -1.0f),   // col 2
        Vec4(0, 0, tz, 0));                                  // col 3
}

glm::vec2 ProjectWorldToScreen(const Mat4& proj, const Vec3& world) {
    Vec4 clip = proj * Vec4(world, 1.0f);
    float invW = (clip.w != 0.0f) ? (1.0f / clip.w) : 0.0f;
    // Perspective divide already lands XY in GS pixel space (center embedded).
    return glm::vec2(clip.x * invW, clip.y * invW);
}
```

- [ ] **Step 4 — Run; iterate the three fitted constants in the TEST until PASS.** This is an explicit fit step: run, read the printed projected vs oracle, adjust `kFitFovRadians` / `kFitNear` / `kFitHalfWidth` (and only if the residual is structural, the column arrangement in `BuildProjectionMatrix`), re-run until both axes are within 3.0 px. The column-order ambiguity is expected (`vu0-math-pipeline.md` §Blocker 3 — "a pixel-diff test will resolve this faster than static analysis"); this test IS that resolver.

```
cmake --build build --target clock_projection_tests
ctest --test-dir build -R clock_projection --output-on-failure
```

Expected after fit: `projection fit: OK`, test Passed. The printed line shows `rod0 projected screen` within 3 px of `(1915.20, 2118.20)`.

- [ ] **Step 5 — Commit.**

```
git add src/clock/Projection.cpp tests/ProjectionFitTest.cpp
git commit -m "Feat(App): Add GS-native projection fitted to rod0 screen oracle"
```

---

## TASK 4 — RodField generation

**Files:**
- create `src/clock/RodField.hpp`
- create `src/clock/RodField.cpp`
- create `tests/RodFieldTest.cpp`

- [ ] **Step 1 — Write the failing test.** Create `tests/RodFieldTest.cpp`. It asserts `RodField::Generate()` produces the rod ring and that rod0's world position matches the captured trace:

```cpp
// RodField geometry. The rod array @ 0x00375250 is the geometry to reproduce.
// rod0 world (+0x00/04/08) = (-13.039, 14.666, 50.271). Group A has 8 front rods
// (rod-pipeline.md §Pass1: "i > 7 (group A skip)"); this chunk generates group A.

#include <cstdio>
#include <string>
#include <cmath>

#include "clock/ClockMath.hpp"
#include "clock/RodField.hpp"

namespace {

int g_fails = 0;
void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("  FAIL: %s\n", what.c_str()); g_fails++; }
}
bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

}  // namespace

int main() {
    const clock::RodField field = clock::RodField::Generate();

    // Group A is 8 front rods (rod-pipeline.md Pass1 skip condition i > 7).
    check(field.rods.size() == 8, "group A has 8 rods");

    // rod0 world position matches the live trace within 0.05 (capture precision).
    const clock::Rod& r0 = field.rods[0];
    check(near(r0.world.x, -13.039f, 0.05f), "rod0 world X = -13.039");
    check(near(r0.world.y, 14.666f, 0.05f), "rod0 world Y = 14.666");
    check(near(r0.world.z, 50.271f, 0.05f), "rod0 world Z = 50.271");

    // Default scale is unit (trace +0x10/+0x14 = 1.0).
    check(near(r0.scale.x, 1.0f, 1e-4f) && near(r0.scale.y, 1.0f, 1e-4f), "rod0 unit scale");

    // Rods sit on a ring: all share ~constant radius in XZ from a common center.
    const float r0Radius = std::sqrt(r0.world.x * r0.world.x + r0.world.z * r0.world.z);
    for (size_t i = 0; i < field.rods.size(); ++i) {
        const clock::Vec3& w = field.rods[i].world;
        float rad = std::sqrt(w.x * w.x + w.z * w.z);
        check(near(rad, r0Radius, 1.0f), "rod " + std::to_string(i) + " on ring radius");
    }

    // Flat-render mesh: each rod yields vertices + indices (non-empty).
    const auto mesh = field.buildFlatMesh();
    check(!mesh.vertices.empty(), "flat mesh has vertices");
    check(!mesh.indices.empty(), "flat mesh has indices");
    check(mesh.indices.size() % 3 == 0, "flat mesh indices are triangles");

    if (g_fails) { std::printf("rod field: %d FAILURES\n", g_fails); return 1; }
    std::printf("rod field: OK\n");
    return 0;
}
```

- [ ] **Step 2 — Run it; expect a COMPILE failure (no `RodField.hpp`).**

```
cmake -B build
cmake --build build --target clock_rodfield_tests
```

Expected: `clock/RodField.hpp: No such file or directory`. RED.

- [ ] **Step 3 — Implement RodField.** Create `src/clock/RodField.hpp`:

```cpp
#pragma once

#include <vector>
#include <cstdint>

#include "clock/ClockMath.hpp"

namespace clock {

// One crystal rod. Mirrors the on-PS2 ROD record fields this chunk needs
// (rod-pipeline.md ROD struct 0x160; runtime-trace.md +0x00 world, +0x10 scale).
struct Rod {
    Vec3 world;          // +0x00/04/08
    Vec3 scale{1, 1, 1}; // +0x10/14
    float angle{0.0f};   // per-frame rotation angle (+0x04 per render loop)
};

// Flat (vertex-color) vertex for the minimal render: world pos + RGBA color.
struct RodVertex {
    Vec3 pos;
    Vec4 color;
};

struct FlatMesh {
    std::vector<RodVertex> vertices;
    std::vector<uint32_t> indices;
};

class RodField {
public:
    // Generate group A (the 8 front rods of the ring) at their captured ring
    // positions. rod0 is pinned to the trace; the rest are distributed on the
    // ring. (Full radial parameterization is fitted in a later chunk.)
    static RodField Generate();

    // Build a flat triangle mesh (a small quad/prism per rod) for the sanity draw.
    FlatMesh buildFlatMesh() const;

    std::vector<Rod> rods;
};

}  // namespace clock
```

Create `src/clock/RodField.cpp`:

```cpp
#include "clock/RodField.hpp"

#include <cmath>

namespace clock {

namespace {
constexpr int kGroupACount = 8;  // rod-pipeline.md Pass1 skip: i > 7
}

RodField RodField::Generate() {
    RodField f;
    f.rods.reserve(kGroupACount);

    // rod0 pinned to the live capture (runtime-trace.md).
    const Vec3 rod0World(-13.039f, 14.666f, 50.271f);
    const float centerY = rod0World.y;
    const float radius = std::sqrt(rod0World.x * rod0World.x + rod0World.z * rod0World.z);
    const float baseAngle = std::atan2(rod0World.z, rod0World.x);

    for (int i = 0; i < kGroupACount; ++i) {
        const float a = baseAngle + (float(i) * 2.0f * 3.14159265358979f / float(kGroupACount));
        Rod r;
        r.world = (i == 0) ? rod0World
                           : Vec3(radius * std::cos(a), centerY, radius * std::sin(a));
        r.scale = Vec3(1, 1, 1);
        r.angle = a;
        f.rods.push_back(r);
    }
    return f;
}

FlatMesh RodField::buildFlatMesh() const {
    FlatMesh m;
    // A small upright quad per rod (two triangles), vertex-colored white.
    // Geometry is placeholder for the flat sanity draw; the real rod prism mesh
    // is built in a later chunk once projection is locked.
    const float hw = 0.5f;   // half width
    const float hh = 4.0f;   // half height
    const Vec4 white(1, 1, 1, 1);

    for (const Rod& rod : rods) {
        const uint32_t base = static_cast<uint32_t>(m.vertices.size());
        const Vec3& c = rod.world;
        m.vertices.push_back({Vec3(c.x - hw, c.y - hh, c.z), white});
        m.vertices.push_back({Vec3(c.x + hw, c.y - hh, c.z), white});
        m.vertices.push_back({Vec3(c.x + hw, c.y + hh, c.z), white});
        m.vertices.push_back({Vec3(c.x - hw, c.y + hh, c.z), white});
        m.indices.insert(m.indices.end(),
                         {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
    }
    return m;
}

}  // namespace clock
```

- [ ] **Step 4 — Run it; expect PASS.**

```
cmake --build build --target clock_rodfield_tests
ctest --test-dir build -R clock_rodfield --output-on-failure
```

Expected: `rod field: OK`, test Passed.

- [ ] **Step 5 — Run the full clock suite to confirm nothing regressed.**

```
ctest --test-dir build -R "clock_" --output-on-failure
```

Expected: 4/4 (`clock_fixed124`, `clock_rotation`, `clock_projection`, `clock_rodfield`) Passed.

- [ ] **Step 6 — Commit.**

```
git add src/clock/RodField.hpp src/clock/RodField.cpp tests/RodFieldTest.cpp
git commit -m "Feat(App): Add RodField group-A generation + flat mesh test"
```

---

## TASK 5 — Flat Vulkan draw of the rods + --dump-rgba pixel-diff sanity check

This task draws the rod field flat (vertex color) onto the kept `src/renderer` foundation and reads it back. It produces a self-contained `ClockRenderer` plus a tiny headless harness so the draw can be sanity-checked numerically without rewiring `main.cpp` yet.

**Files:**
- create `shaders/rod_flat.vert`
- create `shaders/rod_flat.frag`
- create `src/clock/ClockRenderer.hpp`
- create `src/clock/ClockRenderer.cpp`
- modify `CMakeLists.txt`

- [ ] **Step 1 — Write the shaders** (glob-compiled to `bin/shaders/` by the existing CMake block).

`shaders/rod_flat.vert`:

```glsl
#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(push_constant) uniform Push {
    mat4 mvp;
} pc;

layout(location = 0) out vec4 fragColor;

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    fragColor = inColor;
}
```

`shaders/rod_flat.frag`:

```glsl
#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = fragColor;
}
```

- [ ] **Step 2 — Declare `ClockRenderer`.** Create `src/clock/ClockRenderer.hpp` (this is the only `clock/` file that touches Vulkan; it is an `app`-tier class, kept in the folder for cohesion but NOT linked into the pure `clock` lib):

```cpp
#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

#include "core/VulkanContext.hpp"
#include "renderer/ResourceManager.hpp"
#include "renderer/PassRecorder.hpp"
#include "clock/ClockMath.hpp"
#include "clock/RodField.hpp"

class ClockRenderer {
public:
    ClockRenderer(const VulkanContext& ctx, ResourceManager& resources,
                  VkFormat colorFormat, VkExtent2D extent);
    ~ClockRenderer();

    ClockRenderer(const ClockRenderer&) = delete;
    ClockRenderer& operator=(const ClockRenderer&) = delete;

    // Upload the rod field mesh to GPU buffers.
    void setRodField(const clock::RodField& field);

    // Record the flat vertex-color draw into the given color target.
    void record(PassRecorder& recorder, VkImageView colorView, const clock::Mat4& mvp);

private:
    const VulkanContext& m_ctx;
    ResourceManager& m_resources;
    VkExtent2D m_extent;

    VkPipelineLayout m_layout{VK_NULL_HANDLE};
    VkPipeline m_pipeline{VK_NULL_HANDLE};

    AllocatedBuffer m_vbo{};
    AllocatedBuffer m_ibo{};
    uint32_t m_indexCount{0};
};
```

- [ ] **Step 3 — Implement `ClockRenderer`.** Create `src/clock/ClockRenderer.cpp`. It builds the flat pipeline via the kept `PipelineBuilder` (`setBlendState` = opaque, `setShaders`, vertex input for `RodVertex`), uploads the mesh via `ResourceManager::uploadToBuffer`, and records the draw via `PassRecorder`:

```cpp
#include "clock/ClockRenderer.hpp"

#include "renderer/PipelineBuilder.hpp"
#include "renderer/ShaderLoader.hpp"

#include <stdexcept>

namespace {
VkPipelineColorBlendAttachmentState opaqueBlend() {
    VkPipelineColorBlendAttachmentState s{};
    s.blendEnable = VK_FALSE;
    s.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    return s;
}
}  // namespace

ClockRenderer::ClockRenderer(const VulkanContext& ctx, ResourceManager& resources,
                             VkFormat colorFormat, VkExtent2D extent)
    : m_ctx(ctx), m_resources(resources), m_extent(extent) {
    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pc.offset = 0;
    pc.size = sizeof(clock::Mat4);

    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &pc;
    if (vkCreatePipelineLayout(m_ctx.device(), &li, nullptr, &m_layout) != VK_SUCCESS)
        throw std::runtime_error("ClockRenderer: pipeline layout");

    VkShaderModule vert = ShaderLoader::loadModule(m_ctx.device(), "bin/shaders/rod_flat.vert.spv");
    VkShaderModule frag = ShaderLoader::loadModule(m_ctx.device(), "bin/shaders/rod_flat.frag.spv");

    VkVertexInputBindingDescription bind{};
    bind.binding = 0;
    bind.stride = sizeof(clock::RodVertex);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attrs = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(clock::RodVertex, pos)},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(clock::RodVertex, color)},
    };

    PipelineBuilder builder;
    m_pipeline = builder
        .setShaders(vert, frag)
        .setVertexInput({bind}, attrs)
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setCullMode(VK_CULL_MODE_NONE)
        .setPolygonMode(VK_POLYGON_MODE_FILL)
        .setDepthTest(false, false)
        .setBlendState(opaqueBlend())
        .setColorFormat(colorFormat)
        .setPipelineLayout(m_layout)
        .build(m_ctx.device());

    vkDestroyShaderModule(m_ctx.device(), vert, nullptr);
    vkDestroyShaderModule(m_ctx.device(), frag, nullptr);
}

ClockRenderer::~ClockRenderer() {
    if (m_vbo.buffer) m_resources.destroyBuffer(m_vbo);
    if (m_ibo.buffer) m_resources.destroyBuffer(m_ibo);
    if (m_pipeline) vkDestroyPipeline(m_ctx.device(), m_pipeline, nullptr);
    if (m_layout) vkDestroyPipelineLayout(m_ctx.device(), m_layout, nullptr);
}

void ClockRenderer::setRodField(const clock::RodField& field) {
    const clock::FlatMesh mesh = field.buildFlatMesh();
    m_indexCount = static_cast<uint32_t>(mesh.indices.size());

    const VkDeviceSize vSize = mesh.vertices.size() * sizeof(clock::RodVertex);
    const VkDeviceSize iSize = mesh.indices.size() * sizeof(uint32_t);

    m_vbo = m_resources.createBuffer(vSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    m_ibo = m_resources.createBuffer(iSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    m_resources.uploadToBuffer(m_vbo, mesh.vertices.data(), vSize);
    m_resources.uploadToBuffer(m_ibo, mesh.indices.data(), iSize);
}

void ClockRenderer::record(PassRecorder& recorder, VkImageView colorView, const clock::Mat4& mvp) {
    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    recorder.beginDebugLabel("ClockRenderer::rods");
    recorder.beginRendering(colorView, m_extent, &clear);
    recorder.bindPipeline(m_pipeline);
    recorder.setViewportScissor(m_extent);
    recorder.pushConstants(m_layout, VK_SHADER_STAGE_VERTEX_BIT, &mvp, sizeof(clock::Mat4));
    recorder.bindVertexBuffer(m_vbo.buffer);
    recorder.bindIndexBuffer(m_ibo.buffer);
    recorder.drawIndexed(m_indexCount);
    recorder.endRendering();
    recorder.endDebugLabel();
}
```

- [ ] **Step 4 — Add `ClockRenderer` to the app build + a headless dump harness target.** In `CMakeLists.txt`, append after the `clock` lib block a small headless executable that renders one frame to an offscreen image and writes RGBA8 (reusing `ResourceManager::downloadImage`), so the draw is testable without `main.cpp`:

```cmake
# clock_dump — headless: render the rod field flat to an offscreen image and
# write RGBA8 (pixel-diff harness; reuses ResourceManager::downloadImage).
add_executable(clock_dump
    tools/clockdump/main.cpp
    src/clock/ClockRenderer.cpp
    src/core/VulkanContext.cpp
    src/core/WindowContext.cpp
    src/renderer/ResourceManager.cpp
    src/renderer/PassRecorder.cpp
    src/renderer/PipelineBuilder.cpp
    src/renderer/ShaderLoader.cpp
)
add_dependencies(clock_dump Shaders)
target_include_directories(clock_dump PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(clock_dump PRIVATE clock SDL3::SDL3 vk-bootstrap VulkanMemoryAllocator glm::glm Vulkan::Vulkan)
target_compile_definitions(clock_dump PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE)
if (WIN32)
    target_compile_definitions(clock_dump PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif()
set_target_properties(clock_dump PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
```

Create `tools/clockdump/main.cpp` (offscreen one-frame render → readback → write binary; mirrors the kept `--dump-rgba` readback path: render into an `AllocatedImage` created with `TRANSFER_SRC`, then `downloadImage`):

```cpp
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "core/WindowContext.hpp"
#include "core/VulkanContext.hpp"
#include "renderer/ResourceManager.hpp"
#include "renderer/PassRecorder.hpp"
#include "clock/ClockRenderer.hpp"
#include "clock/Projection.hpp"
#include "clock/RodField.hpp"

int main(int argc, char** argv) {
    std::string outPath = "ours.rgba";
    for (int a = 1; a < argc; ++a) {
        std::string arg = argv[a];
        if (arg == "--dump-rgba" && a + 1 < argc) outPath = argv[++a];
    }

    const VkExtent2D extent{640, 224};

    WindowContext window("clock_dump", extent.width, extent.height);
    VulkanContext ctx(window);
    ResourceManager resources(ctx);

    const VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM;
    AllocatedImage target = resources.createImage(extent, fmt,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    ClockRenderer renderer(ctx, resources, fmt, extent);
    renderer.setRodField(clock::RodField::Generate());

    // MVP: fitted projection (rod0-locked) * identity model. Maps GS pixel space
    // (0..2048) to NDC for the 640x224 target via the projection's embedded center.
    clock::ProjectionParams pp;
    pp.fov = 1.047f; pp.near = 1.0f; pp.halfWidth = 512.0f;
    clock::Mat4 proj = clock::BuildProjectionMatrix(pp);
    // Normalize GS pixel output to NDC for the actual target resolution.
    clock::Mat4 toNdc(1.0f);
    toNdc[0][0] = 2.0f / 2048.0f; toNdc[3][0] = -1.0f;
    toNdc[1][1] = 2.0f / 2048.0f; toNdc[3][1] = -1.0f;
    clock::Mat4 mvp = toNdc * proj;

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.queueFamilyIndex = ctx.graphicsQueueFamily();
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool{};
    vkCreateCommandPool(ctx.device(), &pci, nullptr, &pool);

    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd{};
    vkAllocateCommandBuffers(ctx.device(), &ai, &cmd);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    {
        PassRecorder recorder(cmd);
        recorder.transitionImage(target.image, VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        renderer.record(recorder, target.imageView, mvp);
        recorder.transitionImage(target.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    }
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(ctx.graphicsQueue(), 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.graphicsQueue());

    const std::vector<uint8_t> px = resources.downloadImage(
        target, {0, 0}, extent, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    std::ofstream out(outPath, std::ios::binary);
    out.write(reinterpret_cast<const char*>(px.data()), static_cast<std::streamsize>(px.size()));
    out.close();
    std::printf("clock_dump: wrote %zu bytes (640x224 RGBA8) -> %s\n", px.size(), outPath.c_str());

    // Sanity: assert at least one non-black pixel was drawn (rods are visible).
    bool anyLit = false;
    for (size_t i = 0; i + 3 < px.size(); i += 4)
        if (px[i] || px[i + 1] || px[i + 2]) { anyLit = true; break; }
    if (!anyLit) { std::printf("clock_dump: FAIL — frame is all black\n"); return 1; }

    vkDestroyCommandPool(ctx.device(), pool, nullptr);
    resources.destroyImage(target);
    std::printf("clock_dump: OK\n");
    return 0;
}
```

> Note on `WindowContext`/`VulkanContext` ctor signatures: use the actual constructors in `src/core/`. If `VulkanContext` requires a surface from a real window and a hidden window is undesirable for CI, gate `clock_dump` behind an opt-in CMake flag (`option(CRYSTAL_BUILD_CLOCK_DUMP "..." ON)`) so the headless target is built only where a presentable surface is available. The four pure CTests in Tasks 1–4 are the hard gate; `clock_dump` is the visual sanity harness.

- [ ] **Step 5 — Configure + build; expect shaders + targets to build.**

```
cmake -B build
cmake --build build --target clock_dump
```

Expected: `bin/shaders/rod_flat.vert.spv` and `rod_flat.frag.spv` produced; `clock_dump.exe` links.

- [ ] **Step 6 — Run the dump harness; expect a non-black frame.**

```
./bin/clock_dump.exe --dump-rgba ours.rgba
```

Expected: `clock_dump: wrote 573440 bytes (640x224 RGBA8) -> ours.rgba` then `clock_dump: OK` (573440 = 640*224*4). RED state would be `FAIL — frame is all black` (indicates MVP maps rods off-screen — adjust the `toNdc` mapping / fitted params and re-run).

- [ ] **Step 7 — Pixel-diff sanity against a reference (when a reference frame is available).** The numeric gate uses the kept `tools/pixeldiff/pdiff.mjs`. Once a PCSX2 software-renderer reference of the flat rod layout exists (or, interim, a golden `ours.rgba` checked in as a regression baseline), run:

```
node tools/pixeldiff/pdiff.mjs "ours.rgba 640x224" "ref.rgba 640x224" heatmap.png
```

Expected: per-channel mean/max absolute error reported, heatmap written. For this flat (no-style) chunk the gate is loose — the positional oracle (Task 3) is the hard test; `pdiff` here confirms the rods land in roughly the captured screen region, not pixel-exact color.

- [ ] **Step 8 — Run the entire CTest suite to confirm no regressions across the project.**

```
ctest --test-dir build --output-on-failure
```

Expected: all existing tests (`gs_swizzle`, `gs_swizzle_addr`, `gs_texa`, `gs_texa_deswizzle`, `gsvk_blend`, `gsvk_recipe`) plus the four new `clock_*` tests Passed.

- [ ] **Step 9 — Commit.**

```
git add shaders/rod_flat.vert shaders/rod_flat.frag src/clock/ClockRenderer.hpp src/clock/ClockRenderer.cpp tools/clockdump/main.cpp CMakeLists.txt
git commit -m "Feat(App): Add flat rod render + headless dump-rgba sanity harness"
```

---

## Verification gate summary (this chunk is DONE when)

1. `ctest --test-dir build -R "clock_"` reports 4/4 Passed:
   - `clock_fixed124` — 12.4 encode/decode + oracle bits `0x77b3`/`0x8463`.
   - `clock_rotation` — rotation matrix orthonormal, det +1, NaN guard zeroes.
   - `clock_projection` — rod0 world `(-13.039, 14.666, 50.271)` projects to screen within ±3 px of `(1915.20, 2118.20)` and 12.4 within ±48 of `0x77b3`/`0x8463`.
   - `clock_rodfield` — group A = 8 rods, rod0 world matches trace, rods on a common ring, mesh non-empty.
2. `ctest --test-dir build` — no regression in the kept `gs_*`/`gsvk_*` tests.
3. `./bin/clock_dump.exe --dump-rgba ours.rgba` writes a 573440-byte RGBA8 frame and reports `OK` (non-black) — the rods render flat onto the kept renderer foundation.
4. All work committed under the convention `Feat(App): ...`, no `Co-Authored-By` trailer.

Open items deliberately deferred to the next chunk (do NOT attempt here): swapping `main.cpp`'s `GsRenderer` branch for `ClockRenderer` and dropping `gs`/`gsvk`/`GsScene`/`GsRenderer` from the app link; the real rod prism mesh (vs the placeholder quad); resolving the exact `fov`/`near`/`halfWidth` globals from a stable PCSX2 read (`vu0-math-pipeline.md` §Blocker 1) to tighten the projection tolerance below ±3 px; the per-rod radial parameterization and per-frame `angle`.

---

Relevant absolute paths used as ground truth while writing this plan:
- `C:\CodingProjects\Personal\CrystalClockVK\docs\ghidra_analysis\runtime-trace.md` (rod0 oracle: world `-13.039/14.666/50.271`, screen `1915.20/2118.20`, fixed `0x77b3/0x8463`)
- `C:\CodingProjects\Personal\CrystalClockVK\docs\ghidra_analysis\vu0-math-pipeline.md` (rotation cross-product basis; projection args far=2048/aspect=1/scale=65536; column-order/fov/near blockers)
- `C:\CodingProjects\Personal\CrystalClockVK\docs\ghidra_analysis\rod-pipeline.md` (group A = 8 front rods; ROD struct fields)
- `C:\CodingProjects\Personal\CrystalClockVK\CMakeLists.txt` (CTest loop pattern @ lines 196-204; shader glob block; lib-target pattern)
- `C:\CodingProjects\Personal\CrystalClockVK\src\renderer\{PipelineBuilder,ResourceManager,PassRecorder}.hpp` (kept API signatures used above)
- `C:\CodingProjects\Personal\CrystalClockVK\tests\SwizzleRoundTripTest.cpp` (the framework-free `check()`/`main()` test style replicated in all four new tests)