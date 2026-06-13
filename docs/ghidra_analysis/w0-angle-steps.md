# W0 — Rod Angle-Step Global Constants

> **Task:** Resolve all gp-relative angle-step globals referenced in `ui_render_3d_objects @
> 0x00223f78` and `draw_crystal_rod @ 0x00232e38`. Values read from static data in
> `OSDSYS_A_XLF_decrypted_unpacked.elf` (load base `0x200000`). Not from live PCSX2.
>
> **GP base:** Ghidra `0x002cfef0` / decomp-ELF runtime `0x00377970`
> (derived in `w0-projection-constants.md §0`; verified against halfWidth `@ gp−0x7b80 = 41.6`).
>
> **Read method:** `file_offset = 0x1000 + (decomp_vaddr − 0x200000)`. All seven globals fall
> inside the single LOAD segment (`vaddr=0x200000, filesz=0x3ae819`) — none are BSS.

---

## 1. Angle-step global table

| Global | Ghidra addr | Decomp vaddr | gp offset | Raw bytes (LE) | IEEE-754 f32 | Degrees |
|--------|-------------|--------------|-----------|----------------|--------------|---------|
| `fGpffff8318` | `0x002c8208` | `0x0036fc88` | `gp−0x7ce8` | `cd cc cc 3d` | **0.10000** | 5.730° |
| `fGpffff831c` | `0x002c820c` | `0x0036fc8c` | `gp−0x7ce4` | `cd cc cc 3d` | **0.10000** | 5.730° |
| `fGpffff8320` | `0x002c8210` | `0x0036fc90` | `gp−0x7ce0` | `cd cc cc 3d` | **0.10000** | 5.730° |
| `fGpffff8324` | `0x002c8214` | `0x0036fc94` | `gp−0x7cdc` | `cd cc cc 3d` | **0.10000** | 5.730° |
| `fGpffff8328` | `0x002c8218` | `0x0036fc98` | `gp−0x7cd8` | `93 1a da 36` | **6.5e-6**   | ~0°     |
| `fGpffff832c` | `0x002c821c` | `0x0036fc9c` | `gp−0x7cd4` | `cd cc 4c 3e` | **0.20000** | 11.459° |
| `fGpffff8330` | `0x002c8220` | `0x0036fca0` | `gp−0x7cd0` | `cd cc cc 3e` | **0.40000** | 22.918° |

Confidence: **high** for all seven. All are in the ELF static-data segment (non-BSS). GP base is
verified against two independent known values (`halfWidth_4:3 = 41.6` @ `gp−0x7b80`,
`halfWidth_16:9 = 0.44` @ `gp−0x7b7c`).

---

## 2. Per-pass usage (from decompile of `ui_render_3d_objects @ 0x00223f78`)

The function has two top-level branches on `param_1 > 0.0` (transition) vs `else` (steady-state).

### 2a. Group transform scale

```c
// Both branches, before any pass loops:
FUN_002335e8(&stack0x1c0, ..., 0x375250, param_3);
fStack0x1c0 *= fGpffff8318;   // group A transform scale
fStack0x1c4 *= fGpffff8318;   // (same global, applied to both XY outputs)
```

| Global | Role |
|--------|------|
| `fGpffff8318` | Per-group transform input scale — multiplied into the XY outputs of `FUN_002335e8` before both pass loops. |

### 2b. Transition variant (param_1 > 0.0)

Four separate rod-group loops in the transition pass:

| Global | Load site (Ghidra) | Pass | Group | Angle formula |
|--------|--------------------|------|-------|---------------|
| `fGpffff831c` | `0x002242b0: lwc1 f22,-0x7ce4(gp)` | Pass 2 (additive) | A (`0x375250`) | `fVar18 + float(i) * fGpffff831c` |
| `fGpffff8320` | `0x0022434c: lwc1 f22,-0x7ce0(gp)` | Pass 2 (additive) | B (`0x377e50`) | `fVar18 + float(i) * fGpffff8320` |
| `fGpffff8324` | `0x00224404: lwc1 f20,-0x7cdc(gp)` | Pass 3 (subtractive) | A (`0x375250`) | `fVar18 + float(i) * fGpffff8324` |
| `fGpffff8328` | `0x002244a4: lwc1 f20,-0x7cd8(gp)` | Pass 3 (subtractive) | B (`0x377e50`) | `fVar18 + float(i) * fGpffff8328` |

`fVar18 = float(clockState[0]) * in_f1` (the accumulated base angle, common to all passes).

Note: `fGpffff8328 = 6.5e-6` rad (≈ 0°). Group B pass-3 rods in transition all share essentially
the same per-rod angle offset — no angular spread within the group during transition refraction pass.
This appears intentional (static data, confirmed non-BSS).

### 2c. Steady-state variant (param_1 == 0.0, else branch)

In the steady-state branch the function iterates **one rod group only**: from base `0x375250` with
bound `param_3[1]` (total rod count). There is no `0x377e50` (group-B) loop in passes 2 or 3.

| Global | Load site (Ghidra) | Pass | Group | Angle formula |
|--------|--------------------|------|-------|---------------|
| `fGpffff832c` | decompile: `fVar4 = fGpffff832c` before steady pass-2 loop | Pass 2 (additive) | A only (`0x375250`) | `fVar18 + float(i) * fGpffff832c` |
| `fGpffff8330` | decompile: `fVar4 = fGpffff8330` before steady pass-3 loop | Pass 3 (subtractive) | A only (`0x375250`) | `fVar18 + float(i) * fGpffff8330` |

---

## 3. Group-B steady-state resolution (C-A3)

**RESOLVED: Group B has NO dedicated steady-state angle-step globals.**

Evidence from the clean decompile of `ui_render_3d_objects`:

The steady-state `else` branch contains exactly two rod-draw loops (passes 2 and 3). Both loops:
- Use base pointer `iVar14 = 0x375250` (group A only)
- Use loop bound `iVar16 = param_3[1]` (total rod count from `clockState[1]`)
- Reference `fGpffff832c` (pass 2) and `fGpffff8330` (pass 3)

There is no `0x377e50` base pointer anywhere in the steady-state pass-2 or pass-3 sections. The
rod-pipeline.md §5 table listing group-B steady globals was therefore incomplete — no such globals
exist. Group B rods are **not rendered in steady-state passes 2/3**; they appear only in the
transition variant with globals `fGpffff8320` (p2) and `fGpffff8328` (p3).

The glow passes (passes 1/1b/1c) in steady-state also iterate only from `0x375250` with
`param_3[1]` rods, confirming the group-B base is entirely absent from the steady render path.

**Confidence: certain** — the Ghidra decompiler output for `ui_render_3d_objects` is clean (no
fallthrough artifacts), the steady-state else-branch code is fully visible, and no `0x377e50` load
or reference appears in it.

---

## 4. Summary table

| Global | Ghidra addr | f32 value | Pass | Group | Variant |
|--------|-------------|-----------|------|-------|---------|
| `fGpffff8318` | `0x002c8208` | 0.10000 | (scale) | Both | Both |
| `fGpffff831c` | `0x002c820c` | 0.10000 | 2 | A | Transition only |
| `fGpffff8320` | `0x002c8210` | 0.10000 | 2 | B | Transition only |
| `fGpffff8324` | `0x002c8214` | 0.10000 | 3 | A | Transition only |
| `fGpffff8328` | `0x002c8218` | 6.5e-6  | 3 | B | Transition only |
| `fGpffff832c` | `0x002c821c` | 0.20000 | 2 | A | Steady only |
| `fGpffff8330` | `0x002c8220` | 0.40000 | 3 | A | Steady only |

Steady group-B pass-2/pass-3: **does not exist** — group B is absent from steady render.

---

## 5. Observations

- In steady state, pass-3 angle step (0.4 rad/rod) is **2× pass-2** (0.2 rad/rod). The refraction
  pass fans out rods twice as far as the additive surface pass.
- In transition, all four named step globals (831c/8320/8324) are **0.1 rad/rod = 5.73°/rod**,
  making the transition a uniform-spread configuration across both groups in passes 2/3.
- `fGpffff8328` ≈ 0 means during transition pass-3, group-B rods are drawn with no angular spread
  (all stacked at the same angle offset). Whether this is visually intentional or a seeded default
  that gets overwritten at runtime is unknown — but the static data value is `0x361ada93`.
- `fGpffff8318 = 0.1` (scale). It scales both XY outputs of `FUN_002335e8` before being used in
  the group transform. This reduces the effective scale by 10× relative to the raw computed value.

---

## 6. Blockers

None for this task. All seven globals are static data (non-BSS), confirmed readable from the decomp
ELF. No live PCSX2 read is required.

The only open question is whether `fGpffff8328 ≈ 0` is intentional or a cold-boot default that
init code overwrites. A live read of `0x0036fc98` (runtime) during clock display would confirm.
This is low priority — the static value is what the clock renders with if init code doesn't change it,
and 0 is a valid angle step (all group-B rods draw at identical azimuth in transition pass-3).
