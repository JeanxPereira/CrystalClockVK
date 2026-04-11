# Crystal Rod Rendering — OSDSYS vs CrystalClock Deep Analysis

## Executive Summary

The PS2 does **NOT** use real-time shaders for crystal refraction. It uses a **framebuffer feedback loop**: the background is rendered first, then each rod samples the already-rendered framebuffer with UV distortion. The current CrystalClock implementation uses a Phong+normal map shader which is fundamentally wrong.

---

## 1. The 5-Pass Rendering Pipeline (from `ui_render_3d_objects` @ 0x00223f78)

The Ghidra decompilation reveals the exact 5-pass structure. Here's the corrected mapping:

### Pass Structure (Static Mode, `param_1 <= 0`)

| Pass | Blend Call | GS Primitive | Condition | Purpose |
|------|-----------|-------------|-----------|---------|
| **1** | `FUN_002324e8(1,0,1)` + `FUN_00230518(1,1)` | `DAT_002973a0` (transparent) | `flag == 0` (unselected) | **Base glass body** — framebuffer refraction |
| **2** | `FUN_00230fe8(2,1,2)` + `FUN_00230518(2,2)` | `DAT_002973c0` (colored) | `flag == 0` | **Specular highlights** — additive colored edges |
| **3** | `FUN_00230518(0,2)` | `DAT_002973c0` (colored) | `flag == 0` | **Offset rotation pass** — second specular with angular offset |
| **4** | `FUN_00232538(1,0,1)` + `FUN_00230518(1,2)` | `DAT_002973a0` (transparent) | `flag != 0` (selected) | **Selected rod glass** — same as pass 1 but for active rod |
| **5** | `FUN_002324e8(0,1,1)` + `FUN_00230518(1,2)` | `DAT_002973a0` (transparent) | `flag != 0` | **Selected rod fill** — solid overlay for hour indicator |

### GS ALPHA Register Mapping

From the blend setup calls, the GS ALPHA register `(A-B)*C+D` maps as:

| Call | A | B | C | D | OpenGL Equivalent |
|------|---|---|---|---|-------------------|
| `(1,0,1)` | Cs | Cd | As | Cd | `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)` — standard transparency |
| `(2,1,2)` | 0 | Cs | Cd | Cd | `glBlendFunc(GL_ONE, GL_ONE)` — **additive** |
| `(0,2)` | Cs | 0 | — | Cd | `glBlendFunc(GL_ONE, GL_ONE)` — additive (variant) |
| `(0,1,1)` | Cs | Cd | As | Cs | `glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA)` — **reverse alpha** |

> [!IMPORTANT]
> Pass 5 uses **reverse alpha blend** `(0,1,1)` — this is the "slider" effect where the selected rod's alpha is inverted. The hour-indicator rod shows as a solid fill that "fills up" from bottom, creating the countdown visual.

---

## 2. Refraction: Framebuffer-Based, NOT Shader-Based

### What the PS2 Actually Does (Patent US6,693,606 + Ghidra)

The PS2 has **no programmable fragment shaders**. The "glass refraction" is achieved through a **GS framebuffer feedback technique**:

1. **Render background** (tunnel/smoke) to the GS framebuffer
2. **For each rod**, the GS reads back the **already-rendered framebuffer pixels** at the rod's screen position
3. The UV coordinates are **offset by the rod's surface normal** (pre-computed on the EE as vertex attributes)
4. This creates a **distortion/refraction illusion** — you see the background "through" the glass, shifted by the crystal's curvature

From `FUN_00232e38` (draw_crystal_rod):
```c
// Builds rotation matrix from two angles
FUN_002732d8(0x29bd10, 0x29bcf0);  // Build rotation matrix

// Build projection matrix with near=1.0, far=2048.0
FUN_002730a8(fov, 1.0, aspect, 2048.0, 2048.0, 1.0, near, 1.0, out_matrix);

// Multiply: final = projection × rotation
FUN_002738a0(result, projection, rotation);
```

The rod geometry vertices are transformed through this matrix, and the **GS texture coordinates are set to sample the framebuffer** (TEX0 points to the current framebuffer base address).

### What CrystalClock Currently Does (WRONG)

```glsl
// crystal.fs — Pass 0 (current implementation)
vec2 distortedUV = screenUV + (norm.xy * 0.05);
vec3 bgSample = texture(bgTexture, vec2(distortedUV.x, 1.0 - distortedUV.y)).rgb;
vec3 tint = mix(bgSample, prismColor, 0.4);
finalColor = vec4(tint, rodAlpha);
```

**Problems:**
1. ✅ The *concept* is correct (sample background with distorted UVs)
2. ❌ `norm.xy * 0.05` — The distortion factor is arbitrary. PS2 uses the **actual vertex normal projected into screen space**, not the fragment normal map
3. ❌ `mix(bgSample, prismColor, 0.4)` — PS2 does NOT mix in a tint color in pass 1. The tint comes from passes 2-3 (additive specular)
4. ❌ The `bgTexture` is the tunnel layer rendered separately. On PS2, it's the **same framebuffer** — so rods can refract *other rods* behind them (order-dependent transparency)

### Correct Implementation

```glsl
// crystal.fs — Pass 0 (CORRECTED refraction)
// PS2 GS reads framebuffer at (screenUV + normalOffset)
// normalOffset comes from vertex normal projected to screen space
vec3 N = normalize(fragNormal);
vec3 viewDir = normalize(viewPos - fragPosition);

// Project normal to screen space for UV distortion (matches GS behavior)
// The distortion magnitude comes from the crystal's surface curvature
vec2 normalScreen = N.xy * 0.03; // Tuned to match PS2 visual
vec2 distortedUV = screenUV + normalScreen;

// Sample the SAME framebuffer (not a separate layer)
vec3 refracted = texture(bgTexture, distortedUV).rgb;

// Pass 1 output is JUST the refracted background, no tint mixing
finalColor = vec4(refracted, rodAlpha);
```

> [!WARNING]
> The current implementation uses a **separate bgTexture** for the tunnel layer. This means rods cannot refract each other. To match PS2, we need to render rods **back-to-front** and read from the **current framebuffer** (using `GL_READ_FRAMEBUFFER` or a copy-texture approach).

---

## 3. Rod Scale & Time Indicator (from `FUN_00232da0` @ 0x00232da0)

The Ghidra decompilation reveals a complex scale calculation:

```c
// Simplified pseudocode from FUN_00232da0
if (rodIndex >= 2 && rodIndex < 10) {
    maxRods = isWidescreen ? 31 : 39;  // 0x1f or 0x27
    scale = baseScale * (maxRods - rodIndex);
    scale = isWidescreen ? scale / 10.0 : scale / 13.0;
} else {
    maxRods = isWidescreen ? 26 : 32;  // 0x1a or 0x20
    scale = baseScale * (maxRods - rodIndex);
    scale = isWidescreen ? scale / 5.0 : scale / 6.0;
}

// Apply screen ratio correction
if (screenRatio == 16) {
    // skip
} else {
    scale *= (float)screenRatio * 0.0625; // /16.0
}

// Store final Y-scale for this rod
rodData[0x60] = scale;
```

### CrystalClock vs OSDSYS

| Aspect | CrystalClock | OSDSYS | Issue |
|--------|-------------|--------|-------|
| Scale formula | `Lerp(1.0, 0.1, secondsInHour/3600)` | Complex per-rod index-based calculation | **Wrong** — PS2 calculates per rod, not global |
| Widescreen handling | None | Different divisors (10 vs 13, 5 vs 6) | Missing |
| Screen ratio | Ignored | `ratio * 0.0625` correction | Missing |

---

## 4. Rotation Matrix Chain (from `FUN_00232e38`)

### OSDSYS (Ghidra):
```
1. Build rotation from (angle_a, angle_b) → FUN_002732d8
2. Build perspective projection → FUN_002730a8  
3. Multiply: result = projection × rotation → FUN_002738a0
```

Parameters to projection:
- FOV: `uGpffff8c28` (global, set per-screen mode)
- Aspect: `1.0`
- Near plane: `uGpffff8488` (global)
- Far plane: `2048.0` (the GS coordinate space max)

### CrystalClock:
```cpp
Matrix rx = MatrixRotateX(ax);
Matrix rz = MatrixRotateZ(az);
return MatrixMultiply(MatrixMultiply(rz, rx), MatrixRotateZ(hourAngle));
```

**Difference**: PS2 uses a **custom projection matrix** built per-rod, while CrystalClock uses Raylib's camera projection. The PS2 combines rotation and projection into a single matrix (`projection × rotation`), then transforms vertices directly.

---

## 5. Rotation Angle Per Rod (Pass 2)

From `ui_render_3d_objects` line 197 (decompiled):
```c
fVar17 = fVar18 + (float)iVar15 * fVar4 + 0.0;
FUN_00232e38(fVar17, fVar17, rodAddr, matrixPtr);
```

Where:
- `fVar18 = (float)*param_3 * in_f1` — base rotation from time
- `fVar4 = fGpffff832c` — angle step per rod (stored as global)
- Each rod gets: `baseAngle + rodIndex * angleStep`
- **Both rotation angles are the SAME** (`fVar17, fVar17`) in pass 2

In Pass 3 (offset rotation):
```c
FUN_00232e38(fVar17 + (float)param_3[0x2c], fVar17 + (float)param_3[0x2d], ...);
```
- Adds X-offset `param_3[0x2c]` and Y-offset `param_3[0x2d]` — these create the "double image" specular effect

---

## 6. Summary of Critical Differences

| # | Issue | Severity | Fix Required |
|---|-------|----------|-------------|
| 1 | **Refraction is framebuffer-based, not shader Phong** | 🔴 HIGH | Rewrite pass 0 to sample framebuffer with normal-based UV offset |
| 2 | **No per-rod scale calculation** | 🔴 HIGH | Implement `FUN_00232da0` formula with widescreen handling |
| 3 | **Trail segments = 120 instead of 50** | 🟡 MED | Change `TRAIL_SEGMENTS` constant |
| 4 | **Trail alpha uses time-based lerp, not index-based** | 🟡 MED | Use `max(0, 128 - (idx*50/(count-1))*3)` |
| 5 | **Pass 2-3 use mix(bg, color, 0.4) instead of pure additive** | 🟡 MED | Remove tint mixing from pass 0, make passes 2-3 purely additive |
| 6 | **Pass 3 missing angular offsets** | 🟡 MED | Add `param_3[0x2c]` and `[0x2d]` offsets |
| 7 | **Pass 5 missing reverse-alpha blend** | 🟡 MED | Implement `(0,1,1)` → `GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA` |
| 8 | **Rod visibility flag not implemented** | 🟡 MED | Add per-rod flag at `+0x150` offset |
| 9 | **Projection matrix differs** | 🟠 LOW | PS2 uses custom projection per-rod; current uses Raylib camera |
| 10 | **Billboard sizes wrong** | 🟠 LOW | Change from `2.5×/1.25×` to `scale*30/scale*4.5` |
