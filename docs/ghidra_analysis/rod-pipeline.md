# Rod Render Pipeline — OSDSYS Crystal Clock

> RE target: `OSDSYS.elf` (Ghidra base `0x001f0000`).
> All addresses are Ghidra entry addresses. Runtime addresses are embedded in
> `module_clock_XXXXXX` names (skew = Ghidra − runtime). Every claim cites
> `name @ address` or cites raw bytes / pcode evidence.
>
> **Ghidra decompiler caveat:** Many small functions at the listed addresses are
> J-thunks (8–16 bytes, just `j <real_target>`). The decompiler shows wrong code
> for them by falling into the next function body. For those, the raw-byte decode
> and pcode are cited instead; the thunk targets are the true function bodies.

---

## 1. Multi-pass flow (Mermaid)

```mermaid
flowchart TD
    ENTRY["ui_render_3d_objects @ 0x00223f78\n(float transition, undefined4 p2, int* clockState)"]

    ENTRY --> STEADY{"transition == 0?\n(steady-state branch)"}

    STEADY -- "yes (steady)" --> SS_PASS1
    STEADY -- "no (transition)" --> TR_SETUP

    %% --- Transition variant setup ---
    TR_SETUP["FUN_002335e8 × 2\nper-group transform init\n(groups A @ 0x375250, B @ 0x377e50)"]
    TR_SETUP --> TR_MATSTACK["FUN_002367c0 (get matrix stack top)\nFUN_00236a80 (push translate k=state[0x1b]*26*t)"]
    TR_MATSTACK --> SS_PASS1

    %% === PASS 1 — glow / after-image (src-over) ===
    subgraph PASS1 ["Pass 1 — After-image / glow (blend = src-over)"]
        SS_PASS1["FUN_002324e8(1,0,1)  → set blend mode A\nFUN_00230518(1,1)   → set test/texture mode\nFUN_0022f720(0x375230) → begin GS packet"]
        SS_PASS1 --> P1_HDR["write DAT_002973a0/a8/ac header\n(src-over ALPHA register value)\ninto puRam00375230"]
        P1_HDR --> P1_LOOP["for each rod with +0x150 == 0 (front)\ni > 7 (group A skip), i-8 > 1 (group B skip)\n  FUN_00232da0(x, y, rod) → compute glow alpha + write rod+0x60"]
        P1_LOOP --> P1_KICK["FUN_00235350() → kick / flush packet"]
    end

    %% === PASS 1b — back-face glow ===
    subgraph PASS1B ["Pass 1b — Back-face after-image"]
        P1B["FUN_00232538(1,0,1) → set blend mode B (sub-variant)\nFUN_00230518(1,2)\nFUN_0022f720(0x375230)"]
        P1B --> P1B_LOOP["loop: rod+0x150 != 0 (back face)\n  FUN_00232da0(x,y,rod,...,0) — back glow"]
        P1B_LOOP --> P1B_KICK["FUN_00235350()"]
    end

    %% === PASS 1c — back-face glow variant 2 ===
    subgraph PASS1C ["Pass 1c — Back-face alpha=0xff variant"]
        P1C["FUN_002324e8(0,1,1) → blend mode A (reset)\nFUN_00230518(1,2)\nFUN_0022f720(0x375230)"]
        P1C --> P1C_LOOP["loop: rod+0x150 != 0\n  FUN_00232da0(x,y,rod,...,0xff)"]
        P1C_LOOP --> P1C_KICK["FUN_00235350()"]
    end

    %% === PASS 2 — prism rods (additive) ===
    subgraph PASS2 ["Pass 2 — Crystal prism rods (additive blend)"]
        P2["FUN_00230fe8(2,1,2) → set blend mode C (additive)\nFUN_00230518(2,2)\nper-rod:"]
        P2 --> P2_INNER["FUN_0022f720(0x375230)\nwrite DAT_002973c0/c4/c8/cc header\n(additive ALPHA register value)\nangle = fVar18 + i*fGpffff832c\nFUN_00232e38(angle,angle,rod,state+0x28)\n  → matrix pipeline: rotation_build + projection_build + matrix_multiply\nFUN_00235350()"]
    end

    %% === PASS 3 — prism rods (subtractive) ===
    subgraph PASS3 ["Pass 3 — Refraction / subtractive pass"]
        P3["FUN_00230518(0,2) → blend mode off / Z-update\nper-rod:\nFUN_0022f720(0x375230)\nwrite DAT_002973c0 header\nangle += i*fGpffff8330\nFUN_00232e38(angle+state[0x2c], angle+state[0x2d], rod, state+0x28)\nFUN_00235350()"]
    end

    PASS1 --> PASS1B
    PASS1B --> PASS1C
    PASS1C --> PASS2
    PASS2 --> PASS3
```

---

## 2. Function index

All Ghidra entry addresses. Many small functions are J-thunks; the real body
address is noted where confirmed from raw bytes.

| Function | Ghidra addr | Body size | Role |
|----------|-------------|-----------|------|
| `ui_render_3d_objects` | `0x00223f78` | large | Top-level rod renderer — 3-pass structure |
| `FUN_002324e8` | `0x002324e8` | ~0x50 bytes | **Set blend mode A** (src-over variant). Calls `FUN_00241cc0(6)`, computes scissor from `s1+0xc54/c50` (screen dims), calls into GS packet writer @ `0x00248920`. Args `(1,0,1)` = ABE on, A=Cs, B=Cd, C=As, D=Cd (src-over). |
| `FUN_00232538` | `0x00232538` | ~0x60 bytes | **Set blend mode B** (sub/additive variant). Same shape as `FUN_002324e8` but args differ. Starts at offset +0x50 inside same code block — the two functions share `WaitSema`-like GS-write tail. |
| `FUN_00230518` | `0x00230518` | 8 bytes | J-thunk → `0x002401c8`. Real function: **set texture / GIF-tag block**. Unpacks w/h from 16-bit packed word, computes byte size, writes GIF_TAG `0x01860200` (A+D, 6 regs) into a 7-dword struct, calls `FUN_0026753c` (VIF1 kick). |
| `FUN_00230fe8` | `0x00230fe8` | 8 bytes | J-thunk → real body unknown (function body 00230518-0023051f per Ghidra; pcode identifies target logic). **Set blend mode C** (additive/subtractive). Takes index arg (2) to select one of 3 GS blend presets. |
| `FUN_0022f720` | `0x0022f720` | ~0xD8 bytes | **Begin/reset GS packet** at address in arg (0x375230). Resets `puRam00375230` and `puRam00375244` to arg; stores context flags into adjacent RAM globals `uRam002c8a3c/40/2c/30`. Called before every per-rod packet build. |
| `FUN_00235350` | `0x00235350` | 16 bytes | **Kick / flush GS packet**. Calls `0x00239a28` (VIF1 DMA kick?) and `0x0023b5ec`. Sets status flags `uGpffff8c76=1`, `iGpffff8cb4=0`, etc. — the GIF-path end-of-packet sync. |
| `FUN_00232da0` | `0x00232da0` | 8 bytes | J-thunk → `0x00242ac8`. Real function: **compute per-rod glow alpha** and write to `rod+0x60`. Decay depends on position index, group (front/back), and whether it's the selected/active rod. Also calls `FUN_002738e8` (sceVu0MulVector) on rod+0x10 and rod+0x30. Writes `rod+0xa8`. |
| `FUN_00232e38` | `0x00232e38` | large | **draw_crystal_rod** — confirmed anchor. Stores angle into `rod+4`, calls `rotation_build @ 0x002732d8`, `projection_build @ 0x002730a8`, `matrix_multiply @ 0x002738a0` (proj × rot). See `vu0_decode.md`. |
| `FUN_002335e8` | `0x002335e8` | ~12 bytes | **Per-group transform init** — writes 8 bytes from `param_3` into `rodGroup+0x130`, clears `uRam002c8b3c=0`, clears `rodGroup+0x140=0`. Confirmed from clean decompile: `*(undefined8*)(param_5+0x130) = param_3; uRam002c8b3c = 0; *(undefined4*)(param_5+0x140) = 0`. Called with `(stack1, stack2, stack3, 0x375250, clockState)` and `(…, 0x377e50, &stack_copy)`. |
| `FUN_002367c0` | `0x002367c0` | 16 bytes | **Matrix stack top getter**. Raw: `addiu $sp,-16; nop; jr $ra; lbu $v0, 0x8CC4($gp)`. Returns `gp+0x8CC4` (1-byte matrix stack depth index). |
| `FUN_00236a80` | `0x00236a80` | medium | **Matrix stack push + translate**. Raw: saves $ra, loads `gp+0x8CC8` (int stack depth), compares with 1. Called as `FUN_00236a80(0, state[0x1b]*26.0*transition, 0)` — the Y-translate for the transition animation. |
| `rotation_build` | `0x002732d8` | large | Build 4×4 rotation from two angles (azimuth+elevation), output to `0x29BD10`. Full VU0 decode in `vu0_decode.md`. |
| `projection_build` | `0x002730a8` | large | Build GS-native projection matrix (far=2048, scale=65536), output to `0x29BD50`. |
| `matrix_multiply` | `0x002738a0` | large | proj × rot → combined matrix at `0x29BD90`. |

---

## 3. Per-pass GS blend / test mapping

The three GS blend modes from ground truth (3936 draws, 3 blend modes) map to
passes as follows, cross-referencing `pktSetAlphaBlend.s` and `pktSetTEST_1.s`
from `CrystalOSD/asm/graph/`.

### 3.1 ALPHA register encoding (GS register 0x42)

```
GS ALPHA = (A-B)*C/128+D  with A,B,C,D ∈ {0=Cs, 1=Cd, 2=0}; C=fixed or As(3)
```

The two template blocks in `ui_render_3d_objects`:

| Template block | DAT addresses | Used in passes | Blend equation |
|----------------|--------------|----------------|----------------|
| **Block A** | `DAT_002973a0, a8, ac` | After-image / glow (passes 1, 1b, 1c) | Hypothesis: src-over — `(Cs-Cd)*As/128+Cd` i.e. A=0,B=1,C=As,D=1 |
| **Block B** | `DAT_002973c0, c4, c8, cc` | Prism rod (passes 2, 3) | Hypothesis: additive — `(Cs-0)*As/128+Cd` i.e. A=0,B=2,C=As,D=1 |

> These are runtime-initialized RAM locations (zero in static ELF). The actual
> bit patterns must be captured at runtime via PCSX2-MCP memory read
> (`pcsx2_read_memory 0x2973a0 16` / `0x2973c0 16`) to get the encoded ALPHA
> values. The `pktSetAlphaBlend.s` call sequence:
> `pktSetAD(packet, 0x49, value_COLCLAMP)` then `pktSetAD(packet, 0x42, value_ALPHA)`
> — register 0x49 = COLCLAMP, 0x42 = ALPHA.

### 3.2 Call sequence → blend mode

| Call sequence in `ui_render_3d_objects` | Blend mode (hypothesis) |
|-----------------------------------------|------------------------|
| `FUN_002324e8(1,0,1)` | src-over (ABE on; A=Cs,B=Cd,C=As,D=Cd) |
| `FUN_00230fe8(2,1,2)` | additive (ABE on; A=Cs,B=0,C=As,D=Cd) |
| `FUN_00232538(1,0,1)` | src-over back-face variant |
| `FUN_002324e8(0,1,1)` | alpha=0 override (ABE off or alpha=0) |
| `FUN_00230518(1,1)` | TEST / texture mode 1 — ZTST pass-all or depth-write on |
| `FUN_00230518(1,2)` | TEST / texture mode 2 — depth write off |
| `FUN_00230518(2,2)` | TEST / texture mode, subtractive arm |
| `FUN_00230518(0,2)` | TEST reset (depth write back on) |

The GS counts from the ground truth:
- src-over (2108 draws) → passes 1 + 1b + 1c (glow ring)
- additive (652 draws) → pass 2 (prism crystal surface)
- subtractive (1176 draws) → pass 3 (refraction dark pass)

---

## 4. ROD struct field map (stride 0x160 bytes)

Base addresses: Group A = `0x375250`, Group B = `0x377e50`.
Each rod occupies exactly `0x160` bytes.

| Offset | Type | Name | Evidence |
|--------|------|------|----------|
| `+0x00` | `undefined` | base | array element start |
| `+0x04` | `float` | `angle` | `ui_render_3d_objects`: `*(rod+4) = in_f0` in `draw_crystal_rod` (FUN_00232e38 @ `0x00232e38`, line `*(rod+4) = in_f0`) |
| `+0x08` | `int` | `field_0x08` | zeroed in `draw_crystal_rod`: `*(rod+8) = 0` |
| `+0x10` | `float[3]` | `pos` (XYZ?) | `FUN_00232da0` thunk-target calls `sceVu0MulVector(rod+0x10, ...)` |
| `+0x30` | `float[?]` | `transform_ref` | `FUN_002738e8(stack, rod+0x30, rod+0x10)` in glow-compute |
| `+0x60` | `float` | `glow_alpha` | Written by `FUN_00242ac8` (real target of thunk `00232da0`): `*(rod+0x60) = fVar3 * fVar2` (decay formula) |
| `+0xa8` | `undefined4` | `field_0xa8` | `*(rod+0xa8) = in_stack_00000028` in glow-compute |
| `+0xac` | `ptr` | `field_0xac` | `*(rod+0xac) = some_ptr` (lw $a3, 0xac($s0) in ui_render_3d_objects) |
| `+0x130` | `undefined8` | `group_transform` | Written by `FUN_002335e8`: `*(rodGroup+0x130) = param_3` (8 bytes) |
| `+0x140` | `int` | `field_0x140` | Cleared by `FUN_002335e8`: `*(rodGroup+0x140) = 0` |
| `+0x150` | `int` | `face_flag` | Front/back discriminator. `== 0` → front; `!= 0` → back. Used in every pass-loop condition. |

**Unconfirmed fields** (hypothesis, need runtime trace):
- `+0x00..0x03`: possibly `rod_index` or padding
- `+0x0C`: unknown
- `+0x158..0x15F`: padding to 0x160 stride

---

## 5. clockState struct field map

Passed as `param_3` (= `int* clockState`) to `ui_render_3d_objects`.

| Index | Offset | Name | Evidence |
|-------|--------|------|----------|
| `[0]` | `+0x00` | `rod_count_raw` | `fVar18 = (float)*param_3 * in_f1` — multiplied by transition to get base angle |
| `[1]` | `+0x04` | `rod_count` | `iVar15 = param_3[1]` — loop bound in steady-state variant |
| `[0x1b]` | `+0x6c` | `scale` | `fStack0000006c = param_1 * (float)param_3[0x1b]` — transition scale; also `(float)param_3[0x1b] * 26.0 * param_1` for translate |
| `[0x28]` | `+0xa0` | `transform_block_start` | `FUN_00232e38(angle,angle,rod, param_3+0x28)` — 4×4 matrix passed as last arg |
| `[0x2c]` | `+0xb0` | `angle_offset_A` | `fVar17 + (float)param_3[0x2c]` — added to rod angle in pass 3 (group A) |
| `[0x2d]` | `+0xb4` | `angle_offset_B` | `fVar17 + (float)param_3[0x2d]` — angle offset group B pass 3 |

**Globals used in angle computation** (angle-step-per-rod):
| Global | Role |
|--------|------|
| `fGpffff831c` | angle step — group A, pass 2 (transition) |
| `fGpffff8320` | angle step — group B, pass 2 (transition) |
| `fGpffff8324` | angle step — group A, pass 3 (transition) |
| `fGpffff8328` | angle step — group B, pass 3 (transition) |
| `fGpffff832c` | angle step — group A, pass 2 (steady) |
| `fGpffff8330` | angle step — group A, pass 3 (steady) |
| `fGpffff8318` | group transform scale multiplier |

---

## 6. GS packet template constants

The templates at `DAT_002973a0` (block A, 16 bytes = 4 × u32) and
`DAT_002973c0` (block B, 16 bytes = 4 × u32) are **runtime-only**: they are
all zero in the static ELF. They are written during `module_clock_init_resources`
(specifically by the `FUN_00232310` sub-call chain which sets up draw environments
via `FUN_00241cc0` + GS packet writer at `0x00248920`).

**To decode the actual ALPHA/COLCLAMP values:** run PCSX2 to the clock screen
and read:
```
pcsx2_read_memory 0x002973a0 16  # block A (glow pass header)
pcsx2_read_memory 0x002973c0 16  # block B (rod pass header)
```

The `pktSetAlphaBlend.s` calling convention:
```asm
; Call pktSetAlphaBlend(packet_buf, ABE_flag, A, B, C, D)
; → pktSetAD(buf, 0x49, COLCLAMP_value)   # COLCLAMP reg
; → pktSetAD(buf, 0x42, ALPHA_packed_value) # ALPHA reg
; where ALPHA = A | (B<<2) | (C<<4) | (D<<6) | (FIX_alpha<<32)
```
- COLCLAMP reg 0x49: bits[0]=CLAMP (1=clamp 0–255, 0=mask 0–255)
- ALPHA reg 0x42: A[1:0], B[3:2], C[5:4], D[7:6], FIX[39:32]

### Init function chain for draw-env setup

`module_clock_init_resources @ 0x00211488` → `FUN_00232310`:
```c
// FUN_00232310 — sets up 3 scissor/draw-env regions
FUN_00241cc0(4);   // template index 4 → DAT_002973a0 block?
sceGsPutDrawEnv(scissor_A);  // [masked as "WaitSema" by Ghidra]
FUN_00241cc0(5);   // template index 5
sceGsPutDrawEnv(scissor_B);
FUN_00241cc0(6);   // template index 6
sceGsPutDrawEnv(scissor_C);  // args match glow-pass dimensions
```
Scissor coords for template 6 (used for glow/after-image):
- x1 = `((0x1000 - screenW)/2)*16 + 0x2350`
- y1 = `((0x1000 - screenH)/2)*16 + 0x880`
- x2 = `x1_base + 0x24D0`
- y2 = `y1_base + 0x958`

---

## 7. Port notes (Vulkan rebuild)

### 7.1 Blend states (VkPipelineColorBlendAttachmentState)

Three Vulkan blend states to pre-build:

```cpp
// Pass 1/1b/1c — src-over (after-image / glow)
// GS: (Cs - Cd) * As/128 + Cd  ← standard Porter-Duff over
VkPipelineColorBlendAttachmentState blendSrcOver = {
    .blendEnable         = VK_TRUE,
    .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
    .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    .colorBlendOp        = VK_BLEND_OP_ADD,
    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
    .alphaBlendOp        = VK_BLEND_OP_ADD,
};

// Pass 2 — additive (prism surface crystal glow)
// GS: (Cs - 0) * As/128 + Cd
VkPipelineColorBlendAttachmentState blendAdditive = {
    .blendEnable         = VK_TRUE,
    .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
    .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
    .colorBlendOp        = VK_BLEND_OP_ADD,
    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
    .alphaBlendOp        = VK_BLEND_OP_ADD,
};

// Pass 3 — subtractive (refraction dark shadow)
// GS: (0 - Cs) * As/128 + Cd  OR  (Cs - Cd)*As/128+0
// Exact encoding pending runtime read of DAT_002973c0 (second half)
VkPipelineColorBlendAttachmentState blendSubtractive = {
    .blendEnable         = VK_TRUE,
    .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
    .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
    .colorBlendOp        = VK_BLEND_OP_REVERSE_SUBTRACT,
    // verify against runtime DAT_002973c0 read
};
```

> **ALPHA is 0–128 on PS2, not 0–255.** The GS divides C by 128, not 256.
> In Vulkan, a GS alpha of 128 = fully opaque. To match: pass alpha divided
> by 128 as the shader alpha uniform, not 255.

### 7.2 Depth / stencil (VkPipelineDepthStencilStateCreateInfo)

`FUN_00230518` controls the GS TEST register (0x47). The two modes observed:

| Call | GS TEST mode | Vulkan equivalent |
|------|-------------|-------------------|
| `(1,1)` | ZTST=ALWAYS or GEQUAL, ZTE=on, depthWrite=true | `depthTestEnable=true, depthWriteEnable=true, compareOp=GREATER_OR_EQUAL` |
| `(1,2)` / `(2,2)` | ZTST=ALWAYS, ZTE=off (no depth update) | `depthTestEnable=true, depthWriteEnable=false` |
| `(0,2)` | ZTST reset | `depthTestEnable=true, depthWriteEnable=true` |

> Exact TEST register bits must be confirmed from runtime `pcsx2_read_memory`.

### 7.3 Geometry

- **Rod geometry**: textured TRI_STRIP quads (from GS ground truth). Each rod = 4 vertices (STQ + XYZF2). UVs are GS texture-coordinate fixed-point (12.4 via ITOF16 in GS).
- **Vertex format**: XYZF2 (12.4 fixed-point XY, Z 24-bit, fog). Port as: `push-constant` mat4 (proj×rot per rod), vertex pos in local rod space, transform in shader.
- **Projection**: embeds GS screen coords (0–2048 range). In Vulkan: map GS output `[0,2048]` to NDC `[-1,1]` via `(x/1024.0 - 1.0)`, same for Y. Z stays as-is since near/far map to GS Z range.

### 7.4 Per-rod alpha / glow decay

`FUN_00242ac8` (real target of `FUN_00232da0` thunk) computes:
```
// Front rods (group A rod count 2..8):
factor = state[0x1b] * (target_idx - position_idx)
alpha = factor / 13.0  (or / 6.0 for inner ring)

// Back rods:
alpha = factor / 10.0  (or / 5.0)

// Selected rod override (rod == active_rod):
alpha *= (target_idx - iGpffff8b4c) / 40.0
```
Port as: per-rod push-constant float `glow_alpha`, written before each rod's draw call.

### 7.5 Packet structure → Vulkan draw sequence

Each GS "packet" in the original = one `vkCmdDraw` call in Vulkan:

```
GS sequence per rod:          Vulkan equivalent:
FUN_0022f720(buf)         → (no-op; state already bound)
write ALPHA template      → vkCmdBindPipeline(blend_state)
write SCISSOR template    → vkCmdSetScissor(...)
FUN_00232e38(...)         → push_constant(mat4, glow_alpha)
FUN_00235350()            → vkCmdDraw(4, 1, 0, rod_index)
```

### 7.6 Pass order summary (what to reproduce)

```
1. Set src-over blend + depth-write + scissor → front-face glow pass (Group A rods 8+, Group B rods 10+)
2. Set src-over blend + depth-no-write → back-face glow pass 0 (same rods, face_flag != 0, alpha_arg=0)
3. Set src-over blend + depth-no-write → back-face glow pass 0xFF (face_flag != 0, alpha=0xff override)
4. Set additive blend + depth-write → crystal prism surface pass (face_flag == 0, angle per rod)
5. Set depth-no-write → subtractive/refraction pass (face_flag == 0, shifted angle)
```

---

## 8. Open blockers

1. **Runtime DAT_002973a0/c0 values**: must be read from live PCSX2 to get
   exact ALPHA+COLCLAMP register bit-patterns. Blocks confirmed blend-mode values.

2. **FUN_002324e8 / FUN_00232538 exact blend args**: the decompiler shows a
   fallthrough body (wrong). The pcode confirms they both call `0x248920` with
   computed GS-register values derived from `s1+0xc54`/`c50`. The exact ABE
   field assignment (which A/B/C/D values) needs a live trace.

3. **FUN_002335e8 (3 instructions with VU0)**: the first instruction is a VU0
   mac/shift (raw `0xfd060130`). Not decoded. May initialize a VU0 accumulator
   for the group transform. Low priority — this is init-only, not per-draw.

4. **Rod struct fields `+0x00..0x03`, `+0x0C`, `+0x158..0x15F`**: no evidence yet.
   Live PCSX2 memory dump of `0x375250` (rod array) during clock display would
   fill these in.

5. **Texture binding** (`clock_load_texture @ 0x0022f9d8`): how the prism texture
   is bound per-rod draw is not yet decoded. The TCC (texture color component) and
   sampler state live in GS TEX0/TEX1 registers — not yet traced.
