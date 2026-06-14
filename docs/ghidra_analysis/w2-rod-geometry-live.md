# W2 — Rod geometry, from the LIVE Visor (PCSX2, 2026-06-14)

> Definitive rod geometry, read live from `0x375250` (group A) with the **crystal-clock Visor active**
> (paused on the prism ring). Settles the count + structure that the static decompile could not (the
> generator writes via a computed address with no Ghidra xref). Corrects two earlier wrong claims:
> controller said "8 rods" (wrong — 8 was the light spots), user guessed "12" (close, real = **16**).

## ROD COUNT = 16 (group A)

Read each slot at `0x375250 + i*0x160`; field `+0x0c` is the `1.0` valid-marker:
- slots **0..15** = valid rods (`+0x0c = 0x3f800000`).
- slot **16** (`0x00376850`) = all zero.
- Exact: `16 * 0x160 = 0x1600`, `0x375250 + 0x1600 = 0x376850` (the first zero slot). → **16 rods.**

(Group B `0x377e50` is transition-only, W0-2; the steady Visor renders group A's 16.)

## The "8" = light spots, NOT rods (user was right)

`FUN_002354c8`/`FUN_0020eda0` fill `0x34c830` (8 × 0x10) — the central white glow dots + S-curve trails
visible in the Visor screenshot. Confirmed separate from the rods.

## ROD struct (0x160) — decoded from the live array

| offset | field |
|--------|-------|
| `+0x00/04/08` | world XYZ of the rod **origin** (current frame) |
| `+0x0c` | `1.0` valid marker |
| `+0x10/14` | 2 floats (small; per-rod) |
| `+0x20/24/28` | projected screen X/Y/Z (GS pixels) |
| `+0x30/34/38` | screen X/Y/Z in **12.4 fixed** (`=+0x20..*16`) |
| `+0x40` | ~perspective 1/w |
| `+0x50, +0xa0, +0xf0` | **3 previous-frame origins** = the 4-deep after-image TRAIL |
| `+0x140/144/148` | **unit NORMAL** = the rod's radial direction (per-rod, distinct) |
| `+0x150` | front/back flag (render sub-pass selector) |

## SOLID model — 16 bars radiating from a shared centre

All 16 rods have **nearly the same origin** (~world (24, -18, 78), ±2 per rod) and project to ~the
**same screen point** (~2207, 1989 GS px, within ~6 px = the clock centre). They do NOT sit on distinct
ring positions. So the radial clock shape = **16 prism bars sharing a centre, each swept outward in a
different direction** (the bar geometry, drawn rotated). The centre projects to the screen centre.
This replaces the W1 guess (8 rods on a flat XZ circle of distinct positions).

Live rod-origin samples (one paused frame; rods spin so absolutes rotate):
rod0 (25.1, -19.56, 80.39) → screen (2207.9, 1989.4). All 16 origins cluster (23-25, -16..-21, 76-81).

## The 16 `+0x140` normals — and a CORRECTED over-reach

An earlier draft claimed `+0x140` is each rod's radial direction → "the ring is in the orientations."
**That was wrong** (controller self-corrected before committing). The 16 unit vectors, with their
in-plane angle `atan2(y,x)`:
```
i0  ( 0.749,-0.399,-0.529) -28°   i8  (-0.749, 0.399, 0.529) 152°
i1  ( 0.749,-0.399,-0.529) -28°   i9  (-0.749, 0.399, 0.529) 152°   (i0=i1, i8=i9 duplicates)
i2  ( 0.118,-0.293,-0.949) -68°   i10 (-0.582,-0.015,-0.813) -179°
i3  ( 0.941,-0.272, 0.201) -16°   i11 ( 0.582, 0.015, 0.813)   1°   (i10=-i11)
i4  (-0.012, 0.556,-0.436)  91°   i12 (-0.017, 0.787,-0.616)  91°   (i4=i12 angle)
i5  ( 0.400, 0.566, 0.138)  55°   i13 ( 0.565, 0.801, 0.195)  55°   (i5=i13 angle)
i6  (-0.400,-0.566,-0.138)-125°   i14 (-0.565,-0.801,-0.195)-125°   (i6=-i5, i14=-i13)
i7  ( 0.012,-0.556, 0.436) -89°   i15 ( 0.017,-0.787, 0.616) -89°   (i7=-i4, i15=-i12)
```
**Duplicates** (i0=i1, i8=i9; i4=i12, i5=i13… share angles) and **opposite pairs** (i5=-i6, i4=-i7,
i10=-i11, i13=-i14, i12=-i15) → these are **NOT 16 evenly-spaced radial directions**. So `+0x140` is the
rod's **surface / lighting normal**, NOT the ring-angle layout. Do not use it as the radial direction.

## Where the ring angle ACTUALLY lives (open)

The per-rod radial direction is applied at RENDER by `rotation_build` (the rod's bar mesh swept along a
rotated axis), from a per-rod ANGLE — which is NOT one of the struct fields read here (not `+0x04`,
not `+0x140`). It is computed in the render loop (base + per-rod step) and/or a per-rod angle field.
**Not yet isolated.**

## Rebuild stance (honest)

- CONFIRMED for the rebuild: **16 rods, radiating from a shared centre** (centre → screen centre).
- HYPOTHESIS to validate (not confirmed): the 16 bars are **evenly spaced (22.5° apart)** in the clock
  plane — the natural default for a 16-spoke clock, consistent with the screenshot. Build it, render,
  and check the ring against the reference; if uneven, isolate the real per-rod angle from the render's
  rotation inputs.
