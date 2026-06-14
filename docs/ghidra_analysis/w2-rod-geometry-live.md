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

## ⭐ The rods ARE the clock dial (user insight, 2026-06-14)

Confirmed live from the **"Ajuste do Relógio"** (clock-adjust) mode, which FREEZES the spin:
- At time **00:00:00**, the **top (12 o'clock) rod becomes "filled"/bright** while the rest stay dim.
  → the 16 rods are the **clock DIAL markers / pointers**, and a per-rod **fill/highlight state shows
  the time** (not decoration). This is the functional meaning of the ring.
- The `+0x150` flag (earlier called "front/back", render sub-pass selector) is therefore almost
  certainly the **per-rod FILL/active state** driven by the current time (HYPOTHESIS — confirm by
  reading `+0x150` of all 16 at a known time vs the highlighted rod).
- FROZEN-mode rod screen origins (`+0x20`) are now **spread** on a small inner ring (~150 px radius)
  around centre ~(1852, 2080) GS px — vs the spinning frame where they sampled near-centre. So the dial
  layout is real; `+0x20` is the inner (centre) end of each bar, the bar sweeps outward to the dial.
- Menu CUBES (left side of the screen) are a separate config-menu element (confirmed visually).

## Count reconciliation (user, 2026-06-14): 16 = 12 rods + 4 menu cubes

Patent says **12** rods (dial) + separate hexagonal **cubes `302`** (menu items, FIG 8). Live measured
**16** slots in `0x375250`. Leading reconciliation (user's): the array holds **12 dial rods (slots 0-11)
+ 4 menu cubes (slots 12-15)**, both sharing the SAME crystal-prism refraction shader, so they live in
one object array. 12+4 = 16, and slot 16 = end. Matches patent + measurement + the on-screen cubes.
CAVEAT to confirm: slots 12-15 read as valid (rod-like positions) even in the PURE Visor where cubes are
not drawn — so if they are cubes they are allocated/persistent there. DECISIVE TEST (pending): diff
slots 12-15 between menu mode (cubes shown) and pure Visor — if they activate/change with the cubes,
confirmed. Until then: **dial = 12 rods** (use this for the rebuild), cubes = menu, both crystal prisms.

## ⭐⭐ Patent-grounded model (US6693606 digest §2-4) — the framework I should have used first

The patent (2nd embodiment = our clock) already describes ALL of this; the live RE just confirms it:
- **Rods `306`** = transparent radial PRISMS, longitudinal axis pointing OUT = the clock **DIAL**.
  Patent says **12** rods; live array measured **16** valid slots (reconcile — trace is authoritative
  per the digest, but recheck: 12 dial + 4? or stride). They spin as a **group** AND each about its
  **own axis** (S306-S308), rewriting all vertices per frame (= why positions/normals churn live).
- **`306a` = the single COLORED rod = the HOUR** (which dial position is colored). At 00:00 → top/12.
  This is the user's "rod fills as a pointer" — it's the patent's hour read-out.
- **Minutes+seconds = the AMOUNT OF COLORING** along `306a` (a PARTIAL fill of a vertex RANGE; 100% at
  0 m/s, decreasing to ~0 near rollover). The fill is per-VERTEX COLOR data, NOT a flag — so it is NOT
  `+0x150` (that's front/back, 10/6 split) and NOT any single field. Analog read-out via colour range.
- **AM = blue, PM = red** (matches the blue morning screenshots).
- **Light spots `308`** = small points on tangled paths inside a central **wireframe sphere `310/312`**,
  drawn with **after-image trails** (S312). → these ARE the `0x34c830` 8-element array (`FUN_002354c8`)
  + the S-curve trails + the centre glow seen in the Visor. (Patent open Q: spot count — live = 8.)
- **Order:** rods (refraction+bump, feedback loop) → light spots (additive after-image) → blur (post).

### My over-reaches this session, seen through the patent
- "8 rods" — no; 8 = the light spots (patent `308`). Rods are the dial (12/16).
- "ring is in the `+0x140` normals" — no; `+0x140` is a surface normal. The dial angle = even radial
  layout (a clock dial) + group/own-axis spin (patent S306-308), rewritten per frame.
- "`+0x150` = fill" — no; fill = colour-vertex range on `306a` (patent S304-305). `+0x150` = front/back.
All three were re-derived blind from memory instead of grounded in the patent digest I already had.

## Rebuild stance (honest, patent-grounded)
- Rods = **a radial prism dial** (evenly spaced; 12 or 16 — reconcile), each prism axis pointing out,
  spinning as group + own-axis. ONE rod coloured = hour; partial colour fill = min/sec; AM blue/PM red.
- Light spots = 8 points + trails inside a central sphere.
- This is the model to build (geometry now well-understood); exact spin rates + even-spacing validate
  against the reference + the live array.

## (superseded) Rebuild stance

- CONFIRMED for the rebuild: **16 rods, radiating from a shared centre** (centre → screen centre).
- HYPOTHESIS to validate (not confirmed): the 16 bars are **evenly spaced (22.5° apart)** in the clock
  plane — the natural default for a 16-spoke clock, consistent with the screenshot. Build it, render,
  and check the ring against the reference; if uneven, isolate the real per-rod angle from the render's
  rotation inputs.
