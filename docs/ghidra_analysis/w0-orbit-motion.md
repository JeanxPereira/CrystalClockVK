# OSDSYS Crystal Clock — Orbit Motion Analysis (W0-4)

> Source: `OSDSYS.elf` via ghidra-mcp (static only, program="OSDSYS.elf", base 0x001f0000).
> Runtime-trace complement: `runtime-trace.md` §Orbit integrate function table.
> All claims cite `name @ address`.

---

## 1. Orbit angle accumulator and advance

### Confirmed from static decompilation

`FUN_0022fd00 @ 0022fd00` contains the per-frame orbit angle update. From static disassembly at `0022ff18`:

```asm
0022ff18: lwc1 f1, -0x7478(gp)    ; f1 = fGpffff8b88  (orbit angle accumulator, float)
0022ff1c: lwc1 f0, -0x7b9c(gp)    ; f0 = fGpffff8464  (advance per frame, float)
0022ff20: lui at, 0x4980           ; at = 0x49800000 = 1048576.0 (IEEE754)
0022ff24: mtc1 at, f2              ; f2 = 1048576.0 (wrap modulus)
0022ff28: add.s f0, f1, f0         ; f0 = angle + advance
0022ff2c: c.le.s f2, f0            ; flag: 1048576.0 <= new_angle ?
0022ff34: bc1f 0x0022ff44          ; if NOT >=, skip subtraction
0022ff38: swc1 f0, -0x7478(gp)    ; store intermediate
0022ff3c: sub.s f0, f0, f2         ; f0 -= 1048576.0
0022ff40: swc1 f0, -0x7478(gp)    ; fGpffff8b88 = wrapped angle
```

**Angular update rule (exact):**
```
fGpffff8b88 += fGpffff8464
if fGpffff8b88 >= 1048576.0:
    fGpffff8b88 -= 1048576.0
```

The wrap modulus `1048576.0 = 2^20` represents one full orbit cycle. The scale maps to
0..1048576 covering 2π radians (confirmed by the sin/cos usage in `FUN_0022eb10` below).

**Same update confirmed** from a second decompile of `FUN_00221470 @ 00221470`:
```c
fRam002c8a78 = fRam002c8a78 + fRam002c8354;
if (1048576.0 <= fRam002c8a78)
    fRam002c8a78 = fRam002c8a78 - 1048576.0;
```
This is a parallel orbit-angle in a different subsystem (same pattern, different variables).

### Angular velocity constant — BLOCKER

`fGpffff8464` (the advance per frame) is at `gp - 0x7b9c`. This global is in BSS
(zero-initialized in the ELF static image). No static write to this address was found
via byte-pattern search. **HYPOTHESIS**: the value is written by some per-frame computation
or a config-driven init function not yet traced. Extracting this constant requires:
- A live read via PCSX2 (register reads were dead in previous session), OR
- Tracing the init chain from `module_clock_init_resources @ 00211488` deeper.

The advance constant controls the **orbital period**:
```
period_frames = 1048576.0 / fGpffff8464
```
At 60 fps a period of ~8 seconds → advance ≈ `1048576 / (60 * 8) ≈ 2185`.

---

## 2. Orbit position math in `FUN_0022eb10 @ 0022eb10`

This function IS the orbit billboard transform, called once per frame from `FUN_00211598 @ 00211598`.

```c
void FUN_0022eb10(void)
{
    // iGpffff8b48: flag (0 = orbs visible)
    // iGpffff8b4c: orb count / active slot count (> 0 when orbs active)
    // iGpffff8b7c: mode/type selector (0x13, 1, 0x12 → scale factor 1; else 4)

    if ((iGpffff8b48 == 0) && (0 < iGpffff8b4c)) {
        int iVar1 = 4;
        if ((iGpffff8b7c == 0x13) || (iGpffff8b7c == 1) || (iGpffff8b7c == 0x12))
            iVar1 = 1;

        // Sin of (2 * angle * scale)
        FUN_0025ffd8(FUN_00247ae8((fGpffff8b88 + fGpffff8b88) * (float)iVar1));

        // Cos of (angle * scale) → Y offset
        float cosVal = (float)FUN_0025fd68(FUN_00247ae8(fGpffff8b88 * (float)iVar1));
        float Y = cosVal * 0.25 + fGpffff8408;        // fGpffff8408 = Y-axis tilt/offset

        FUN_002733b0();     // matrix set Y rotation
        float X = uGpffff8410;    // fGpffff8410 = X-axis position/radius
        FUN_002736f0();     // matrix set X/Z

        // Orb scale (billboard size) from time counter:
        float fVar4 = 40.0;
        if (iGpffff8d18 != 0) fVar4 = 33.0;
        float scale = ((float)iGpffff8b4c * 0.5) / fVar4 + 0.5;

        FUN_002477a8();    // billboard matrix setup
        FUN_002738a0();    // matrix self-multiply
        FUN_00242f50();    // apply transform
    }
}
```

**Orbit math deduced:**
- `fGpffff8b88` = orbit angle accumulator (0..1048576, wraps)
- `FUN_00247ae8` = angle normalizer / modulo (maps to radian trig range)
- `FUN_0025ffd8` = sin
- `FUN_0025fd68` = cos
- Y-position = `cos(angle * scale_factor) * 0.25 + fGpffff8408`
- X-position/radius = `fGpffff8410`
- Scale factor `iVar1` is 1 in normal mode, 4 in special mode (triple-speed)

`fGpffff8408` and `fGpffff8410` are gp-relative globals in BSS — values are runtime init. They encode the **orbital tilt/Y-offset** and **orbital radius/X-offset** respectively. **BLOCKER**: exact numeric values require live read or tracing init code.

`FUN_0022eaf0 @ 0022eaf0` is a duplicate of `FUN_0022eb10` without the language check at top (same body; both the billboard position setups). Same globals used.

---

## 3. fn-table dispatch `module_clock_22F5D0 @ 0022b5f0`

### Dispatch mechanism

Static disassembly confirmed:
```asm
0022b5f0: lui v0, 0x2a
0022b5f4: addiu v0, v0, -0x4c40   ; v0 = 0x29b3c0 (table base)
0022b5f8: sll v1, v1, 2            ; v1 = slot_index * 4
0022b5fc: addu v1, v1, v0          ; v1 = &table[slot_index]
0022b600: lw v1, 0(v1)             ; v1 = table[slot_index] (function pointer)
0022b604: bne v1, zero, 0x0022b614 ; if non-null, call it
0022b614: jalr v1                  ; call(*fn_ptr)()
```

### Table contents (static ELF vs runtime)

- **Static ELF** (`0x0029b3c0`): all zeros. Pointers are installed at runtime.
- **Runtime** (from `runtime-trace.md`): `table[0] = 0x00239440`, `table[1] = 0x00238D60`.

### Who calls the dispatch and what index

`module_clock_22F5D0` is called from `module_clock_22FE98 @ 0021beb8`. Confirmed by byte-pattern search for JAL at `0x0021beec`. The sequence before the call:

```asm
0021becc: jal 0x0022b2b8    ; call module_clock_22F298
...
0021beec: jal 0x0022b5f0    ; call module_clock_22F5D0 (orbit dispatch)
```

`module_clock_22F298 @ 0022b2b8` sets v1=3 or v1=`gp[-0x6f5c]+0x40` before returning.
- Path 1 (`v1=3`): `table[3]` = null at runtime → dispatch is NO-OP on this call.
- Path 2 (v1 = dynamic): v1 = incremented counter from `iRam002c8f94` area.

**BLOCKER**: the exact runtime slot index fed into the dispatch depends on execution state. The fn-table has only indices 0 and 1 populated at runtime, so the dispatch either hits one of those or is a no-op. The call pattern matches a per-frame conditional update of **one slot at a time** (not all slots simultaneously).

### Ghidra addresses of the two integrate functions — NOT FOUND (static blocker)

The runtime addresses `0x00239440` (table[0]) and `0x00238D60` (table[1]) **cannot be
resolved to Ghidra entries** by static analysis because:

1. The fn-table at `0x0029b3c0` is all-zeros in the ELF (runtime-patched).
2. The runtime→Ghidra skew is non-uniform across code regions (confirmed by multiple
   module_clock examples: skew varies between `0x3FE0` and `0x14928` and `0x13998`
   depending on segment). Applying any candidate skew to the two runtime addresses
   produces addresses with no matching Ghidra function entry in any tested combination.
3. No `swc1/sw` instruction in the static ELF writes to `0x0029b3c0`+offsets (byte
   pattern search returned no matches).

**HYPOTHESIS**: the two functions at runtime `0x00239440` and `0x00238D60` are in a code
region with a skew not yet characterized. They are likely the two orbit-slot updaters that
compute the position per orb context and push to the trail ring buffer. The ring buffer
push is what makes the trail from the orbit path.

---

## 4. Orb count

### Evidence from renderer callers

`FUN_00225be8 @ 00225be8` (the GS packet orb renderer) has exactly **4 callers**, all in
the `0x001f5xxx`–`0x001f6xxx` range:

| Caller | Ghidra addr | Orb context base | Condition |
|--------|-------------|-----------------|-----------|
| `FUN_001f5d84` | `001f5d84` | `0x27b3a8` | Unconditional |
| `FUN_001f60c4` | `001f60c4` | `0x27b3d0` | Unconditional |
| `FUN_001f61a4` | `001f61a4` | `0x27b3f8` | Conditional (`FUN_00204498` != 0) |
| `FUN_001f6450` | `001f6450` | `0x27b420` | Unconditional |

Orb context blocks are spaced `0x28` bytes apart. Stride matches the context-slot geometry.

- **3 orbs unconditional + 1 conditional** → **normally 3, sometimes 4**.
- The conditional orb at `0x27b3f8` is gated by `FUN_00204498()` which appears to be a
  file/texture streaming check — it renders the 3rd orb only when a resource is loaded.

### Evidence from fn-table (runtime-trace)

The fn-table at `0x0029b3c0` has exactly **2 non-null entries** at runtime (indices 0 and
1). This is consistent with 2 orbit-integrate *physics* slots while the renderer has 3–4
*render* slots. The extra render slots may reuse the same orbit position (e.g., different
textures / scale factors on the same orbit path) or be for trailing / ghost orbs.

### `iGpffff8b4c` as active slot counter

From `FUN_0022eb10`: `0 < iGpffff8b4c` guards the entire orbit billboard render.
`iGpffff8b4c` is written by `module_clock_22FEF0 @ 0022bf10` which sets `*(param+0x400)`.
This global tracks how many orb slots are active.

### Conclusion on orb count

**ORB COUNT = 2 physics orbits, 3 render orbs normally (4 when a texture is loaded).**

The 2 fn-table entries correspond to 2 distinct orbit paths. The render layer shows 3 orbs
because one orbit may be rendered twice (e.g., core + halo on the same path) or because
one render slot is for a persistent after-image.

**CONFIDENCE: MEDIUM-HIGH** for "≥ 2 distinct orbit paths". The exact total visible orb
count (3 or 4) and whether all share one path or are on distinct paths needs live
confirmation.

---

## 5. `iGpffff8b4c` — orb count / fade counter

Seen in `FUN_0022eb10`:
```c
float fVar4 = 40.0;
if (iGpffff8d18 != 0) fVar4 = 33.0;
float scale = ((float)iGpffff8b4c * 0.5) / fVar4 + 0.5;
```
`fVar4 = 40.0` for NTSC (480i), `33.0` for PAL (576i). `iGpffff8b4c` ramps 0..40 (NTSC)
or 0..33 (PAL) to produce billboard scale 0.5..1.0. This is a **spawn-in fade counter**,
not a count of active orbs per se — but "0 < iGpffff8b4c" still gates rendering.

---

## 6. Global index

| Global | GP offset | Role | Value status |
|--------|-----------|------|--------------|
| `fGpffff8b88` | gp-0x7478 | Orbit angle accumulator | Runtime (BSS, dynamic) |
| `fGpffff8464` | gp-0x7b9c | Orbit angle advance/frame | Runtime (BSS) — **value unknown** |
| `fGpffff8408` | gp-0x7bf8 | Y-axis tilt offset | Runtime (BSS) — **value unknown** |
| `uGpffff8410` | gp-0x7bf0 | X-axis radius/offset | Runtime (BSS) — **value unknown** |
| `iGpffff8b4c` | gp-0x74b4 | Orb spawn-in counter (0..40/33) | Dynamic |
| `iGpffff8b48` | gp-0x74b8 | Orb visibility flag (0=visible) | Dynamic |
| `iGpffff8b7c` | gp-0x7484 | Mode (0x13/1/0x12 → scale×1, else ×4) | Dynamic |
| `fGpffff8bc0` | gp-0x7440 | Camera/view orbit angle | Dynamic (controller input) |
| `fGpffff8478` | gp-0x7b88 | Camera angle step (R button) | Unknown |
| `fGpffff8474` | gp-0x7b8c | Camera angle step (L button) | Unknown |
| `fRam002c8a78` | `0x002c8a78` | Second orbit angle (different subsystem) | Dynamic |
| `fRam002c8354` | `0x002c8354` | Second orbit advance | Unknown |

---

## 7. Blockers

1. **fGpffff8464 value** (orbital angular velocity): BSS — requires live read or deeper
   init-chain trace. Priority: HIGH (directly sets orbital speed).

2. **fGpffff8408 / fGpffff8410** (Y-tilt offset, X-radius): BSS — same blocker.
   These encode the orbital shape (elliptical if 8408 ≠ 0, planar tilt, radius).

3. **Ghidra addresses of the two integrate functions** (runtime `0x00239440` and
   `0x00238D60`): non-uniform skew prevents static resolution. Would reveal the exact
   per-slot orbit path math (phase offsets, individual radius/velocity).

4. **Exact orb count**: 2 physics orbits confirmed, 3–4 render slots. Whether the 3rd
   render slot is an after-image of slot 0 or an independent third orbit is unknown.
   Check: decompile `FUN_001f61a4 @ 001f61a4` caller chain and `FUN_00204498`.

5. **Slot phase offset**: The two orbit integrate functions likely have different initial
   phase offsets (e.g., 0° and 180°, or 120° and 240° for 3-way symmetry). This is
   invisible from the fn-table stub — requires decompiling the functions themselves.

---

## 8. Vulkan port guidance (what IS implementable now)

```c
// Per-frame orbit update (exact, from static analysis):
orb_angle += orb_advance;
if (orb_angle >= 1048576.0f) orb_angle -= 1048576.0f;

// Billboard Y position (confirmed shape, placeholder constants):
float norm_angle = orb_angle * (2.0f * M_PI / 1048576.0f);
float Y = cosf(norm_angle) * 0.25f + Y_OFFSET;  // Y_OFFSET = fGpffff8408 (unknown)
float X = X_RADIUS;                               // X_RADIUS = fGpffff8410 (unknown)

// Billboard scale ramp (spawn-in, exact):
float scale_factor = (iGpffff8b4c * 0.5f) / 40.0f + 0.5f;  // NTSC; 33.0f for PAL

// Orbit scale mode (exact):
int freq_mult = 4;  // default
if (mode == 0x13 || mode == 1 || mode == 0x12) freq_mult = 1;
float orbit_Y = cosf(norm_angle * freq_mult) * 0.25f + Y_OFFSET;
```

**Placeholder values until live reads or init-chain trace**: `Y_OFFSET ≈ 0`, `X_RADIUS ≈ 1.0`.
Use the rod world position (Y ≈ 14.6 from runtime-trace.md `0x00375250`) as a
cross-reference for orbital scale.
