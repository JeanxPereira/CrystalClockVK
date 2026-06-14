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

## KEY MODEL — it's a 16-bar STARBURST, the ring is in the ORIENTATIONS

All 16 rods have **nearly the same origin** (~world (24, -18, 78), ±2) and project to ~the **same
screen point** (~2207, 1989 GS px = the clock centre). They do NOT sit on distinct ring positions.
The radial CLOCK shape comes from each rod being a **prism bar oriented along its own `+0x140` normal**
(16 distinct unit directions). So:
- origin ≈ common centre (small per-rod jitter).
- per-rod orientation = the `+0x140` normal → the 16 radial directions of the starburst.
- the bar mesh is swept along that direction (drawn via the rotation matrix from the rod's angle).

This **replaces the W1 RodField guess** (8 rods on a flat XZ circle of distinct positions). The correct
rebuild: 16 bars from a shared centre, each rotated to its radial direction; the projection maps the
shared centre to the screen centre.

## Live rod-origin samples (one frame; rods spin, so absolute values rotate)

rod0 world (25.1, -19.56, 80.39) → screen (2207.9, 1989.4) / 12.4 `0x89fe,0x7c57`. Origins of all 16
cluster ~(23-25, -17..-20, 76-80); orientation is what differs (the `+0x140` normals).

## Next

- Extract the 16 `+0x140` normals → the exact radial-direction distribution (even 22.5° spacing? or
  clock-hour weighted?). Then RodField = 16 bars at those orientations.
- The shared-centre origin + screen centre give a clean projection anchor (re-fit fov/camera so the
  centre lands at the screen centre; the bars then radiate correctly — fixes the W1 off-screen render).
