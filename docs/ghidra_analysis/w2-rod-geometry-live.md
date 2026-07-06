# W2 — Rod geometry, from the LIVE Visor (PCSX2, 2026-06-14)

> Audit 2026-07-05: claims status-tagged per master-strategy spec §6.

> Definitive rod geometry, read live from `0x375250` (group A) with the **crystal-clock Visor active**
> (paused on the prism ring). Settles the count + structure that the static decompile could not (the
> generator writes via a computed address with no Ghidra xref). [LIVE-VERIFIED for the read itself]
> Corrects two earlier wrong claims: controller said ~~"8 rods"~~ (wrong — 8 was the light spots),
> user guessed "12" (close, real = ~~**16**~~). **[FALSIFIED → per known-falsified item 8, "8 rods on a
> circle" is wrong; the array is actually a 12-rod dial + 4 menu cubes (16 slots total, stride 0x160).
> This doc's own "16 rods" interim conclusion is itself superseded later in this same file by the
> "COUNT = 12 RODS (settled)" section below — the slot COUNT of 16 is LIVE-VERIFIED, but the
> "16 rods" INTERPRETATION was wrong.]**

## ROD COUNT = 16 (group A) [LIVE-VERIFIED slot count; superseded interpretation — see "COUNT = 12 RODS (settled)" below]

Read each slot at `0x375250 + i*0x160`; field `+0x0c` is the `1.0` valid-marker: [LIVE-VERIFIED]
- slots **0..15** = valid rods (`+0x0c = 0x3f800000`). [LIVE-VERIFIED for slot validity; ~~"rods"~~ **[FALSIFIED → 12 dial rods + 4 menu cubes, known-falsified item 8]**]
- slot **16** (`0x00376850`) = all zero. [LIVE-VERIFIED]
- Exact: `16 * 0x160 = 0x1600`, `0x375250 + 0x1600 = 0x376850` (the first zero slot). → ~~**16 rods.**~~ **[FALSIFIED → 16 valid slots = 12 dial rods + 4 menu cubes, known-falsified item 8]** [LIVE-VERIFIED for the arithmetic/slot count]

(Group B `0x377e50` is transition-only, W0-2; the steady Visor renders group A's 16 slots.) [LIVE-VERIFIED / DECOMP-SOURCED cross-reference]

## The "8" = light spots, NOT rods (user was right)

`FUN_002354c8`/`FUN_0020eda0` fill `0x34c830` (8 × 0x10) — the central white glow dots + S-curve trails
visible in the Visor screenshot. Confirmed separate from the rods. [LIVE-VERIFIED / DECOMP-SOURCED — this correction matches known-falsified item 8's "8 rods on a circle" being wrong (8 = light spots, not rods)]

## ROD struct (0x160) — decoded from the live array

| offset | field |
|--------|-------|
| `+0x00/04/08` | world XYZ of the rod **origin** (current frame) [LIVE-VERIFIED] |
| `+0x0c` | `1.0` valid marker [LIVE-VERIFIED] |
| `+0x10/14` | 2 floats (small; per-rod) [LIVE-VERIFIED reading; role HYPOTHESIS] |
| `+0x20/24/28` | projected screen X/Y/Z (GS pixels) [LIVE-VERIFIED] |
| `+0x30/34/38` | screen X/Y/Z in **12.4 fixed** (`=+0x20..*16`) [LIVE-VERIFIED] |
| `+0x40` | ~perspective 1/w [HYPOTHESIS] |
| `+0x50, +0xa0, +0xf0` | **3 previous-frame origins** = the 4-deep after-image TRAIL [LIVE-VERIFIED reading; "trail" interpretation HYPOTHESIS] |
| `+0x140/144/148` | ~~**unit NORMAL** = the rod's radial direction (per-rod, distinct)~~ **[Superseded within this same file — see "CORRECTED over-reach" section below: `+0x140` is a surface/lighting normal, NOT the ring/radial-angle layout. HYPOTHESIS at time of writing this row, corrected later in-doc.]** |
| `+0x150` | front/back flag (render sub-pass selector) [LIVE-VERIFIED reading; role cross-checked against `rod-pipeline.md §4`] |

## SOLID model — 16 bars radiating from a shared centre [LIVE-VERIFIED slot/measurement data; "16 bars" count superseded — see known-falsified item 8 / "COUNT = 12 RODS (settled)" below]

All 16 rods have **nearly the same origin** (~world (24, -18, 78), ±2 per rod) and project to ~the
**same screen point** (~2207, 1989 GS px, within ~6 px = the clock centre). They do NOT sit on distinct
ring positions. [LIVE-VERIFIED] So the radial clock shape = **16 prism bars sharing a centre, each swept outward in a
different direction** (the bar geometry, drawn rotated). The centre projects to the screen centre.
~~This replaces the W1 guess (8 rods on a flat XZ circle of distinct positions).~~ **[FALSIFIED → both the "16 prism bars" count in this sentence and the "8 rods" it references are superseded; per known-falsified item 8 the correct model is 12 dial rods + 4 menu cubes, not 16 and not 8.]**

Live rod-origin samples (one paused frame; rods spin so absolutes rotate): [LIVE-VERIFIED]
rod0 (25.1, -19.56, 80.39) → screen (2207.9, 1989.4). All 16 origins cluster (23-25, -16..-21, 76-81). [LIVE-VERIFIED]

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
rod's **surface / lighting normal**, NOT the ring-angle layout. Do not use it as the radial direction. [LIVE-VERIFIED]

## Where the ring angle ACTUALLY lives (open)

The per-rod radial direction is applied at RENDER by `rotation_build` (the rod's bar mesh swept along a
rotated axis), from a per-rod ANGLE — which is NOT one of the struct fields read here (not `+0x04`,
not `+0x140`). It is computed in the render loop (base + per-rod step) and/or a per-rod angle field.
**Not yet isolated.** [HYPOTHESIS/open]

## ⭐ The rods ARE the clock dial (user insight, 2026-06-14)

Confirmed live from the **"Ajuste do Relógio"** (clock-adjust) mode, which FREEZES the spin: [LIVE-VERIFIED]
- At time **00:00:00**, the **top (12 o'clock) rod becomes "filled"/bright** while the rest stay dim.
  → the ~~16~~ **[FALSIFIED → 12, per known-falsified item 8]** rods are the **clock DIAL markers / pointers**, and a per-rod **fill/highlight state shows
  the time** (not decoration). This is the functional meaning of the ring. [LIVE-VERIFIED for the freeze/highlight behavior; rod count corrected]
- The `+0x150` flag (earlier called "front/back", render sub-pass selector) is therefore almost
  certainly the **per-rod FILL/active state** driven by the current time (HYPOTHESIS — confirm by
  reading `+0x150` of all 16 at a known time vs the highlighted rod). [HYPOTHESIS, as labeled]
- FROZEN-mode rod screen origins (`+0x20`) are now **spread** on a small inner ring (~150 px radius)
  around centre ~(1852, 2080) GS px — vs the spinning frame where they sampled near-centre. So the dial
  layout is real; `+0x20` is the inner (centre) end of each bar, the bar sweeps outward to the dial. [LIVE-VERIFIED]
- Menu CUBES (left side of the screen) are a separate config-menu element (confirmed visually). [LIVE-VERIFIED]

## COUNT = 12 RODS (settled). Array 16 slots = 12 rods + 4 menu cubes.

**The clock dial is 12 rods — final.** A 12-hour clock has 12 hour positions; the patent says 12; the
hour = 1-of-12 coloured. The `0x375250` array's 16 slots = **12 dial rods (0-11) + 4 menu cubes (12-15)**,
both sharing the crystal-prism refraction shader. Do not relitigate this. (Earlier controller "16 rods"
was the error — it counted the 4 cubes as rods.) [This is the corrected state per known-falsified item 8 — LIVE-VERIFIED slot count (16) / DECOMP+patent-cross-referenced 12+4 split; the 12/4 split itself is not independently live-confirmed per-slot — see CAVEAT below, still HYPOTHESIS for the exact 0-11/12-15 boundary]

### (historical) earlier reconciliation note [HYPOTHESIS — pre-dates the "settled" section above]

Patent says **12** rods (dial) + separate hexagonal **cubes `302`** (menu items, FIG 8). [DECOMP/patent-sourced] Live measured
**16** slots in `0x375250`. [LIVE-VERIFIED] Leading reconciliation (user's): the array holds **12 dial rods (slots 0-11)
+ 4 menu cubes (slots 12-15)**, both sharing the SAME crystal-prism refraction shader, so they live in
one object array. 12+4 = 16, and slot 16 = end. Matches patent + measurement + the on-screen cubes. [HYPOTHESIS at time of writing; later confirmed as the settled model matching known-falsified item 8]
CAVEAT to confirm: slots 12-15 read as valid (rod-like positions) even in the PURE Visor where cubes are
not drawn — so if they are cubes they are allocated/persistent there. DECISIVE TEST (pending): diff
slots 12-15 between menu mode (cubes shown) and pure Visor — if they activate/change with the cubes,
confirmed. Until then: **dial = 12 rods** (use this for the rebuild), cubes = menu, both crystal prisms. [HYPOTHESIS/open — decisive test not reported as run in this file]

## ⭐⭐ Patent-grounded model (US6693606 digest §2-4) — the framework I should have used first

The patent (2nd embodiment = our clock) already describes ALL of this; the live RE just confirms it:
- **Rods `306`** = transparent radial PRISMS, longitudinal axis pointing OUT = the clock **DIAL**.
  Patent says **12** rods; live array measured **16** valid slots (reconcile — trace is authoritative
  per the digest, but recheck: 12 dial + 4? or stride). They spin as a **group** AND each about its
  **own axis** (S306-S308), rewriting all vertices per frame (= why positions/normals churn live). [DECOMP/patent-sourced for the "12 rods" and spin description; LIVE-VERIFIED for the 16-slot measurement; reconciliation itself was open at time of writing — see settled section above / known-falsified item 8]
- **`306a` = the single COLORED rod = the HOUR** (which dial position is colored). At 00:00 → top/12.
  This is the user's "rod fills as a pointer" — it's the patent's hour read-out. [DECOMP/patent-sourced claim; cross-referenced against LIVE-VERIFIED freeze-mode observation above]
- **Minutes+seconds = the AMOUNT OF COLORING** along `306a` (a PARTIAL fill of a vertex RANGE; 100% at
  0 m/s, decreasing to ~0 near rollover). The fill is per-VERTEX COLOR data, NOT a flag — so it is NOT
  `+0x150` (that's front/back, 10/6 split) and NOT any single field. Analog read-out via colour range. [HYPOTHESIS — patent-sourced inference, not independently live-confirmed for this specific field mapping]
- **AM = blue, PM = red** (matches the blue morning screenshots). [HYPOTHESIS — patent-sourced, screenshot-consistent but not decisively tested]
- **Light spots `308`** = small points on tangled paths inside a central **wireframe sphere `310/312`**,
  drawn with **after-image trails** (S312). → these ARE the `0x34c830` 8-element array (`FUN_002354c8`)
  + the S-curve trails + the centre glow seen in the Visor. (Patent open Q: spot count — live = 8.) [DECOMP-SOURCED for the array; LIVE-VERIFIED for the 8-count/visual match]
- **Order:** rods (refraction+bump, feedback loop) → light spots (additive after-image) → blur (post). [DECOMP/patent-sourced]

### My over-reaches this session, seen through the patent
- ~~"8 rods"~~ — no; 8 = the light spots (patent `308`). Rods are the dial (12/16). **[FALSIFIED → 12-rod dial + 4 menu cubes, per known-falsified item 8]**
- "ring is in the `+0x140` normals" — no; `+0x140` is a surface normal. The dial angle = even radial
  layout (a clock dial) + group/own-axis spin (patent S306-308), rewritten per frame. [LIVE-VERIFIED for the normal finding; layout claim is HYPOTHESIS]
- "`+0x150` = fill" — no; fill = colour-vertex range on `306a` (patent S304-305). `+0x150` = front/back. [DECOMP-SOURCED for `+0x150` = front/back, cross-checked against `rod-pipeline.md §4`; fill-location claim is HYPOTHESIS]
All three were re-derived blind from memory instead of grounded in the patent digest I already had.

## Rebuild stance (honest, patent-grounded)
- Rods = **a radial prism dial** (evenly spaced; 12 or 16 — reconcile), each prism axis pointing out,
  spinning as group + own-axis. ONE rod coloured = hour; partial colour fill = min/sec; AM blue/PM red. [HYPOTHESIS — this stance predates the later "COUNT = 12 RODS (settled)" section; the 12-vs-16 ambiguity here is resolved above per known-falsified item 8]
- Light spots = 8 points + trails inside a central sphere. [LIVE-VERIFIED]
- This is the model to build (geometry now well-understood); exact spin rates + even-spacing validate
  against the reference + the live array. [HYPOTHESIS/narrative]

## (superseded) Rebuild stance

- ~~CONFIRMED for the rebuild: **16 rods, radiating from a shared centre** (centre → screen centre).~~ **[FALSIFIED → 12-rod dial + 4 menu cubes, per known-falsified item 8; explicitly marked superseded in this doc's own heading]**
- HYPOTHESIS to validate (not confirmed): the ~~16~~ bars are **evenly spaced (22.5° apart)** in the clock
  plane — the natural default for a ~~16~~-spoke clock, consistent with the screenshot. Build it, render,
  and check the ring against the reference; if uneven, isolate the real per-rod angle from the render's
  rotation inputs. [HYPOTHESIS, as labeled; superseded count per known-falsified item 8 — a 12-spoke dial implies 30° spacing, not 22.5°]
