# Transitions of the clock scene: boot, main menu, System Configuration

Recorded 2026-10-02. Builds: **HDD OSD 1.10U** and **ROM 2.30**.
*Measured* = read from a running build through a trace probe. *Verified* = recomputed from the
probed inputs and equal bit for bit (`References/scripts/verify_transitions.mjs`,
`verify_trail_fill.mjs`, and the existing `verify_orbs.mjs`, `verify_approach.mjs`,
`verify_scale.mjs`, `verify_clock_state.mjs`, and for the flow items `verify_exits.mjs`,
`verify_rand.mjs`, `verify_options.mjs`). *Read* = disassembly only.
Captures: `Watson/Runtime/captures/hddosd-110U-trans-*` and `rom-0230A-trans-*`.
`node verify_transitions.mjs <trace> --timeline` prints the timeline of a capture.

The functions below differ between the builds only in data addresses (`diff_rom.mjs`:
`func_00234EA8` 0 words; `func_00234B88`, `func_00234C28`, `func_00234CD0`, `func_00234D18`,
`module_clock_234E70`, `module_clock_22F908`, `func_0022F078`, `func_0022F110`, `func_002320B0`
only address words). `StartSysConfig` has one more call and another sound number on ROM 2.30.

| What | HDD OSD 1.10U | ROM 2.30 |
|---|---|---|
| clock thread (set-up, loop, exit) | `clock_input_check_handler_p6_p7_tgt` `0x002259B8` | `0x00221060` |
| set-up of the scene | `module_clock_init_resources` `0x00225DB0` | `0x00221488` |
| one frame | `clock_orb_rendering_func` `0x00225E80` | `0x00221558` |
| reset mode and weight | `func_00234B88` | `0x00231190` |
| set the mode | `func_00234C28` | `0x00231230` |
| step the weight | `func_00234EA8` | `0x002314b0` |
| tick the band ramp, draw the overlay | `module_clock_234E70` | `0x00231478` |
| one orb: position, colour, ring, draw | `module_clock_22F908` | `0x0022b928` |
| open System Configuration | `StartSysConfig` `0x00230EC0` | `0x0022d040` |
| System Configuration per frame (closing schedule) | `func_00230FD8` | `0x0022d160` |
| rods on / rods off | `func_0022F078` / `func_0022F110` | `0x0022b098` / `0x0022b130` |
| main menu items appear | `func_002320B0` | `0x0022e320` |
| mode / weight | `0x00370AB4` / `0x00370AB8` | `0x002c8f6c` / `0x002c8f70` |
| band ramp / overlay record | `0x002B5F20` / `0x002B5F30` | `0x00297120` / `0x00297130` |
| rods ramp | `0x002B5640` | `0x00296600` |
| System Configuration ramp | `0x002B2E04` | `0x0028b00c` |
| background grey ramp | `0x002B5CD0` | `0x00296c90` |
| main menu items ramp | `0x002B2E78` | `0x0028b070` |
| sprite fade ramp | `0x002B61B0` | `0x00297410` |
| per-orb random angles | `0x00405210` | `0x003711e0` |
| previous module | `0x001F064C` | `0x001F05EC` |
| screen code | `0x00370A7C` | `0x002c8f1c` |
| camera offset | `0x00370A80` | `0x002c8f20` |

A ramp is four ints: length, value, changed, state (0 idle at 0, 1 rising, 2 held at the
length, 3 falling). One tick moves the value one unit.

## 1. What drives each stage, and who writes it

| Stage | Variable | Rule (who writes it) |
|---|---|---|
| Fade from black | mode, weight | Set-up calls set-mode(2): mode 2, weight 0, overlay colour black. Every frame `func_00234EA8` adds 1 to the weight; when it would pass 128 it stays 128 and the mode becomes 0. The overlay's alpha is `128 - weight` (*read*: `func_00234E08`). |
| Camera approach | offset | Set-up writes -100; every frame `offset = offset * 0.97`, added to the camera's z (`clock-camera.md`). It restarts on every start of the clock thread. |
| Scene scale | scale, target | `func_00232640`. HDD OSD: 0 while the time record is not filled (the first frame of every thread start), then `scale += (target - scale) * 0.03`. ROM 2.30: `* 0.1`, never zeroed. |
| Orbs fly in | orb position | `module_clock_22F908`, mode 2: every orb if the previous module is 1 (the opening), else orb 0 only (section 3). |
| Orb colours | orb colour | Mode 2: each orb's own colour blended to the base colour by the weight (section 3). |
| Orb sprites fade in | sprite ramp | Set-up (`func_00238F20`) sets the length to `fps * 256 / 60` and starts it rising. It ticks once per orb call, so 7 per frame: full after 37 frames. |
| Trails grow | the rings | Set-up (`func_00239E60`) empties the rings. One point per 3 frames; full (49 points drawn) after 149 frames. |
| Band (the vignette) | band ramp | Length `fps * 80 / 60`. `func_00234EA8` starts it rising when, in mode 2, the weight reaches `128 - length`. Set-mode(3) and `StartSysConfig` start it falling; closing System Configuration starts it rising. Ticked by `module_clock_234E70`; drawn only in mode 0 (*read*). |
| Main menu items | menu items ramp | Length `fps / 6`. `func_002320B0` starts it rising when, in mode 2, the weight is `128 - fps / 6`. The menu takes input only while it is held. |
| Rods | rods ramp, appearance | Length 1. `func_0022F078` (from `StartSysConfig`) starts it rising, `func_0022F110` falling. Appearance = value / length (`clock-state.md`), so it is 0 or 1; the rods are submitted only while rod 0's appearance is above 0.05 (*read*: `module_clock_22FE98`, constant `D_0036FBE4`). |
| Background grey | background ramp | Length `fps * 40 / 60`. Started rising by `StartSysConfig` (`func_002331C8`), falling by the closing schedule. |
| System Configuration | its ramp | Length `D_003702E0 + D_003702CC + D_003702D0` = `fps*40/60` (`func_00234B88`) + `fps*40/60` + `fps/6` (both `func_002324C8`) = 40 + 40 + 10 = 90 (*read* in `StartSysConfig`, set on every opening; equal to the length measured). `StartSysConfig` starts it rising; Back starts it falling; `func_00230FD8` hangs the closing schedule on its value. |
| Leaving the clock | mode 3, weight | The thread calls set-mode(3): weight 128, falling 1 per frame; at 0 the thread ends and writes 2 to the previous-module word. |

At 60 fields a second the lengths are 80, 10, 256, 40 and 90; at 50 they are 66, 8, 213, 33
and 74 (33 + 33 + 8). The 50 field lengths are *measured* on PAL runs (`verify_transitions.mjs`,
`verify_orbs.mjs`, section 5). The weight (128 steps), the camera factor and the scale factor do
not depend on the field rate.

## 2. Timelines (measured; values at the start of each frame)

### Entering the clock scene: every start of the clock thread

`T0` is the first frame the clock draws. The schedule after `T0` is the same in every entry
measured, on both builds:

| Frame | What happens |
|---|---|
| `T0` | mode 2, weight 0 (overlay opaque black); camera offset -100; rings empty; sprite ramp rising (7 after the first frame); 7 orbs drawn. HDD OSD: scale 1 on entry, set to 0 in this frame. |
| `T0 + 1` | weight 1, then +1 per frame; offset -97, then x 0.97 per frame; HDD OSD scale 0, then eased by 0.03 toward 1 (0.656 at `T0 + 36`, 0.979 at `T0 + 128`) |
| `T0 + 36` | sprite ramp at its length (256) |
| `T0 + 47` | the weight step reaches 48: the band ramp is started; it rises 80 frames |
| `T0 + 118` | weight 118: the main menu items ramp is started; it rises 10 frames |
| `T0 + 128` | weight 128; band ramp held at 80 |
| `T0 + 129` | mode 0; menu items ramp held: **the menu takes input from here** |
| `T0 + 149` | the rings are full: trails at their 49 points |
| `T0 + 378` | camera offset below 0.001 |

| Entry | Build | `T0` | Previous module | Orbs displaced |
|---|---|---|---|---|
| power-on, `SkipSearchLater` | HDD OSD | 162 | 2 | orb 0 |
| power-on, `SkipSearchLater BootOpening` (through the opening) | HDD OSD | 410 | 1 | all 7 |
| back from the Browser (circle to dismiss its HDD error, circle again) | HDD OSD | 2105, 99 frames after the second press | 3 | orb 0 |
| power-on (opening, then first run: see section 4) | ROM 2.30 | 453 | 1 | all 7 (first frame only) |
| back from the Browser (circle) | ROM 2.30 | 93 frames after the press | 3 | orb 0 |

**The only thing the previous-module word changes in the clock's start is which orbs fly in**
(and, *read*, that the screen code is forced to 100, the main menu). Camera offset, scale,
fade, band, menu items and trails follow the same schedule in all entries. The word is written
by `opening_transition_to_clock` (1), the clock thread's exit (2), `browser_exit_previous_module`
(3 *measured*) and `main` (*read*).

In the main menu: the orbs and their trails, the band, no rods (rods ramp idle, appearance 0),
background grey 0.

### Main menu to System Configuration (cross on the second item, held from frame `P`)

Same on both builds (HDD OSD `P` = 1645, ROM 2.30 `P` = 2034).

| Frame | What happens |
|---|---|
| `P + 2` | `StartSysConfig` runs: rods ramp, System Configuration ramp and background ramp start rising, band ramp starts falling |
| `P + 3` | rods at appearance 1 and submitted (packets per frame 364 -> 731); band 80 falling; background 1 |
| `P + 42` | background grey at 40 |
| `P + 83` | band at 0, idle |
| `P + 93` | System Configuration ramp held at 90 |

Mode stays 0, weight 128, scale 1, camera unchanged. The clock state through it: the rods'
colours and progress do not change with the opening; only the appearance goes 0 to 1 in one
frame.

### System Configuration back to the main menu (circle, held from frame `P`)

Same on both builds (HDD OSD `P` = 1904, ROM 2.30 `P` = 2311). Triangle is Options there: it
makes the same ramp fall but with the dialog ramp busy, and then none of the steps below run
(section 4a).

| Frame | Ramp value | What happens |
|---|---|---|
| `P + 3` | 90, falling | |
| `P + 12` | reaches 80 (`fps*40/60 * 2`) | band ramp starts rising (80 frames) |
| `P + 52` | reaches 40 | background ramp starts falling (40 frames) |
| `P + 91` | reaches 1 (`D_003702E8`) | rods ramp falls: appearance 0, rods no longer submitted from `P + 92` |
| `P + 93` | 0, idle | band held at 80, background 0 |

*Read* (`func_00230FD8`): each step is taken only while the dialog ramp is idle; ten frames
into the fall (`length - value == fps / 6`) `func_002303C8` is called (menu side, not read).

### Main menu to the Browser: leaving the clock (cross on the first item, held from frame `P`)

Same on both builds (HDD OSD `P` = 1622, ROM 2.30 `P` = 2010).

| Frame | What happens |
|---|---|
| `P + 3` | screen code 9999; menu items ramp falling (10 frames); after this frame the thread calls set-mode(3): weight 128, band ramp falling (80 frames) |
| `P + 4` | mode 3, weight 128, then -1 per frame: overlay alpha rises to black; orb 0 moves out and changes colour (section 3) |
| `P + 131` | last frame of the clock (weight reaches 0 in it); the thread ends, previous module := 2 |

The sprite ramp and the scale do not move while leaving.

## 3. The transition arithmetic (verified)

### Weight step, `func_00234EA8`, once per frame

```
mode 1 or 2:  weight += 1;  if weight > 128: weight = 128, mode = 0
mode 3:       weight -= 1;  if weight < 0: weight = 0            (mode stays 3)
mode 0, 4:    nothing
then, if the mode at entry was 2 (and was not just ended) and weight == 128 - band length:
              start the band ramp rising if it is idle
```

### Set mode, `func_00234C28(mode)`

Stores the mode; writes the screen size x 16 into the overlay record; then

| Mode | Weight | Overlay colour | Also | Callers |
|---|---|---|---|---|
| 1 | 0 | 255, 255, 255 | | none in either build: 6 callers each, all with the constant 2, 3 or 4 (*read*, scan of `jal`/`j` and of the address as a data word) |
| 2 | 0 | 0, 0, 0 | | scene set-up; end of the first-run intro (*read*) |
| 3 | 128 | 0, 0, 0 | band ramp starts falling if held | clock thread when leaving |
| 4 | 0 | 0, 0, 0 | | first-run intro (ROM 2.30 `0x00229840` *measured*) |

### One orb in modes 2 and 3, `module_clock_22F908`

`k` is the orb, `(x, y)` its projected position relative to the screen centre
(`clock-camera.md`), `W` the weight, `R_k` the orb's random number, `screen` the screen size in
pixels. The orb is displaced when the mode is 2 and the previous module is 1 (every orb), when
the mode is 2 and `k == 0`, or when the mode is 3 and `k == 0`:

```
a    = (int16)(W * 128)                 a quarter turn at W = 128
rest = 1 - sin(a)                       the table sine of clock-camera.md
dx   = (screen width  / 2) * cos(R_k) * rest
dy   = (screen height / 2) * sin(R_k) * rest

mode 2:  x += dx                 y += dy
mode 3:  x = x * (W / 128) + 1.5 * dx      y = y * (W / 128) + 1.5 * dy
```

every operation in single precision, cut toward zero, in that order; the depth is untouched.
So in mode 2 an orb starts on an ellipse of the screen's half size at its random angle and
arrives at its orbit as the weight reaches 128; in mode 3 orb 0 goes from its orbit to one and
a half times that ellipse while its own position shrinks to the centre.

`R_k = rand() % 65536` (C remainder), drawn for the 7 orbs at every start of the clock thread
(`func_0022EF40`, *read*). `rand` is newlib's: `state = state * 0x41C64E6D + 12345`,
`rand = state & 0x7FFFFFFF`, the state at `_impure_ptr + 0x58`. *Verified*
(`verify_rand.mjs`; captures `hddosd-110U-flow-rand-boot.log`, `hddosd-110U-flow-rand-opening.log`,
`rom-0230A-flow-rand-boot.log`): 21 of 21 angles equal (HDD OSD 14, ROM 2.30 7). On a plain HDD OSD
power-on the state is 1 when the clock draws them, so the angles are always
`7EA6 B0E7 E494 9B3D DF32 7483 B600`; after the opening (whose lights call `rand`) and on the
ROM 2.30 power-on (which plays the opening) the state differs.

Colour handed to the ring, four ints, with `base = (48, 98, 128, 60)` and the per-orb table
`(0,0,128,60) (0,128,0,60) (0,128,128,60) (128,0,0,60) (128,0,68,60) (128,68,0,60) (128,128,128,60)`:

```
mode 2, every orb:   (own[k][i] * (128 - W) + base[i] * W) >> 7
mode 3, orb 0:       (own[0][i] * (128 - W) + base[i] * W) >> 7
otherwise:           base
```

### What was compared, and how much

| Check | HDD OSD 1.10U | ROM 2.30 |
|---|---|---|
| weight step: mode, weight, band start | 1 884 frames; modes 0, 2, 3 natural, 1 and 4 written in | 1 509 frames; modes 0, 2, 3, 4 natural, 1 written in |
| band ramp tick | 1 884 | 1 509 |
| set mode: mode, weight, overlay colour | mode 2 twice, mode 3 once | mode 2 twice, mode 3 once, mode 4 twice |
| orb position, mode 2, every orb | 903 (boot through the opening) | 7 natural + 903 with mode 2 written in the menu |
| orb position, mode 2, orb 0 | 258 (plain boot, back from the Browser) | 129 (back from the Browser) |
| orb position, mode 3, orb 0 | 128 | 128 |
| orb colour blend, mode 2 / mode 3 | 2 709 / 896 | 1 813 / 896 |
| orb position and colour, no transition | all others, every one equal | all others, every one equal |
| trail with the ring filling (`verify_trail_fill.mjs`) | 6 244 strips, 148 176 points, heads 0..49 | 4 172 strips, 98 784 points |
| sprite fade rising (`verify_orbs.mjs`) | 761 orb calls, every sprite equal | 512 orb calls |
| camera approach from -100 (`verify_approach.mjs`) | 3 entries, 1 354 steps | 2 entries, 861 steps |
| scale across the entry (`verify_scale.mjs`) | 3 entries, zeroed then eased by 0.03 | first run: eased to 0.8 by 0.1; re-entry: stays 1 |
| clock state across open and close (`verify_clock_state.mjs`) | 116 + 116 frames | 117 + 117 frames |
| per-orb random angles (`verify_rand.mjs`) | 14 of 14 | 7 of 7 |
| options dialog ramp (`verify_options.mjs`) | 77 + 77 ticks, every value equal | 77 + 77 ticks, every value equal |
| exits for the drive's codes (`verify_exits.mjs`) | 11 codes | 12 codes |
| PAL transitions (`verify_transitions.mjs` with `CLOCK_VIDEO=pal`) | power-on, open, close: FOUND | open, close: FOUND |
| PAL trail fill (`verify_trail_fill.mjs`) | 2 086 strips, 49 392 points | not run |

Every value compared is equal. `verify_orbs.mjs` prints PARTIAL on the entry captures only
because it looks for strips of two or more points and the first calls have none or one;
`verify_trail_fill.mjs` covers those (a filling ring gives `head - 1` points, down to an empty
strip).

Written in with the VM paused (stimulated, not natural): mode 1 on both builds (weight 0 to
128, then mode 0; orb colours stay the base colour), mode 4 on HDD OSD (25 frames), mode 2 in
the ROM 2.30 main menu (to get every-orb displacement over the whole weight range there).

## 4. The first-run path (ROM 2.30 from power-on, empty configuration)

*Measured*, capture `rom-0230A-trans-boot` (frames 443 to 949):

- `T0` = 453, previous module 1. The set-up sets mode 2 and the scale target is already 0.8;
  during the first frame set-mode(4) is called from `0x00229840`, and again at `T0 + 180`.
- Mode 4 for all 497 frames: weight stays 0, so the overlay stays opaque black over the scene.
  The orbs are still processed underneath: 7 per frame, rings fill, sprite ramp rises, camera
  approaches as in any entry.
- The scale eases from 1 to 0.8 by 0.1 per frame; no band, no menu items ramp.
- Only the 7 orb calls of frame 453 ran in mode 2 (every orb displaced, weight 0).

*Read* (HDD OSD; ROM 2.30 has the same six-stage machine at `0x00229778`, with an extra condition
on `0x001F00B0`): `main` calls `enable_enter_clock_module_208398` (writes 0 to
`should_enter_clock_module_2AD22C`) when `config_first()` reports no saved configuration or
`oobe_forced` is set; the clock's set-up `func_002324C8` then ends in `func_0022D760`: gate ramp
`D_002B46D0` shown, logo ramp `D_002B46F0` length fps, text ramp `D_002B46E0` length fps/2,
`D_002B4A34 = fps/2`, stage `D_00370264 = 0`, overlay `D_00370238 = 0x80`, scale target
`D_00370294 = D_0036FBC8 = 0.8`. Stages (table `0x00365710`): 0 PlayStation logo; 1 PS2 logo
(`oobe_load_image(res, x, y, w, h, alpha)`; each logo rises fps frames, holds fps, falls fps;
set-mode(4) when it starts); 2 the language prompt (text ramp; cross hides it; leaving it calls
set-mode(2)); 3 the User Preferences pages (page record `D_002B4A18`; overlay 0); 4 "Settings
completed" (cross); 5 the end (scale and target 1.0, gate hides, set-mode(2) unless an exit is
pending, the first-run flag switched off by `disable_enter_clock_module_208388`). The logo
rules are in `verify_first_run.mjs`.

Also *read* on HDD OSD (`scale-and-modes.md`): `func_0022D760` writes the 0.8 and `func_0022D828`
runs the intro states (two 180-frame cycles in mode 4, then the pages); its last state sets
scale and target to 1 and calls set-mode(2), which starts the normal fade. HDD OSD did not take
this path in any capture (`config_first()` is false there), so its side is read only.

## 4a. Options dialog of System Configuration (triangle)

*Verified* (`verify_options.mjs`; captures `hddosd-110U-flow-options-{open,close}`,
`rom-0230A-flow-options-{open,close}`). Triangle on a list entry pushes a dialog.

| | HDD OSD 1.10U | ROM 2.30 |
|---|---|---|
| push | `func_0022D558(page)` from `func_002316B8`; the page is chosen by the entry | `0x002288B8`; one fixed page |
| page title | the entry's name (for example "Clock Adjustment") | "Options" |
| dialog ramp | `D_002B46B8` | `0x00293BA8` |
| pages function ticking it, once a frame | `func_0022D5D8` | `0x00228918` |

The dialog ramp length is `D_003702D0 + D_003702CC` = 10 + 40 = 50 (PAL 8 + 33 = 41). The page
opens (`func_0022ABC8`) on the frame the rising value reaches `D_003702CC` (40); when the page's own
ramp falls, the dialog hides (`func_0022D5A0`). Compared on both builds: 77 + 77 ticks with every
ramp value equal (rising 5..50, falling 46..0), and the page opened on the computed frame.

`verify_frame.mjs` on the same captures: every packet the model produces is equal (HDD OSD 74 + 75
frames, 86.2% and 84.6% of the packets; ROM 2.30 75 + 75 frames, 92.6% and 90.0%; the rest is
text). With `--carry` the model's carried state differs in `entryActive` and in the
configuration-items ramp: the dialog is an event the model does not take yet (`clock-frame.md`).

## 4b. Exits of the clock thread for the drive's screen codes

*Verified* (`verify_exits.mjs`; logs `hddosd-110U-flow-exits.log`, `rom-0230A-flow-exits.log`).
With a code put in `s0` at the thread's decision (HDD OSD breakpoint `0x00225A4C`, ROM 2.30
`0x002210D4`), every code `0x6A..0x74` (HDD OSD) and `0x6A..0x75` (ROM 2.30) makes the thread
exit exactly 128 frames later at its end (HDD OSD `0x00225CD4`, ROM 2.30 `0x002213AC`), with the
writes the code selects. Code `0x72` does not exit while the word at HDD OSD `0x1F0D58` / ROM 2.30
`0x1F0CF8` is 0 (state as captured). Code 9999 (the Browser) is section 2.

## 4c. PAL

*Verified* with `CLOCK_VIDEO=pal`, no copy of the verifier needed (`pal.md`):

- `verify_transitions.mjs`: FOUND on `hddosd-110U-flow-pal-boot` (power-on), `-pal-open`,
  `-pal-close`; and `rom-0230E-flow-pal-open`, `-pal-close`. The lengths are 66, 8, 213, 33, 74.
- `verify_trail_fill.mjs`: 2 086 strips, 49 392 points on `hddosd-110U-flow-pal-boot`.
- `verify_orbs.mjs`: 924 strips, 1 848 sprites on `hddosd-110U-flow-pal-fade-up2` (ramp rising,
  178 calls, length 213).

## 5. Build differences seen here

| | HDD OSD 1.10U | ROM 2.30 |
|---|---|---|
| Scale on entering the clock | set to 0 on the first frame, then eased by 0.03 | not zeroed; factor 0.1 |
| `StartSysConfig` | | one more call (`0x00223910`) and sound `0x5200` instead of `0x6300` (*read*) |
| Returning from the Browser | the Browser shows an HDD error dialog first; circle twice | circle once |

The transition arithmetic and all schedules are the same.

## 6. Still only read, or not done

- The reset in `func_00234B88` (mode 0, weight 0) is never seen: it is called once, at
  `0x00225DD8` in `module_clock_init_resources`, and set-mode(2) at `0x00225E60` in the same
  function; none of the seven functions called between them reads the mode or the weight
  (*read*). What it sets and is seen: `D_003702E0 = fps*40/60`, the band ramp length
  `D_002B5F20 = D_003702E4 = fps*80/60`, `D_003702E8 = 1`.
- Set-mode(1) (white overlay): no caller exists in either build; only the weight step of mode 1
  was exercised, by writing the mode in.
- The first-run intro: `verify_first_run.mjs` holds the stage rules (section 4); its traces on
  HDD OSD (from a state made by writing 0 to `should_enter_clock_module_2AD22C` at the set-up
  breakpoint `0x002324C8`), on ROM 2.30 (ROM addresses in `verify_first_run.mjs`) and on PAL are
  not compared yet; ROM 2.30's later states (pages after frame 949) likewise.
- Code `0x72` with its gate word written to 1, on both builds.
- The PAL fade rising on ROM 2.30, and PAL hour turning with a time record written to hh:59:59.
- What the overlay, the band and the background ribbons draw, the clock-alone screen (ramp
  `D_002B5780`) and Clock Adjustment: other pages.
- A PAL written-state capture on ROM 2.30.
