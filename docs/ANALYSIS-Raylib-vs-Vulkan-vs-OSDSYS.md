# CrystalClock: Raylib vs Vulkan vs OSDSYS — Full Comparative Analysis

**Date:** 2026-04-13
**Purpose:** Identify every delta between the Raylib implementation, the current Vulkan port, and the real OSDSYS binary (via Ghidra-MCP + VU0Decoder). Produce a gap-closure implementation plan.

---

## 1. Architecture Comparison

| Aspect | Raylib (`CrystalClock/`) | Vulkan (`CrystalClockVK/`) | OSDSYS (Real PS2) |
|--------|-------------------------|---------------------------|-------------------|
| **Renderer** | Raylib 4.x / OpenGL ES 3.0 | Vulkan 1.4 / Dynamic Rendering | GS (Graphics Synthesizer) direct VRAM |
| **Feedback Loop** | 3 separate `RenderTexture` FBOs (tunnelLayer, orbsLayer, clockLayer) | `subpassLoad()` via `VK_KHR_dynamic_rendering_local_read` — reads tile memory in-place | Direct VRAM read/write in same framebuffer |
| **Shader Language** | GLSL 100/330 via Raylib | SPIR-V (Vulkan) | GS hardware registers (no shader programs) |
| **Matrix API** | `raymath.h` (row-major, v*M convention) | GLM (column-major, M*v convention) | VU0 COP2 microcode (128-bit SIMD) |
| **Mesh** | Procedural hex prism via `GenCrystalRodMesh()` | Same hex prism ported to `CrystalGeometry` | Raw vertex strips in VRAM at `0x375250` / `0x377e50` |
| **Build** | CMake + FetchContent (Raylib) | CMake + FetchContent (SDL3, VMA, vk-bootstrap, GLM, ImGui) | EE C + VU0 asm, baked into OSDSYS.elf |
| **Platforms** | Windows, macOS, Android, Web (Emscripten) | Windows (RDNA2 baseline), macOS (MoltenVK) | PS2 only |

---

## 2. Rendering Pipeline — Pass-by-Pass Delta

### Source of Truth: `ui_render_3d_objects` @ `0x00223f78`

The OSDSYS has **two code paths**: animated (`param_1 > 0.0`) and static (`param_1 <= 0.0`). Both Raylib and Vulkan only implement static mode.

| Pass | OSDSYS (Static) | Raylib `DrawClock()` | VulkanVK `RenderOrchestrator` | Delta |
|------|-----------------|---------------------|------------------------------|-------|
| **1** Glass Refraction | `FUN_002324e8(1,0,1)` + `FUN_00230518(1,1)` → Cs*As + Cd*(1-As). Binds `DAT_002973a0` primitive (transparent). Iterates rods where `flag==0`. | `GL_SRC_ALPHA/ONE_MINUS_SRC_ALPHA`. Draws ALL 12 rods, no flag check. Fixed alpha=0.4. | `VK_BLEND_FACTOR_SRC_ALPHA/DST_ALPHA` + `subpassLoad()` for tile-local read. | **Raylib/VK draw all rods; OSDSYS skips selected rods. VK uses DST_ALPHA not ONE_MINUS_SRC_ALPHA.** |
| **2** Additive Specular | `FUN_00230fe8(2,1,2)` + `FUN_00230518(2,2)`. Binds `DAT_002973c0` (colored). Per-rod angle: `baseAngle + i * fGpffff832c`. **Both rotation angles identical.** | `GL_ONE/GL_ONE`. Same matrix for all. Fixed alpha=0.7. | `VK_BLEND_FACTOR_ONE/ONE`. Same structure. | **OSDSYS uses per-pass angle step global, not fixed 6deg. Both angles same is correct.** |
| **3** Offset Shimmer | `FUN_00230518(0,2)`. Per-rod: `angle + param_3[0x2c]`, `angle + param_3[0x2d]`. **Two DIFFERENT angles.** | **DISABLED** (commented out). Had `BuildRodMatrixOffset` with arbitrary `rodIndex * PI/6 * 0.15`. | Not implemented. | **Critical missing pass. OSDSYS uses clock-state-driven offsets, not per-rod linear.** |
| **4** Selected Highlight | `FUN_00232538(1,0,1)` + `FUN_00230518(1,2)`. Only rods with `flag!=0`. Same blend as P1. | `GL_SRC_ALPHA/ONE_MINUS_SRC_ALPHA`. Hardcoded rod index 0. | Same as Raylib port. | **OSDSYS uses per-rod flag; Raylib/VK hardcode rod 0.** |
| **5** Selected Fill | `FUN_002324e8(0,1,1)` + `FUN_00230518(1,2)`. Rods with `flag!=0`. Last param=`0xFF`. | `GL_ONE_MINUS_SRC_ALPHA/SRC_ALPHA`. Hardcoded rod 0. Fixed alpha=0.8. | Reverse alpha blend. Same hardcoded rod. | **0xFF alpha override not implemented. Per-rod flag missing.** |

### Rod Filtering Logic (OSDSYS)

The OSDSYS does NOT simply iterate 0-11:
- **Group A** (`0x375250`): Renders rods where `index > 7` AND `flag==0` (for P1-3) / `flag!=0` (for P4-5)
- **Group B** (`0x377e50`): Renders rods where `(index - 8) > 1` AND flag condition

This conditional per-group filtering is absent from both Raylib and Vulkan.

---

## 3. Rotation Matrix — Critical Math Delta

### OSDSYS (Real — via VU0Decoder)

`FUN_00232e38` builds a **3-step VU0 pipeline**:
```
rotation_build(0x29BD10, 0x29BCF0)  →  43 VU0 instructions (axis-angle via VOPMSUB cross product)
projection_build(0x29BD50, ...)     →  92 VU0 instructions (custom GS-native projection)
matrix_multiply(0x29BD90, proj, rot) → 18 VU0 instructions
```

**Key facts from VU0 decode:**
1. Rotation uses **two angles** (azimuth + elevation) built via accumulator chains and `VOPMSUB` (cross product)
2. The rotation is **axis-angle / Rodrigues**, NOT Euler Z→Y→Z decomposition
3. The projection has `far = 2048.0` (GS screen coordinates), `aspect = 1.0`, `scale = 65536.0` (GS fixed-point Q16.16)
4. Each rod gets its **own projection matrix** (not a shared camera)

### Raylib (Wrong)

```cpp
Matrix BuildRodMatrix(int rodIndex, float minuteRot, float hourRot) {
    float angle = rodIndex * -30.0f;
    Matrix R = MatrixRotateY(minuteRot * 4.0f * DEG2RAD);   // arbitrary 4x multiplier
    Matrix M = MatrixIdentity();
    M = MatrixMultiply(M, MatrixRotateZ(angle * DEG2RAD));   // Z rotation for ring placement
    M = MatrixMultiply(M, MatrixRotateY(-minuteRot * DEG2RAD));
    M = MatrixMultiply(M, MatrixRotateZ(hourRot * DEG2RAD));
    M = MatrixMultiply(R, M);
    return M;
}
```

**Differences:**
- Uses Z→Y→Z Euler decomposition, not axis-angle
- `4.0f * minuteRot` multiplier is an empirical guess (no PS2 basis)
- Uses Raylib camera perspective, not per-rod GS projection
- Hour rotation in degrees, not derived from GS time format

### Vulkan (Ported from Raylib — Still Wrong)

```cpp
// CrystalMath.hpp::buildRodMatrix — direct port of Raylib logic
glm::mat4 M = glm::mat4(1.0f);
M = glm::rotate(M, hourRotDeg * DEG2RAD, glm::vec3(0,0,1));
M = glm::rotate(M, -minuteRotDeg * DEG2RAD, glm::vec3(0,1,0));
M = glm::rotate(M, rodAngleDeg * DEG2RAD, glm::vec3(0,0,1));
M = M * R;
```

Same fundamental errors as Raylib, just re-expressed in GLM column-major.

---

## 4. Rod Scale Calculation — Delta

### OSDSYS (`FUN_00232da0` → `0x00242630`)

```
Per-Rod Scale:
  if (index in [2,9]):
    scale = baseScale * (maxRods - index) / divisor
    maxRods = widescreen ? 31 : 39
    divisor = widescreen ? 10.0 : 13.0
  else (edge rods):
    scale = baseScale * (maxRods - index) / divisor
    maxRods = widescreen ? 26 : 32
    divisor = widescreen ? 5.0 : 6.0

  Screen ratio correction:
    if (!widescreen && ratio != 0x10): scale *= ratio / 16.0
    if (widescreen && ratio != 0x0E):  scale *= ratio / 14.0

  Hour countdown (selected rod only):
    countdown = widescreen ? 33 : 40
    scale *= (countdown - hourCounter) / maxVal
```

### Raylib (Wrong)
```cpp
float LerpPrismScale(float t) { return 1.f - t / 3600.f; }
```
Single global linear interpolation. No per-rod computation, no widescreen awareness, no screen ratio correction.

### Vulkan (Partially Correct)
`CrystalMath::computeRodScale()` has the per-rod index-based scale with widescreen divisors, but:
- Screen ratio correction is missing
- Hour countdown uses `hourCounter / 3600.0 * maxVal` approximation instead of the OSDSYS integer counter
- Not actually called in the render loop (the old `lerpPrismScale` is still used)

---

## 5. Per-Pass Angle Step — Delta

### OSDSYS
Uses **6 distinct global float values** for angle stepping:

| Global | Pass | Mode |
|--------|------|------|
| `fGpffff832c` | Pass 2 | Static |
| `fGpffff8330` | Pass 3 | Static |
| `fGpffff831c` | Pass 2 | Animated |
| `fGpffff8320` | Pass 2b | Animated |
| `fGpffff8324` | Pass 3 | Animated |
| `fGpffff8328` | Pass 3b | Animated |

Angle per rod: `baseAngle + rodIndex * angleStep`

### Raylib / Vulkan (Wrong)
```cpp
const float ANGLE_STEP = 360.f / 60.f;  // fixed 6° per rod
```
Single fixed step for all passes. Need to extract the real float values from OSDSYS memory.

---

## 6. Color Cycling — Delta

### OSDSYS
Uses two different color primitives:
- `DAT_002973a0` — transparent mesh color (Pass 1, 4, 5)
- `DAT_002973c0` — colored/lit mesh color (Pass 2, 3)

These are loaded from GS register data. The exact RGBA values need extraction.

### Raylib / Vulkan
```cpp
const Vector3 PRISM_COLORS[] = {
    { 0.04f, 0.23f, 0.46f },  // Deep Blue
    { 0.17f, 0.03f, 0.45f },  // Violet
    { 0.03f, 0.39f, 0.45f }   // Teal
};
// Lerp every 10 seconds
```
Color cycling is approximated. The actual GS primitive colors need verification.

---

## 7. Missing Features

| Feature | OSDSYS | Raylib | Vulkan | Priority |
|---------|--------|--------|--------|----------|
| **Animated mode** (`param_1 > 0`) | Full dual-array blend with interpolation | Not implemented | Not implemented | Medium |
| **Pass 3 shimmer** | Two distinct offset angles from clock state | Disabled/commented out | Not implemented | **High** |
| **Per-rod selection flag** | `rod + 0x150` bitfield | Hardcoded rod 0 | Hardcoded rod 0 | **High** |
| **Rod group filtering** | Two groups A/B with index-range filters | Single loop 0-11 | Single loop 0-11 | Medium |
| **Widescreen rod extension** | Group B at `0x377e50` adds edge rods | Not implemented | Not implemented | Low |
| **VU0 axis-angle rotation** | Cross-product-based rotation builder | Z→Y→Z Euler | Same Euler (ported) | **Critical** |
| **GS-native projection** | Per-rod custom projection (far=2048, scale=65536) | Raylib camera perspective | GLM perspective | **Critical** |
| **Per-pass angle step** | 6 distinct globals | Fixed 6° | Fixed 6° | **High** |
| **Screen ratio correction** | Per-rod scale × ratio/16 or ratio/14 | None | None | Medium |
| **Pass 5 alpha override** | `0xFF` parameter feeds scale/alpha | Fixed 0.8f | Fixed value | Low |
| **Orbs/Trails** | Part of full OSDSYS scene | Implemented (Raylib) | Not yet ported | Medium |
| **Tunnel/Background** | GS smoke tunnel in VRAM | Implemented (Raylib) | Shader exists | Low |
| **FXAA** | N/A (GS hardware) | Implemented | Not yet | Low |

---

## 8. VU0 Decoded Instruction Summary

### `rotation_build` (FUN_002732d8) — 43 instructions
Key pattern:
```
Phase 1 (.x):  VMSUBQ → VMADD → VMUL → VMADDQ (sin/cos application)
Phase 2 (.yzw): VMADDI → VMULQ → VMADDw → VOPMSUB ← CROSS PRODUCT
Phase 3-8:      Column-by-column refinement via accumulator chains
```
Confirms: **Azimuth/Elevation rotation via Rodrigues formula + orthogonalization**

### `projection_build` (FUN_002730a8) — 92 instructions
Key pattern:
```
VITOF4 → VMADDw → VSUB → VMADD → VCLIP → VMUL → VADD chains
```
Confirms: **Custom perspective projection embedding GS coordinate transform (0-2048 range)**

### `matrix_multiply` (FUN_002738a0) — 288 instructions (lookup table + multiply)
Standard 4×4 matrix multiply using VU0 SIMD. The 288 count includes a sin/cos lookup table embedded in the function.

---

## 9. Implementation Plan — Gap Closure

### Phase 1: Extract OSDSYS Constants (Ghidra-MCP)
**Goal:** Get the exact float values for all globals.

| Task | Target |
|------|--------|
| 1.1 | Read `fGpffff832c`, `fGpffff8330`, `fGpffff831c`, `fGpffff8320`, `fGpffff8324`, `fGpffff8328` — angle step floats |
| 1.2 | Read `fGpffff8c28` (FOV), `fGpffff8488` (near), `fGpffff8480` / `fGpffff8484` (half-width standard/widescreen) |
| 1.3 | Read `DAT_002973a0` (16 bytes — transparent primitive RGBA) and `DAT_002973c0` (16 bytes — colored primitive RGBA) |
| 1.4 | Read `fGpffff8318` (initial scale factor used in `fStack000001c0/c4`) |
| 1.5 | Determine `iGpffff8b3c` (active rod pointer), `iGpffff8b48` (hour indicator mode), `iGpffff8b4c` (hour counter) initialization |

### Phase 2: Reconstruct VU0 Math in `gs/CrystalMath`
**Goal:** Replace Euler rotation with OSDSYS-accurate axis-angle math.

| Task | Description | File |
|------|-------------|------|
| 2.1 | Implement `BuildRotation(angleA, angleB)` using Rodrigues formula (matching VOPMSUB cross-product pattern) | `src/gs/CrystalMath.hpp` |
| 2.2 | Implement `BuildProjection(fov, halfWidth, near)` with GS-native parameters (far=2048, aspect=1, scale=65536) | `src/gs/CrystalMath.hpp` |
| 2.3 | Implement `CombinedTransform = projection × rotation` | `src/gs/CrystalMath.hpp` |
| 2.4 | Validate via PCSX2 VU trace: feed known angles, compare output matrices | Manual verification |
| 2.5 | Remove old `buildRodMatrix()` Euler chain from `app/CrystalMath.hpp` | `src/app/CrystalMath.hpp` |

**Proposed rotation reconstruction:**
```cpp
glm::mat4 BuildRotation(float angleA, float angleB) {
    float sinA = sin(angleA), cosA = cos(angleA);
    float sinB = sin(angleB), cosB = cos(angleB);

    glm::vec3 forward(sinA * cosB, sinB, cosA * cosB);
    glm::vec3 up(-sinA * sinB, cosB, -cosA * sinB);
    glm::vec3 right = glm::cross(forward, up);  // the VOPMSUB

    return glm::mat4(
        glm::vec4(right,   0.0f),
        glm::vec4(up,      0.0f),
        glm::vec4(forward, 0.0f),
        glm::vec4(0, 0, 0, 1.0f)
    );
}
```

### Phase 3: Fix 5-Pass Pipeline in `RenderOrchestrator`
**Goal:** Match OSDSYS pass structure exactly.

| Task | Description |
|------|-------------|
| 3.1 | Add rod selection flag (`uint32_t flag` at conceptual offset `+0x150`) to rod data struct |
| 3.2 | Pass 1/2/3: Skip rods where `flag != 0` (selected). Pass 4/5: Skip where `flag == 0` |
| 3.3 | Pass 2: Use extracted `fGpffff832c` angle step. Both rotation angles identical: `BuildRotation(angle, angle)` |
| 3.4 | **Implement Pass 3**: Use `fGpffff8330` angle step + clock-state offsets for the two angles: `BuildRotation(angle + offsetX, angle + offsetY)` |
| 3.5 | Pass 5: Pass `0xFF` alpha override to scale computation |
| 3.6 | Fix Pass 1 blend: use `SRC_ALPHA / ONE_MINUS_SRC_ALPHA` (not `DST_ALPHA`) |

### Phase 4: Fix Rod Scale
**Goal:** Match OSDSYS per-rod scale exactly.

| Task | Description |
|------|-------------|
| 4.1 | Wire `computeRodScale()` into the actual render loop (currently unused) |
| 4.2 | Add screen ratio correction (`ratio / 16.0` or `ratio / 14.0`) |
| 4.3 | Fix hour countdown to use integer counter ticking, not linear time interpolation |
| 4.4 | Remove old `lerpPrismScale()` usage |

### Phase 5: Implement Per-Pass Angle Steps
**Goal:** Replace fixed 6deg step with OSDSYS globals.

| Task | Description |
|------|-------------|
| 5.1 | Store extracted angle step values as constants in `gs/GsConstants.hpp` |
| 5.2 | Use correct step per pass: P2 → `ANGLE_STEP_P2`, P3 → `ANGLE_STEP_P3` |
| 5.3 | Angle formula: `baseAngle + rodIndex * passAngleStep` (not `rodIndex * 30deg`) |

### Phase 6: Port Missing Features
**Goal:** Feature parity with Raylib + OSDSYS additions.

| Task | Description | Priority |
|------|-------------|----------|
| 6.1 | Port Orbs/Trails from Raylib to Vulkan (billboard rendering + trail mesh) | Medium |
| 6.2 | Implement animated mode (`param_1 > 0`) with dual rod array blending | Low |
| 6.3 | Add widescreen rod extension (Group B logic) | Low |
| 6.4 | FXAA post-process shader | Low |
| 6.5 | Extract and use real GS primitive colors (`DAT_002973a0`, `DAT_002973c0`) | Medium |

### Phase 7: Visual Validation
**Goal:** Pixel-accurate comparison with PS2 output.

| Task | Description |
|------|-------------|
| 7.1 | Capture PS2 reference frames at known time values via PCSX2 |
| 7.2 | Capture VulkanVK frames at matching time values |
| 7.3 | Overlay comparison — validate rotation angles, scale, color, blend |
| 7.4 | Adjust any remaining constants based on visual delta |

---

## 10. File Change Map

| File | Action | Scope |
|------|--------|-------|
| `src/gs/CrystalMath.hpp` | **NEW** | VU0-accurate rotation + projection (pure math, no Vulkan) |
| `src/gs/GsConstants.hpp` | **NEW** | Extracted OSDSYS float constants (angle steps, FOV, etc.) |
| `src/app/CrystalMath.hpp` | **REWRITE** | Remove Euler rotation chain; delegate to `gs/CrystalMath` |
| `src/app/RenderOrchestrator.cpp` | **MODIFY** | Add rod flag filtering, Pass 3, per-pass angle steps, fix blending |
| `src/app/RenderOrchestrator.hpp` | **MODIFY** | Add rod data struct with flag field |
| `shaders/Crystal.frag` | **MODIFY** | Adjust alpha handling for Pass 5 `0xFF` override |
| `shaders/CrystalSpecular.frag` | **MODIFY** | Validate additive blend output matches GS |

---

## 11. Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| VU0 rotation reconstruction incorrect | Visually wrong rod orientation | Validate against PCSX2 VU trace with known inputs |
| GS projection constants unknown | Scale/perspective mismatch | Extract floats from Ghidra-MCP memory reads |
| `subpassLoad()` precision differs from GS VRAM | Refraction visual delta | Tune chromatic displacement parameters |
| Per-pass angle steps are runtime-computed (not static) | Constants may vary | Check if globals are written during init or per-frame |
| Animated mode path untested | Missing transition effects | Lower priority — static mode first |

---

## 12. Execution Order (Critical Path)

```
Phase 1 (Constants)  ─── must complete before ───→  Phase 2 (VU0 Math)
                                                       │
Phase 3 (Pass Pipeline) ←── depends on ───────────────┘
    │
    ├── Phase 4 (Rod Scale) ── parallel ──→ Phase 5 (Angle Steps)
    │
    └── Phase 6 (Features) ── after P3-P5 stable
                                      │
                                      └──→ Phase 7 (Validation)
```

**Estimated critical blockers:**
1. Extracting OSDSYS float constants (Phase 1) — gates everything
2. VU0 rotation reconstruction (Phase 2) — highest visual impact
3. Pass 3 implementation (Phase 3.4) — the shimmer is the signature effect
