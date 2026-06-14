# W2 — Clock element generation (Ghidra) — IN PROGRESS

> **CORRECTION (user caught a controller over-reach):** an earlier draft of this doc claimed
> `FUN_002354c8` generates the 8 RODS. **That was wrong.** It writes a DIFFERENT array. See below.
> The real rod generator (writes the `0x375250` array, 0x160 stride) is **NOT yet found**, and the
> **rod count is unknown** (could be 12 like clock-hour positions — do not assume 8).

## What `FUN_002354c8` / `FUN_0020eda0` actually generate — the 8-element `0x34c830` array (NOT rods)

`module_clock_init_resources @ 0x00211488` calls `FUN_002354c8` first; it tail-loops in
`FUN_0020eda0 @ 0x0020eda0`. The loop (index 0..7, `while (i < 7)` → **8 elements**) writes:
```c
int o = index * 0x10;                                  // stride 0x10 (16 bytes) — NOT 0x160
float r   = (float)((7-i)*(7-i)) * fGpffff81d8;         // quadratic radius in (7-i)
float mag = (r + r) / 64.0f;
// angle wrapped into [fGpffff81e0, fGpffff81d8]
*(float*)(o + 0x34c830) = sinf(angle) * mag * scale;   // X  (FUN_0025fd68 = sin)
*(float*)(o + 0x34c834) = cosf(angle) * mag * scale;   // Y  (FUN_0025ffd8 = cos)
// + a separate running base at (base - 0x3840) advanced by i*fGpffff81a0/a4/a8 (X/Z/Y)
//   with per-axis clamp/wrap into [fGpffff81b0..81d4]
```
**Target = `0x0034c830`, 8 × 0x10 bytes.** The ROD array is `0x00375250`, 8?/12? × **0x160** bytes —
a completely different structure. So this is **NOT the rod generator**. Stride 0x10 + polar sin/cos +
quadratic (7-i) radius = a small radial spread of 8 points — most likely the **light spots / glints**
(patent: "light spots") or orb glints. (Identity TBD; the point here is: NOT rods.)

## The real rod generator — STILL UNFOUND

- Rod array `0x375250` (0x160 stride) is live-confirmed (`s3=0x375250` in the render
  `ui_render_3d_objects @ 0x223f78`). `get_xrefs_to 0x375250` returns nothing (the address is built by
  a split `lui/addiu`, so Ghidra logs no data xref) — the writer must be found another way.
- Rod COUNT unknown. Do NOT assume 8 (that was the light-spot count). The clock-hand intuition (12) is
  unverified. The render iterates `clockState[1]` rods — value not captured.
- Functions checked and RULED OUT as the rod generator: `FUN_002354c8`/`FUN_0020eda0` (→ 0x34c830 light
  spots), `FUN_00230e10`/`FUN_00231190`/`FUN_00231580` (input/menu/transition state), `FUN_002335e8`
  (group transform only), `FUN_00231cf0` (UI Y-layout 30/110/156 px).

## Reliable next step

Decompile-hunting for the `0x375250` writer has misfired repeatedly (light spots, input handlers).
The RELIABLE path: **live-read the populated `0x375250` array** (the rods' actual world positions +
count, directly) when the **crystal-clock visor is actively rendering rods**. Every attempt so far
read zeros (emulator on a non-visor screen / kernel idle). Once the visor is live, one read gives the
real rod count AND all positions — settling geometry without trusting a fragile asm decode. Secondary:
a systematic instruction-search for the `0x375250` immediate to find its writer.
