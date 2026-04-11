# Crystal Rod OSDSYS Clean-Room Analysis — Ghidra-MCP Deep Dive

## 1. The Master Render Function: `ui_render_3d_objects` @ `0x00223f78`

The Ghidra decompilation of this function reveals the **complete 5-pass pipeline** with two distinct code paths:

- `param_1 > 0.0` → **Animated/transitioning mode** (blending between two rod arrays)
- `param_1 <= 0.0` → **Static mode** (single rod array, standard clock display)

### Key Parameters
```
param_1 : float — animation blend factor (0.0 = static, >0 = transitioning)
param_3 : int*  — pointer to clock state struct
  param_3[0]   : base time value (cast to float, multiplied by in_f1)
  param_3[1]   : rod count
  param_3[0x1b]: visibility/alpha factor (must be >= 0.0 for rendering)
  param_3[0x28]: matrix transform base (passed to FUN_00232e38)
  param_3[0x2c]: Pass 3 X-angle offset (float as int)
  param_3[0x2d]: Pass 3 Y-angle offset (float as int)
```

---

## 2. Rod Data Structure (0x160 bytes per rod)

Each rod lives in a contiguous array starting at `0x375250` (first group) and `0x377e50` (second group), stride `0x160`:

| Offset | Type | Purpose |
|--------|------|---------|
| `+0x04` | float | Rotation angle A (written by `FUN_00232e38`) |
| `+0x08` | int | Zero'd before rotation build |
| `+0x10` | float | Screen X position |
| `+0x14` | float | Screen Y position |
| `+0x18` | float | Z/depth offset |
| `+0x30` | float[16] | 4×4 transform matrix base |
| `+0x60` | float | **Y-scale** (height scaling for time indicator) |
| `+0x90` | uint | Flags bitfield |
| `+0x94` | int | Rod ID / type identifier |
| `+0x98` | float | Computed base X position |
| `+0x9c` | float | Computed base Y position |
| `+0xa0` | float | Target X position (for animation interpolation) |
| `+0xa4` | float | Target Y position (for animation interpolation) |
| `+0xa8` | float | Transform result (written after `FUN_002738e8`) |
| `+0xac` | int | Screen ratio value (0x10=16:9, 0x0E=4:3) |
| `+0x150` | int | **Selection flag** (0 = normal, ≠0 = selected/active hour) |

> [!IMPORTANT]
> The rod array uses **TWO disjoint groups**:
> - Group A: `0x375250` with `in_stack_00000000._4_4_` count — filtered by `iVar15 > 7` (indices 8+)
> - Group B: `0x377e50` with `in_stack_000000e0._4_4_` count — filtered by `(iVar15 - 8U) > 1` (indices 2-7 skipped)
>
> This means the OSDSYS renders rods conditionally based on their index and group, NOT all 12 uniformly.

---

## 3. The 5-Pass Pipeline (Static Mode — `param_1 <= 0.0`)

### Pass 1: Base Transparent Glass
```c
FUN_002324e8(1, 0, 1);     // GS ALPHA: (Cs - Cd) * As + Cd → standard alpha blend
FUN_00230518(1, 1);         // Bind DAT_002973a0 primitive (transparent mesh)

// For each rod where flag == 0 (unselected):
FUN_00232da0(angleX, angleY, rodAddr, param_3, 0);
```

### Pass 2: Additive Specular Highlights
```c
FUN_00230fe8(2, 1, 2);     // GS ALPHA: additive blend (Cs + Cd)
FUN_00230518(2, 2);         // Bind DAT_002973c0 primitive (colored mesh)

// For each unselected rod:
angleStep = fGpffff832c;    // ← global angle step per rod
angle = baseAngle + rodIndex * angleStep;
FUN_00232e38(angle, angle, rodAddr, param_3 + 0x28);
// NOTE: Both rotation params are THE SAME angle
```

### Pass 3: Offset Rotation Specular
```c
FUN_00230518(0, 2);         // Same colored mesh, different texture mode

// For each unselected rod:
angleStep = fGpffff8330;    // ← DIFFERENT global angle step
angle = baseAngle + rodIndex * angleStep;
FUN_00232e38(angle + param_3[0x2c], angle + param_3[0x2d], rodAddr, param_3 + 0x28);
// NOTE: Two DIFFERENT angles! X gets +param_3[0x2c], Y gets +param_3[0x2d]
```

> [!IMPORTANT]
> **Pass 3 is the shimmer/double-image effect.** The old CrystalClock had this disabled with a comment saying "offset angles cause ghosting." The PS2 uses two distinct offset values from the clock state struct (`param_3[0x2c]` and `param_3[0x2d]`), NOT a per-rod multiplied offset.

### Pass 4: Selected Rod Highlight
```c
FUN_00232538(1, 0, 1);     // GS ALPHA: same as pass 1 but via different register path
FUN_00230518(1, 2);         // Bind transparent mesh

// For each rod where flag != 0 (SELECTED):
FUN_00232da0(angleX, angleY, rodAddr, param_3, 0);
```

### Pass 5: Selected Rod Reverse-Alpha Fill
```c
FUN_002324e8(0, 1, 1);     // GS ALPHA: (Cs - Cd) * As + Cs → REVERSE alpha
FUN_00230518(1, 2);         // Same transparent mesh

// For each SELECTED rod:
FUN_00232da0(angleX, angleY, rodAddr, param_3, 0xff);
// NOTE: Last param is 0xff (255) — full alpha override for fill
```

---

## 4. Rotation + Projection Matrix: `FUN_00232e38`

From the disassembly at `0x00232e38`:

```c
void draw_crystal_rod(float angleA, float angleB, int rodAddr, int* matrixBase) {
    // Store angleA into rod struct  
    *(float*)(rodAddr + 0x04) = angleA;
    *(int*)(rodAddr + 0x08) = 0;

    // 1. Build rotation matrix from two angles
    //    VU0 microcode at FUN_002732d8
    //    Input: angleA → s3[...], angleB → s2[...]
    //    Output: 4×4 rotation matrix at 0x29BD10
    FUN_002732d8(0x29BD10, 0x29BCF0);   // rotation_build(out_rot, temp)

    // 2. Build perspective projection
    //    VU0 microcode at FUN_002730a8
    float fov    = uGpffff8c28;          // global FOV value
    float aspect = 1.0f;                  // hardcoded 1:1
    float near   = uGpffff8488;          // global near plane
    float far    = 2048.0f;              // GS coordinate space max
    
    // widescreen check:
    float halfWidth = uGpffff8480;       // default
    if (iGpffff8d18 != 0) {
        halfWidth = uGpffff8484;         // widescreen override
    }
    
    FUN_002730a8(fov, 1.0, halfWidth, 2048.0, 2048.0, 1.0, near, 1.0, 0x29BD50);

    // 3. Multiply: final = projection × rotation
    FUN_002738a0(0x29BD90, 0x29BD50, 0x29BD10);
    //           (result,   proj,     rotation)
}
```

> [!WARNING]
> **The PS2 does NOT use a standard camera projection!** It builds a **custom per-rod projection matrix** with:
> - `far = 2048.0` (GS screen-space max, not world-space)
> - `aspect = 1.0` (square, then corrected by screen ratio at the end)
> - A widescreen-dependent half-width value
>
> The old CrystalClock uses Raylib's camera perspective and applies `MatrixRotateZ(hourAngle)`, `MatrixRotateX(ax)`, `MatrixRotateZ(az)` — this is a fundamentally different rotation chain.

---

## 5. Rod Scale Calculation: `FUN_00232da0` → `FUN_00242630`

The scale formula at `0x00242630` is significantly more complex than the old implementation:

```c
// Simplified clean-room reconstruction:
void compute_rod_scale(int rodIndex, float* rodData, bool isWidescreen) {
    float baseScale = f20;  // from caller (accumulator)
    float scale;
    
    // --- Step 1: Index-Based Scale Factor ---
    if (rodIndex >= 2 && rodIndex < 10) {
        // "Middle" rods (indices 2-9)
        int maxRods = isWidescreen ? 0x1F : 0x27;  // 31 or 39
        scale = baseScale * (float)(maxRods - rodIndex);
        scale = isWidescreen ? (scale / 10.0f) : (scale / 13.0f);
    } else {
        // "Edge" rods (indices 0-1 and 10+)
        int maxRods = isWidescreen ? 0x1A : 0x20;  // 26 or 32
        scale = baseScale * (float)(maxRods - rodIndex);
        scale = isWidescreen ? (scale / 5.0f) : (scale / 6.0f);
    }
    
    // --- Step 2: Screen Ratio Correction ---
    int screenRatio = *(int*)(rodData + 0xAC);
    if (!isWidescreen) {
        if (screenRatio != 0x10) {  // not 16:9
            scale *= (float)screenRatio * 0.0625f;  // ÷ 16.0
        }
    } else {
        if (screenRatio != 0x0E) {  // not 14:?
            scale *= (float)screenRatio / 14.0f;
        }
    }
    
    // --- Step 3: Hour Indicator Countdown ---
    if (rodData == activeRodPtr) {  // iGpffff8b3c
        if (hourIndicatorMode == 1) {  // iGpffff8b48
            int countdown = isWidescreen ? 0x21 : 0x28;  // 33 or 40
            float maxVal  = isWidescreen ? 33.0f : 40.0f;
            scale *= (float)(countdown - hourCounter) / maxVal;
            //                                         ^ iGpffff8b4c
        }
        // else: keep full scale
    }
    
    // Store final Y-scale
    *(float*)(rodData + 0x60) = scale;
    
    // --- Step 4: Apply matrix transform ---
    FUN_002738e8(output, param3 + 0x30, rodData + 0x10);
}
```

### vs Old CrystalClock
```cpp
// OLD (WRONG):
float LerpPrismScale(float t) {
    return 1.f - t / 3600.f;  // ← simple linear interpolation over 1 hour!
}
```

> [!CAUTION]
> **The old implementation uses a single global linear scale.** The real OSDSYS:
> 1. Calculates a **per-rod** scale based on the rod's index
> 2. Applies different divisors for widescreen vs standard
> 3. Has a **separate countdown multiplier** for the active hour rod (with a counter that ticks)
> 4. Applies a screen ratio correction

---

## 6. Per-Pass Angle Step Globals

The decompilation reveals **4 distinct global angle step values**, one per rendering pass:

| Global | Address | Used In | Purpose |
|--------|---------|---------|---------|
| `fGpffff832c` | Pass 2 (static) | `fVar18 + rodIndex * fGpffff832c` | Specular highlight rotation step |
| `fGpffff831c` | Pass 2 (anim) | Same formula | Specular step (animated mode) |
| `fGpffff8330` | Pass 3 (static) | `fVar18 + rodIndex * fGpffff8330` | Offset specular rotation step |
| `fGpffff8320` | Pass 2b (anim) | Same formula | Secondary specular step (anim) |
| `fGpffff8324` | Pass 3 (anim) | Same formula | Offset step (animated mode) |
| `fGpffff8328` | Pass 3b (anim) | Same formula | Offset step second group |

### vs Old CrystalClock
```cpp
// OLD:
const float ANGLE_STEP = 360.f / 60.f;  // ← fixed 6° per rod
```
The real OSDSYS uses **different angle steps per pass**, likely extracting them from the clock config. This creates the visual separation between the specular and offset passes.

---

## 7. Selection Flag Logic

The OSDSYS checks `*(int*)(rodAddr + 0x150)` to determine rod type:

```c
// Passes 1-3: render only if flag == 0 (unselected)
if (*(int*)(rodAddr + 0x150) == 0) {
    draw_rod(...);
}

// Passes 4-5: render only if flag != 0 (SELECTED — active hour)
if (*(int*)(rodAddr + 0x150) != 0) {
    draw_rod(...);
}
```

### vs Old CrystalClock
```cpp
// OLD: Hardcoded to rod index 0
// Pass 4-5 only draws rod 0
Matrix M = BuildRodMatrix(0, secOfMinRotation, hourOfDayRotation);
```
The real OSDSYS uses a **per-rod flag** to mark which rod(s) are active. Multiple rods could theoretically be flagged.

---

## 8. Pass 5 Alpha Override

In Pass 5, the last parameter to `FUN_00232da0` is `0xFF`:
```c
FUN_00232da0(angleX, angleY, rodAddr, param_3, 0xff);
```
This `0xFF` propagates into the scale computation and likely controls the alpha fill level for the "slider" effect.

In contrast, Pass 4 uses `0x00`:
```c
FUN_00232da0(angleX, angleY, rodAddr, param_3, 0);
```

---

## 9. The BuildRodMatrix Chain (Old CrystalClock — WRONG)

```cpp
// OLD Implementation:
Matrix BuildRodMatrix(int rodIndex, float minuteRot, float hourRot) {
    float angle = rodIndex * -30.0f;
    Matrix R = MatrixRotateY(minuteRot * 4.0f * DEG2RAD);   // ← arbitrary 4× multiplier
    Matrix M = MatrixIdentity();
    M = MatrixMultiply(M, MatrixRotateZ(angle * DEG2RAD));   // ← Z rotation for placement
    M = MatrixMultiply(M, MatrixRotateY(-minuteRot * DEG2RAD)); // ← negate
    M = MatrixMultiply(M, MatrixRotateZ(hourRot * DEG2RAD));    // ← hour rotation
    M = MatrixMultiply(R, M);                                    // ← pre-multiply
    return M;
}
```

### What the PS2 Actually Does:
```c
// OSDSYS FUN_00232e38:
// 1. Set angle into rod struct
*(float*)(rod + 4) = angleA;
*(int*)(rod + 8) = 0;

// 2. Build rotation from TWO angles (VU0 COP2):
rotation_build(angleA, angleB) → 4×4 matrix
// angleA and angleB ARE THE SAME in Pass 2
// angleA ≠ angleB in Pass 3 (offset by param_3[0x2c]/[0x2d])

// 3. Build custom projection (NOT camera projection):
projection_build(fov, 1.0, halfWidth, 2048.0, 2048.0, 1.0, near, 1.0)

// 4. Multiply: final = projection × rotation
```

**Critical differences:**
1. PS2 uses a **2-angle rotation** (VU0 builds a combined rotation from both), not Z→Y→Z Euler decomposition
2. PS2 has NO `4.0f * minuteRot` multiplier — the speed comes from the base time value (`(float)*param_3 * in_f1`)
3. PS2 embeds the **projection into the rod matrix** (combined transform), not separate camera projection
4. The angle formula per rod is: `baseAngle + rodIndex * angleStep` where `angleStep` is a **per-pass global**, not a fixed `360/60 = 6°`

---

## 10. Summary of Critical Differences

| # | Issue | OSDSYS (Real) | Old CrystalClock | Severity |
|---|-------|---------------|-------------------|----------|
| 1 | **Rotation chain** | VU0 2-angle rotation → custom projection | Z→Y→Z Euler + Raylib camera | 🔴 HIGH |
| 2 | **Rod scale** | Per-rod index-based with widescreen divisors | Global linear `1 - t/3600` | 🔴 HIGH |
| 3 | **Per-pass angle step** | 4+ distinct globals (`fGpffff832c`, `830`, etc.) | Single fixed `360/60` | 🔴 HIGH |
| 4 | **Selection flag** | Per-rod flag at `+0x150` | Hardcoded rod index 0 | 🟡 MED |
| 5 | **Pass 3 offsets** | `param_3[0x2c]` and `[0x2d]` (state-driven) | Disabled / arbitrary `rodIndex * PI/6 * 0.15` | 🟡 MED |
| 6 | **Rod groups** | Two groups at `0x375250` and `0x377e50`, filtered by index ranges | Single loop 0-11 | 🟡 MED |
| 7 | **Projection matrix** | Custom per-rod (far=2048, aspect=1, widescreen-aware) | Raylib camera perspective | 🟡 MED |
| 8 | **Pass 5 alpha** | `0xFF` parameter feeds into scale/alpha override | Fixed `0.8f` | 🟠 LOW |
| 9 | **Both angles same (P2)** | `FUN_00232e38(angle, angle, ...)` | Single rotation matrix | 🟠 LOW |
| 10 | **Hour countdown** | Counter-based `(countdown - counter) / max` | Linear time-based lerp | 🟡 MED |
| 11 | **Animated mode** | Full dual-array blend path with `param_1` factor | Not implemented | 🟡 MED |
| 12 | **Base time formula** | `(float)*param_3 * in_f1` (EE-driven time) | `LerpClockRotation(secondsInMinute)` | 🟡 MED |

---

## 11. Recommended Approach for CrystalClockVK

### Phase 1: Extract Global Constants
Use Ghidra-MCP to read the float values at:
- `fGpffff832c` — Pass 2 angle step
- `fGpffff8330` — Pass 3 angle step
- `fGpffff8c28` — FOV
- `fGpffff8488` — Near plane
- `fGpffff8480` / `fGpffff8484` — Half-width (standard vs widescreen)

### Phase 2: Reconstruct VU0 Math
The VU0 microcode functions (`FUN_002732d8`, `FUN_002730a8`) are encoded as COP1/COP2 opcodes that Ghidra cannot decompile. Options:
1. **PCSX2 trace** — Run OSDSYS under PCSX2 with VU logging, capture input/output matrices
2. **Manual decode** — The COP1 opcodes encode standard rotation and projection math; reconstruct from the calling convention (2 angles → rotation, fov/aspect/near/far → projection)
3. **Behavioral match** — Since we know the inputs (two angles + projection params), implement standard `RotationFromTwoAngles` and `PerspectiveProjection` and visually validate

### Phase 3: Implement `CrystalMath` in `gs/`
Build the clean-room math as pure functions (no Vulkan dependencies):
```cpp
// gs/CrystalMath.hpp
struct RodTransform {
    glm::mat4 rotation;
    glm::mat4 projection;
    glm::mat4 combined;    // projection × rotation
    float     yScale;
};

RodTransform BuildRodTransform(
    float angleA, float angleB,
    int rodIndex, bool isWidescreen,
    float fov, float nearPlane
);

float ComputeRodScale(
    int rodIndex, float baseScale,
    bool isWidescreen, int screenRatio,
    bool isSelected, int hourCounter
);
```

> [!NOTE]
> The VU0 rotation builder (`FUN_002732d8`) almost certainly implements a standard rotation from two angles (azimuth + elevation). The key insight is that both angles are the **same** value in Pass 2, and **offset** in Pass 3 — this creates the visual "double" specular shimmer. The `4× multiplier` in the old code was likely an empirical approximation of the actual PS2 time-to-angle formula.
