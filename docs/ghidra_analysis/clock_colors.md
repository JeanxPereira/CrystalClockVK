# OSDSYS Clock & Config Colors — static extraction (hddosd.elf)

Extracted 2026-07-24 via Ghidra MCP (decompile) + inline Java quad-scan over
0x2A0000-0x2C8000. All values are int quads R,G,B,A in GS convention:
**128 = 1.0** (neutral) for vertex-color modulate and alpha. To convert for
our shaders use `value / 128.0`, NOT /255 (Font_SetColor confirms the
x0.0078125 = 1/128 scaling).

## Rod (crystal) colors — table base 0x2B5540

| Addr | RGBA | /128 | Element | Confidence |
|---|---|---|---|---|
| 0x2B5560 | 40,40,40,128 | 0.31,0.31,0.31 | selected-rod fill (idle gray) | high |
| 0x2B5600 | (live slot) | — | CURRENT glass tint, written by cycler | high |
| 0x2B5610/20 | 60,60,60,128 | 0.47 gray | rod specular (both passes) | high |
| 0x2B5630 | 128,128,128,30 | white, a=0.23 | likely Pass-2 additive edge tint | low |

### Glass tint color cycle (5 entries, step 16B, advances 1 slot / 8 frames, counter DAT_0037026C, sentinel a=-1 at 0x2B55C0)
| Slot | Addr | RGBA | /128 |
|---|---|---|---|
| 0 | 0x2B5570 | 45,85,102,128 | 0.35, 0.66, 0.80 (cool blue — matches live capture) |
| 1 | 0x2B5580 | 68,47,102,128 | 0.53, 0.37, 0.80 (purple) |
| 2 | 0x2B5590 | 44,71,96,128 | 0.34, 0.55, 0.75 (teal-blue) |
| 3 | 0x2B55A0 | 51,73,111,128 | 0.40, 0.57, 0.87 (blue) |
| 4 | 0x2B55B0 | 51,111,113,128 | 0.40, 0.87, 0.88 (teal) |

### Highlight/active-glow cycle (2 entries, same 8-frame cadence, counter DAT_00370274)
| Slot | Addr | RGBA | /128 |
|---|---|---|---|
| 0 | 0x2B55D0 | 202,246,231,128 | 1.58, 1.92, 1.80 (pale mint, >1.0 = overbright) |
| 1 | 0x2B55E0 | 187,235,255,128 | 1.46, 1.84, 1.99 (pale sky, overbright) |

Per-frame midpoint blend (base+highlight)/2 computed into DAT_00405200
(scratch, no external readers). Cyclers: FUN_0022ead0 (glass),
FUN_0022eb50 (highlight), composer FUN_0022ee20, specular calc FUN_0022e8c0.

## Orb colors — base 0x2B5650

| Addr | RGBA | Element | Confidence |
|---|---|---|---|
| 0x2B5650/60 | 128,128,128,128 | orb base = pure white (glow is procedural) | high |
| 0x2B5670 | 48,98,128,60 | orb state tint A (blue-violet, a=0.47) | medium |
| 0x2B5680 | 0,0,128,60 | state tint B (blue) | medium |
| 0x2B5690 | 0,128,0,60 | state tint C (green) | medium |
| 0x2B56A0 | 0,128,128,60 | state tint D (teal) | medium |
| 0x2B56B0 | 128,0,0,60 | state tint E (red) | medium |
| 0x2B56C0 | 128,0,68,60 | state tint F (pink) | medium |
| 0x2B56D0 | 128,68,0,60 | state tint G (orange) | medium |
| 0x2B56E0 | 128,128,128,60 | state tint H (white) | medium |

Blend: module_clock_22F908 lerps A<->B(+index) via
get_clock_should_render_orbs() 0..128; branch on FUN_00234b80() (2 or 3);
index via struct+0x130 (likely module_clock_get_config_item(0) mode).

## Text / UI colors

| Addr | RGBA | Element | Confidence |
|---|---|---|---|
| literal in FUN_00226300 | 96,96,96,a | clock date/time text (Font_SetColor) | high |
| 0x2B2460 | 96,96,96,128 | button-panel label text | high |
| 0x2B2540 | 30,110,156,128 | config menu item text state A (blue) | medium |
| 0x2B2550 | 44,44,44,128 | config menu text (dim/disabled?), no reader found | low |
| 0x2B2560 | 96,96,96,128 | config menu item text state B (gray) | medium |
| 0x2B2570 | 110,110,0,128 | config selected/header item (olive) | medium |

## Extra quads from the full sweep (unattributed yet)

| Addr | RGBA | Note |
|---|---|---|
| 0x2ACA80..B0 | 220,70,90,128 / 220,20,60,128 x2 / 220,70,90,128 | crimson table (4 entries) — outside clock module; candidate: warning/error text or boot UI |
| 0x2B21E0 | 55,40,60,128 | dark purple; sits in the clock camera-globals region (0x2B2190 pos) — candidate: clock background/clear tint |
| 0x2B5750 | 0,150,200,128 | bright cyan-blue, clock region — candidate: trail/ribbon head (live capture head was rgb 48,98,128) |
| 0x2B5760 | 100,100,100,128 | mid gray, clock region |
| 0x2B61F0 | 255,255,255,128 | overbright white |

## Config/browser crystalline cube: NOT in the ELF

The save-icon cube's lighting/ambient colors come from each save's
`icon.sys` file (parsed by browser_handle_icon_sys_binary into
iconinfo+0x480..0x528: ambient + light1/2/3 dir+color) — per-file data,
not binary constants. To get the exact cube look, read the icon.sys of
the save being displayed (or PCSX2 live-capture the parsed struct).

## Function anchors
Font_SetColor 0x212870 (proves /128 scale) · cyclers 0x22ead0/0x22eb50 ·
composer 0x22ee20 · specular 0x22e8c0 · orb init 0x22ef40 · orb blend
0x22f908 · clock text 0x226300 · config menu 0x22af60.
