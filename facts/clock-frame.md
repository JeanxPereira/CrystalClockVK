# End to end: the clock's frame from its inputs

Builds: **HDD OSD 1.10U** (canon) and **ROM 2.30**; NTSC and PAL (`CLOCK_VIDEO=pal`; ROM 2.30 PAL
is the 2.30 E image). The model takes the build (`CLOCK_BUILD=hdd|rom`) and has one branch per
build difference, listed below.

## What it produces

`References/model/clock_frame.mjs` exports `frame({ memory, events }, mesh)`: from the clock's
state at the entry of the frame function (HDD `0x00225E80`, ROM `0x00221558`) it returns, in
order, every packet the frame function sends except text, each as a list of register writes, and
it leaves in `memory` the state the next frame starts from. It reads nothing from the packets.
The mesh is `data/rod-mesh.json`; the sine table is the formula of `clock-camera.md`.

`References/scripts/verify_frame.mjs` reads the snapshot from probes, runs the model and compares
every write of every packet with what the console sent (*verified*, "Result"). With `--carry` it
runs every frame after the first from the state the model left, taking from the trace only the
inputs listed under "What the model takes as input".

### The model's files

| File | Holds |
|---|---|
| `model/clock_memory.mjs` | the state as named pieces of EE memory with their address in each build (`LAYOUT`), merged into probe ranges; `Memory`, read and written in place |
| `model/clock_math.mjs` | single-precision operations cut toward zero (`add`, `sub`: the double sum with the lost part recovered, cut toward zero, denormals flushed), the sine table, the matrix routines, the ramp tick |
| `model/clock_camera.mjs` | HDD `module_clock_225F38`: the screen matrix, the view matrix with libvu0's arithmetic, the approach offset's decay |
| `model/clock_rest.mjs` | head of the frame (clear, background tube, blur trips, copies, tint), vignette and fade, the trips after the rods, bars, column; the overlay level's state machine, the blur level, the menu ramp's step |
| `model/clock_frame.mjs` | rods, orbs and extra passes, the GS state helpers, the orbs' motion and colours in overlay modes 2 and 3, and the frame's order |
| `model/clock_cubes.mjs` | the cubes of System Configuration: ramp, list step (ring; ROM 2.30's standing cubes), the pass, every send of a cube and of the highlight layer, the buffers put together |
| `model/clock_logic.mjs` | HDD `func_0022F1A0` (appearance ramp, time to angles, colours, progress) and the first half of `func_00232640` (spin, scene scale) |
| `model/clock_menus.mjs` | the menus' code that touches the clock: main menu, System Configuration, Clock Adjustment, the thread's step between frames (`between()`) |
| `model/clock_date.mjs` | the date library that Clock Adjustment's date check calls |
| `model/ee_libm.mjs` | `cosf` as the C library computes it, with the EE's rounding (`model/check_cosf.mjs` checks it) |

## The snapshot

`verify_frame.mjs` reads the pieces of `clock_memory.mjs`'s `LAYOUT` with probes on the frame
function's first instructions (eight ranges to a probe; 13 ranges on HDD OSD and 13 on ROM 2.30
for the clock's own pieces), at the entry of each part the frame function calls (where that
part's packets start), and, in two probes at the bars function's entry, the state when the pages
function has returned (what the menus changed in the frame). Besides the clock's own variables the
pieces are data the program never writes (constants, the rectangle records' fixed fields, the
cubes' two colours) and values set once at start (the display environments, the cubes' two
matrices, ramp lengths, the proportions). A capture taken with only two probes (the frame
function at entry; rods and orbs at entry: the view and screen matrices, the state block, the rod
template, the seven orb rings, the screen size, the scene record) is read for the rods, orbs and
extra passes only.

The checks of one frame:

- every packet the model produces equals the packet sent at the same place, write by write;
- the two matrices the model computed equal the ones the rods were handed;
- each part starts at the packet where the model has it;
- with `--carry`, every piece of carried state equals the next frame's snapshot.

## Order of a frame (*read at HDD `0x00225E80` / ROM `0x00221558`, verified by the results*)

A frame is drawn with the state the previous frame's logic left: the drawing functions run first
and the clock logic (ROM `0x0022b1c0`) and the scale (ROM `0x0022e910`) after them.

```
camera            screen matrix, view matrix (position z + approach offset), offset *= factor
head              display with the clear; background (grey ramp tick; 16 strips when the overlay
                  mode is 0); blur trips (level < 6 ? level : 10 - level); copy to work buffer 0;
                  copy to work buffer 1; tint back onto the display
rods              rods, orbs, the two extra passes
overlay           vignette ramp tick; vignette when the mode is 0 and the ramp is not idle; fade
pages             trips after the rods (level - 5 from level 5 up; ROM 2.30 always calls the blur
                  function, which ends with one more "blend 1, depth test GREATER")
                  cubes: ramp tick, list step, the pass                       (System Configuration)
                  menu ramp step (HDD module_clock_230DF0)
                  menus' code (clock_menus.mjs)
                  [menus and their text: not produced]
bars              the two letterbox bars when configuration item 0 is 0 or 2
[date and time: not produced]   [button hint: not produced]
column            two pixels at the right edge
state only        clock logic; spin + 30, scene scale; blur level; frame counter + 1; overlay step
```

ROM 2.30 addresses of the entry sequence: matrices `0x00221610`, background `0x002216d8`, rods,
orbs and extra passes `0x0022beb8`, five more drawing functions, clock logic `0x0022b1c0`, scale
`0x0022e910`.

The 2 packets sent between the frame function's return and the next entry (the display buffer
swap, from `0x0020C000` and `0x002892E8` on HDD OSD) are not the clock's.

## Per send (both builds)

`buffer(n)` binds a work buffer as texture, `work(target)` and `display` choose where to draw,
`blend(mode, ztst)`, `bind(texture, blended, ztst)`; the helpers are those of `clock-gs-state.md`.

A rod:

```
buffer(0), work(1, field), blend(1, 1)      refracted, far-side faces, add 0        -> 0x118
bind(2, 1, 2), blend(2, 2)                  grain, far-side faces
blend(0, 2)                                 grain with the rod's pair, far-side faces
buffer(1), display(field), blend(1, 2)      refracted, near-side faces, add 0       -> the frame
buffer(1), work(0, field), blend(1, 2)      refracted, near-side faces, add 255     -> 0x0D2
```

An orb: `display(field), blend(0, 3)`, header, trail, `bind(7, 1, 1)`, glow, `bind(6, 1, 1)`,
disc; then `work(0, field), blend(0, 3)` and the same again with the second set of colours.

An extra pass: `work(1, clear (0, 0, 0, 128), field)`; per rod, whole:
`bind(0, 0, 2), blend(1, 1)`, reflection, `bind(pass ? 2 : 3, 1, 2), blend(2, 1)`, grain; split:
`blend(0, 1), bind(0, 0, 2)`, reflection, `bind(pass ? 3 : 2, 1, 2), blend(2, 1)`, grain; then
`buffer(1), display(no field), blend(0, 1)` and the full-screen rectangle with alpha 30.

The cubes' sends (System Configuration), *read* in HDD `module_clock_237350`,
`module_clock_237860`, `module_clock_2308F0` and ROM `0x00233928`, `0x00233DD8`, `0x0022C8D0`:
per cube `bind the frame`, eight state changes, the half-buffer sprite; per layer cube the
buffer, `TEXCREFA` and two blends; then buffer 1 added to buffer 0, the blur chain, buffer 0 onto
the display. Verified as part of the carried results.

## Rules the model implements

Each is *read*; the carried results are equal only with it.

**Rods and orbs**

- **A split rod's grain is one face to a packet on ROM 2.30** (`0x002342d8` open, `0x00234314`
  emit, `0x0023431c` send, and the same at `0x0023437c`, `0x00234428`, `0x002344cc`). HDD OSD
  sends both pieces' faces in one send. The contents are the same.
- **The trail header's alpha goes through the sprites' fade ramp** (`clock-orbs.md`).
- **The refracted emitter's `g`** is the cosine routine at `angle = (int16)((1 - F) x 32768 / 0.1)`,
  and `bright = (int)(bright x ((1 - cos) x 0.5))` (ROM `0x00232a68..0x00232ac4`); 54 faces with
  `F` above 0.99 in `rom-0230A-clock-frame` and `rom-0230A-clock-frame-adjust` (sent with
  `PRIM 0x114`), all equal.
- **What a rod's node holds beyond the template**: number, matrix, Y scale, base colour `+0x80`,
  reflection colour `+0xC0`, strength `+0x90` (200 for the current rod, 160 for the others), `t`
  and `n`; the accent chaser (state `+0x250`) as piece A's colour; the fourth colour (state
  `+0x260`) as the reflection colour of both pieces in the extra passes. An orb's node is the
  template as the last rod left it, with only the matrix replaced.
- **Two words the program copies but never sets:** the fourth word of an orb ring entry's
  position (`module_clock_237300` writes x, y, z; `module_clock_239E98` copies sixteen bytes) and
  the rod record's two matrix pointers, which are the addresses of the frame function's locals
  (`module_clock_22FE98`, `0x0022FEA8`, `0x0022FEB0`). The carried state is compared without them.

**Floating point**

- **The EE's FPU has no denormals.** The camera's approach offset, multiplied by its factor every
  frame, goes from the smallest normal number to minus zero (`rom-0230E-pal-clock-b`, frame 3395:
  real `0x80000000`). The model's single-precision step returns a signed zero below the smallest
  normal number.
- **`cosf` is reproduced, not taken as measured.** `model/ee_libm.mjs` is the C library's `cosf`
  (`0x00294B28`, `__kernel_cosf` `0x00296CF8`, `__kernel_sinf` `0x002977A0`,
  `__ieee754_rem_pio2f` `0x00295850`, table `npio2_hw` at `0x0036EB00` read from the file) with
  every operation cut toward zero: 3 213 calls of the cube placement in six captures, 144
  distinct arguments, every result equal bit for bit (*verified*, `model/check_cosf.mjs`).
- **Every single-precision sum is the exact sum cut toward zero** (`add`, `sub` in
  `clock_math.mjs`); the 169 sums of the model use them.

**Cubes and menus**

- **The cubes use neither the camera's position nor its rotation.** The cube record's two matrix
  pointers (`+0x64`, `+0x60`) point at `0x004090F0` and `0x004090B0` (ROM `0x003750C0`,
  `0x00375080`), which `func_002324C8` fills once: `sceVu0UnitMatrix` and
  `sceVu0ViewScreenMatrix(512, ax, ay, 2048, 2048, 1, D_0036FC0C, 1, 65536)` (`0x00232528`,
  `0x00232534`). The view handed to the transform is the unit matrix (*measured* in all 110 cube
  transforms of `hddosd-110U-cubes-b`; the model's cubes are equal only with it).
- **Who writes the cubes' spin:** the first instructions of HDD `func_00232640` (ROM
  `0x0022E938`): `spin += 0x1E`, 16 bits.
- **The menu ramp's step** (HDD `module_clock_230DF0` / `func_00230D58`, ROM `0x0022CF70`): the
  ramp ticks; while rising, at counter = tail length (`D_003702D0`) the cubes' ramp is sent down;
  when full, square (pad bit `0x80` of `0x00370334`) sends it down and starts the cubes' ramp
  (ROM 2.30 chooses standing cubes or ring there, `0x0022C330`). One event carries the blur level
  and the cubes through the System Configuration to clock transition.
- **The menu transition's blur chain** (`func_00236350`, `func_00236058`, `func_00236110`) is the
  cubes' chain; 1 to 5 trips in `*-whole-to-clock` (390 + 390 packets equal on HDD OSD).
- **The main menu** (`func_00232408`, ROM `0x0022E6E8`): its ramp, appearing at
  `weight = 128 - tail` in mode 2 and leaving at weight 128 in mode 3 (`func_002320B0`); the gate
  "no other page in front" (`func_00232020`: page, version, dialog and first-run ramps hidden and
  `D_003701D0` = 0); up, down, cross (Browser or `StartSysConfig`, ROM `0x0022D040`, which first
  rebuilds the list, `0x00223710`).
- **System Configuration** (`module_clock_231E48`, ROM `0x0022E0B8`): its ramp; the schedule
  (`func_00230FD8`, ROM `0x0022D160`); the selected entry's glow counter (+0x34); the input
  (`func_00231C50`, `func_002316B8`, `func_00231B38`; ROM `0x0022DFD8`, `0x0022DC00`,
  `0x0022DE80`): list moves (`func_00230860`), cross into an entry, square (clock alone), circle
  (close), confirm with the pulse, cancel. ROM 2.30: moves only with the ring, the standing
  cube's pulse, the wait on the drive write (`0x001F00A4`, `0x001F00B0`).
- **The entries' callbacks that touch the clock** are Clock Adjustment's only (every call of every
  callback followed). Opening (`D_00226FD0`, ROM `0x00222760`): field order by date format
  (`func_00226E68`), scale target 0, sprites hidden, seconds item 0, the time record from the
  items (`func_002358F8`), the date check. Confirm and cancel: scale target 1, sprites shown.
  Every frame inside it (`D_00227AD0`, ROM `0x00223258`): the field editor (`func_00227940`, ROM
  `0x002230A8`) and the time from the items.
- **The date check** (`func_00227488`, ROM `0x00222BF0`) and the date library it calls
  (`clock_date.mjs`): date to seconds `func_002149D8`, back `some_sort_of_lut_calc`, zone
  `func_00214728` with the base city 0x33 = +540 min read from the table, days of a month
  `func_002357D0`.
- **The thread's step** (`clock_input_check_handler_p6_p7_tgt` from `0x00225A28`, ROM
  `0x00221060`): main menu falling and changed gives screen code 9999 and the leaving flag; the
  flag with the first-run ramp hidden and mode 0 gives set-mode(3) (`func_00234C28`).
- **The display block is written between frames**: the buffer swap sets the next field's offset
  in the environment. Every use writes the offset first, so nothing sent depends on it.

**Build differences in the cubes and the buffer helpers**

- Before a cube's depth quads HDD OSD calls blend 0 with depth test 2 (`0x00237604`), ROM 2.30
  with 3 (`0x00233BB0`).
- ROM 2.30's buffer helpers end with one more "blend 1, depth test GREATER": the blur function
  and the copy function (`clock-frame-rest.md`) and the cubes' blur chain (`0x002327B8`, at
  `0x002328D0`, also with no trip). The add and half-buffer sprites do not.
- ROM 2.30's frame-texture binder: `TEST 0x70000` at `0x002306AC`, `CLAMP 0` at `0x002306B0`.
- ROM 2.30's texture binder writes `TEST_1 = 0x30000 | ztst << 17` (`clock-gs-state.md`).

**PAL**

- ROM 2.30 in PAL draws the vignette taller: its vertical term is multiplied by 1.15 (constant
  at `0x002C81D8`) when the video mode is PAL (`0x0022FEE4..0x0022FF00`). HDD OSD's
  `func_002338E0` has no such step, so the radii are unchanged in HDD OSD PAL. Verified by
  `rom-0230E-pal-whole-menu`. ROM 2.30's cached video mode is the word at `0x0027B380` (read at
  `0x002052D0`; 2 is PAL).
- `ZBUF_1` follows the screen size (`ceil(W/64) x ceil(H/32) x 2`).

## Where the model branches on the build

All *measured* on both, by the model passing on each with its own branch:

| | HDD OSD 1.10U | ROM 2.30 |
|---|---|---|
| Refracted face header | `PRIM`, `CLAMP_1` = region clamp to `0..W-1`, `0..H-1`, `RGBAQ` | `PRIM`, `CLAMP_1 0x1000000` twice, `RGBAQ` |
| Refracted lookup | 12.4 integers clamped to the screen | 1024 and 256 added, floor at 1024 |
| A split rod's grain | both pieces' faces in one packet | one face to a packet |
| Texture binder | `TEST_1 0x10000 \| ztst << 17`, `CLAMP_1` 5 | `TEST_1 0x30000 \| ztst << 17`, `CLAMP_1` 0 |
| Work buffer binder | `TEST_1 0x50000`, `TEXFLUSH`, `CLAMP_1` = region clamp | `TEST_1 0x70000`, register `0x7F`, `CLAMP_1` 5 |
| Display block | `0x001F0A70`, index at `+0x230`, screen size at `+0x244` | `0x001F0A10`, index at `+0x230`, screen size at `+0x240` |

The grain's `CLAMP_1 0x1000000` (repeat) is in the textured emitter's own face header on both
builds: `PRIM 0x54`, then `CLAMP_1 0x1000000` (`clock-gs-state.md` has its writer).

## Result (*verified*)

Rods, orbs and extra passes, from one snapshot per frame (`verify_frame.mjs`, no `--carry`).
Every write equal: `PRIM`, `RGBAQ`, `ST`, `UV`, `XYZF2`, `CLAMP_1` of the vertex packets, and
`TEST_1`, `ALPHA_1`, `TEX0_1`, `TEX1_1`, `CLAMP_1`, `TEXA`, `FRAME_1`, `ZBUF_1`, `XYOFFSET_1`,
`SCISSOR_1` and the rest of the state packets. Packets are compared in order, so the depth-sorted
draw order of twelve rods and seven orbs is part of what agrees.

| Capture | Frames | What happens in them | Vertex packets | State packets |
|---|---|---|---|---|
| `hddosd-110U-clock-frame` | 14 | HDD OSD, the clock alone; both fields, both display buffers | 2 744 of 2 744 | 4 256 of 4 256 |
| `rom-0230A-clock-frame` | 9 | the clock alone; rod 5 split at `t` 0.265; both fields, both display buffers | 1 962 of 1 962 | 2 736 of 2 736 |
| `rom-0230A-clock-frame-adjust` | 29 | Clock Adjustment, the hour turned from 5 to 6 with the pad: the ring jumps, the new rod's `t` climbs from 0.004, the scene scale is near 0, the sprites' fade is at 0, a ring's head wraps from 49 to 0 | 6 264 of 6 264 | 8 816 of 8 816 |

The whole frame: `CLOCK_BUILD=hdd|rom node References/scripts/verify_frame.mjs <trace> --carry`.
"Carried" = every frame after the first runs from the state the model left. Coverage = packets
produced and equal out of all packets between two entries of the frame function. Every line is
`verdict: FOUND N frames: every packet the model produces equal`, with `matrices computed equal
to the ones handed to the rods: N of N`, `parts starting where the model has them: 8N of 8N` and
`state the model carried, against each frame's snapshot: every piece equal in every frame`.

**HDD OSD 1.10U**

| Capture | What | Frames | Produced and equal, of the frame's packets | Events | Carried |
|---|---|---|---|---|---|
| `hddosd-110U-whole-clock` | clock alone | 16 | 8 992 of 9 616 (93.5%) | none | FOUND |
| `hddosd-110U-whole-long` | clock alone | 65 | 36 530 of 39 065 (93.5%) | none | FOUND |
| `hddosd-110U-whole-menu` | main menu (vignette on, rods hidden) | 13 | 3 666 of 4 732 (77.5%) | none | FOUND |
| `hddosd-110U-whole-config` | System Configuration, six cubes on the ring | 12 | 9 984 of 12 348 (80.9%) | none | FOUND |
| `hddosd-110U-whole-to-clock` | System Configuration to the clock alone (square held): blur 10 to 0, cubes shrinking, the list leaving | 49 | 40 768 of 47 287 (86.2%) | menuRamp (1 frame) | FOUND |
| `hddosd-110U-whole-enter` | main menu into System Configuration (down, cross held): vignette falling, grey rising, rods on, cubes growing | 75 | 53 814 of 59 879 (89.9%) | greyRamp, vignetteRamp, appearance, cubeRamp (2 frames) | FOUND |
| `hddosd-110U-whole-boot` | power-on, the clock's first 57 frames: overlay mode 2, orb 0 flying in, rings filling, camera approach, scale held at 0 | 57 | 13 117 of 14 610 (89.8%) | none | FOUND |
| `hddosd-110U-whole-boot-opening` | power-on through the opening (`BootOpening`), the clock's first 49 frames: all seven orbs flying in | 49 | 11 269 of 12 554 (89.8%) | none | FOUND |
| `hddosd-110U-pal-clock-frame` | PAL: clock alone | 15 | 8 430 of 9 015 (93.5%) | none | FOUND |
| `hddosd-110U-pal-config-frame` | PAL: System Configuration | 12 | 9 984 of 12 348 (80.9%) | none | FOUND |
| `hddosd-110U-pal-whole-to-clock` | PAL: System Configuration to the clock alone | 49 | 38 878 of 44 712 (87.0%) | menuRamp (1 frame) | FOUND |
| `hddosd-110U-whole2-enter` | main menu into System Configuration, with the menus' code modelled | 75 | 89.9% | none | FOUND |
| `hddosd-110U-whole2-adjust-open` | Clock Adjustment entered: fade down, scale 0, time from the items | 49 | 85.7% | none | FOUND |
| `hddosd-110U-whole2-adjust-hour` | inside Clock Adjustment, cursor moved, hour held up | 45 | 85.8% | none | FOUND |
| `hddosd-110U-whole2-leave` | main menu to Browser: mode 3, orb 0 leaving, thread ends | 130 | 89.1% | none | FOUND |

**ROM 2.30** (PAL: the 2.30 E image)

| Capture | What | Frames | Produced and equal, of the frame's packets | Events | Carried |
|---|---|---|---|---|---|
| `rom-0230A-whole-clock` | clock alone | 16 | 10 208 of 10 832 (94.2%) | none | FOUND |
| `rom-0230A-whole-long` | clock alone | 65 | 41 502 of 44 037 (94.2%) | none | FOUND |
| `rom-0230A-whole-menu` | main menu | 13 | 3 718 of 4 784 (77.7%) | none | FOUND |
| `rom-0230A-whole-config` | System Configuration, five standing cubes | 13 | 11 189 of 13 334 (83.9%) | none | FOUND |
| `rom-0230A-whole-to-clock` | System Configuration to the clock alone | 49 | 42 221 of 44 637 (94.6%) | menuRamp (1 frame) | FOUND |
| `rom-0230A-whole-enter` | main menu into System Configuration | 75 | 55 736 of 59 894 (93.1%) | greyRamp, vignetteRamp, appearance, menuList, cubeRamp, cubeMode (3 frames) | FOUND |
| `rom-0230E-pal-clock-b` | PAL: clock alone | 16 | 10 272 of 10 896 (94.3%) | none | FOUND |
| `rom-0230E-pal-whole-config` | PAL: System Configuration | 13 | 11 243 of 13 388 (84.0%) | none | FOUND |
| `rom-0230E-pal-whole-menu` | PAL: main menu (vignette) | 17 | 4 862 of 6 256 (77.7%) | none | FOUND |
| `rom-0230A-whole2-down` | System Configuration, down (standing cubes) | 45 | 87.1% | none | FOUND |
| `rom-0230A-whole2-to-clock` | System Configuration to the clock alone (square) | 49 | 94.6% | none | FOUND |

The six `whole2` rows give coverage only; their packet counts are not tabulated here.

### Events in the carried runs

"Events" lists the pieces whose value the model took from the trace: in that frame the model's
own value differed from the console's after the pages function and was replaced by it
(`verify_frame.mjs`, `real.copy(mine)`). In those frames the model reads the answer for that
piece; an event is not an external input.

- In the first 20 captures (689 frames; run before the menus' code was modelled), 669 frames run
  from carried state and **8 of them carry an event** (13 piece-events): `hddosd-110U-whole-to-clock`
  1 frame, `hddosd-110U-pal-whole-to-clock` 1, `rom-0230A-whole-to-clock` 1, `hddosd-110U-whole-enter`
  2 (1641, 1681), `rom-0230A-whole-enter` 3 (2030, 2031, 2070). The other 661 frames take nothing
  but the external inputs from the trace.
- **Zero events across a menu change, with the menus' code modelled:** `hddosd-110U-whole2-enter`,
  `-adjust-open`, `-adjust-hour`, `-leave`, `-back`, `-up` (HDD OSD); `rom-0230A-whole2-down`,
  `rom-0230A-whole2-to-clock` (ROM 2.30). ROM 2.30's main menu into System Configuration has no
  zero-event result: `rom-0230A-whole2-enter` is PARTIAL (`configEntries` differs from frame 2030,
  with a `menuList` event; Open). The other captures with no event show no menu change (the
  clock alone, the main menu and System Configuration at rest, the power-on runs).
  On a capture that holds the menus' pieces every event is a fault of the model; a modelling
  error in an event's rule shows as an event in every frame, not in one.

Coverage of what the model produces, per frame: clock alone 93.5% to 94.3%; main menu 77.5% to
77.7%; System Configuration 80.9% to 84.0%; Clock Adjustment 85.7% to 85.8% (HDD OSD). In the
rods, orbs and extra passes alone, ROM 2.30's clock sends 677 packets a frame and the model
produces 522 (77.1%); HDD OSD 500 of 601 (83.2%): the same draws in fewer packets. In Clock
Adjustment the clock's own packets are 520 of 996 (`rom-0230A-clock-frame-adjust`).

## What the model takes as input

**The snapshot, once:** the pieces of `LAYOUT` (see "The snapshot").

**From outside, every frame** (`EXTERNAL` in `verify_frame.mjs`): these stay external in every
carried run.

| Piece | Why |
|---|---|
| `time` | the time record, as the time keeper (HDD `func_00235B10`) leaves it |
| `index` | which display buffer is drawn to |
| `scene` + 8 | the field flag (the scale at +0 and the leaving flag at +4 stay the model's) |
| `disc` | the disc state the drive reports |
| `configDirty` | whether the configuration is being saved (the save runs outside the clock) |
| `configItems` | the configuration items as the configuration code leaves them (Clock Adjustment edits items 6 to 0xB) |
| `romWrite` | ROM 2.30: the state of the write of the clock to the drive |
| `mechaconParam` | the console settings word (time zone, summer time, aspect ratio), read from the drive |
| `item0` | configuration item 0, reloaded every frame |
| `pad` | the pad words, as the pad reader (HDD `func_00235EB0`) leaves them at the end of the frame before |
| `wide` | whether the clock was entered from the opening (`0x001F064C`) |
| `timeFilled` | HDD OSD: whether the time keeper has filled the time record yet |
| `display` | the display block (see the rules above); no effect on what is sent |

The view and screen matrices are computed by the model (`clock-camera.md` verifies the screen
matrix's formula and `verify_view_matrix.mjs` the view matrix, both bit for bit). The step from
one frame's state to the next is checked per piece by `verify_clock_state.mjs` and by `--carry`.

**Events** (`EVENTS` in `verify_frame.mjs`): state that the menus' own code writes when something
happens. The model carries each by its rule (ramp ticks, list step, overlay step, menus' code)
and takes the new value only in a frame where the state after the pages function differs from the
model's: `menuRamp`, `cubeRamp`, `cubeList`, `cubeMode`, `standing`, `menuList`, `mode`,
`orbRandom`, `overlayLevel`, `fadeRecord`, `scaleTarget`, `greyRamp`, `vignetteRamp`,
`appearance`, `spriteFade`. Counts per capture: "Events in the carried runs".

## What is not produced

Text and the menus' own drawing; the font is `text.md`'s.

| Screen | Per frame, HDD OSD | Per frame, ROM 2.30 | What |
|---|---|---|---|
| clock alone | 24 + 13 | 24 + 13 | date and time; button hint |
| main menu | 33 + 24 + 23 | 33 + 24 + 23 | menu items' text (after the trips that follow the rods); date and time (sent with alpha 0); hint |
| System Configuration | 95 + 24 + 76 | 95 + 24 + 44 | the list's text; date and time; hint |
| every screen | 2 | 2 | the display buffer swap, sent after the frame function has returned |

In the rods, orbs and extra passes alone, the rest of ROM 2.30's clock-alone frame, per frame, by
the function that has it sent (HDD OSD names): 101 background, blur and tint
(`module_clock_226000`); 24 date and time (`func_00226300`); 13 button hint (`func_002269E0`); 6
letterbox bars (`func_002262C8`); 4 fade overlay (`module_clock_234E70`); 4 right edge column
(`func_00226A88`); 2 display buffer swap; 1 `module_clock_232458`. The whole-frame model produces
the background, bars, fade and column among these.

In the main menu's pages gap on HDD OSD (*measured*, `hddosd-110U-whole-menu`): 27 glyph fans
(`PRIM 0x5D`, texture `0x2F04`) from `func_00232170`, and their state packets; nothing else.

## Not covered

- Text: date and time, button hint, the menus' and the list's text.
- The menus' code is *read* and modelled, and exercised only by the captures above. Not met in a
  capture: the list moving up or down on HDD OSD, a confirmed value's pulse, overlay modes 1 and
  4, ROM 2.30 Clock Adjustment, ROM 2.30 main menu to the Browser. Mode 3's orb rule is
  `verify_transitions.mjs`'s; a whole-frame capture exercises it only in
  `hddosd-110U-whole2-leave`. The list moving, the pulse and the fade are covered piece by piece
  by `verify_cubes.mjs` and `verify_orbs.mjs`.
- Configuration item 0 (the aspect ratio: `config_get_aspect_ratio`, bits 1..2 of
  `var_mechacon_config_param_1`, HDD `0x00371818`, ROM `0x002C9680`; reloaded into item 0
  (`0x00409130` / `0x00375100`) by `config_load_clock_osd` when `D_00370300` is 1): no capture
  has aspect ratio 1 or 2 with the clock alone, so the bars are verified only for the item 0 the
  captures hold.
- A send with no face on one side: the rod function always opens and sends the packet
  (`module_clock_237A28`: `func_002333E0`, the face loop, `func_00238DB0`, no count test), as the
  model does, and no orientation of the rod and seconds angles gives an empty side (*read*). Met
  only for orb trails with an empty ring (70 in `hddosd-110U-whole-boot`, equal); never met for
  a rod.
- Orb mode 2 and 3 on ROM 2.30; a transition on ROM 2.30 in PAL; the opening (`opening.md`) and
  the first-run pages.
- The hour turning in Clock Adjustment, end to end: ROM 2.30 only (`rom-0230A-clock-frame-adjust`,
  rods, orbs and extra passes).

## Open

- `rom-0230A-whole2-enter` (ROM 2.30, main menu into System Configuration with the menus' code):
  verdict PARTIAL, two causes identified and not corrected in the model. Settled when
  `verify_frame.mjs --carry` on this capture ends FOUND with zero events.
  1. `menuList` [ROM `0x0028AFF0`, 0x14] overlaps `configPage`: the event takes the page's `+0xC`
     title width. The fix is to remove `menuList` from `LAYOUT` and `EVENTS` and read the count
     and selection from `configPage` in `clock_cubes.mjs` and `clock_rest.mjs`.
  2. `configEntries` +0xE4 (entry 4's value count): the list rebuild calls `0x002235D8(count)`,
     which writes the entry's value count and value table by language (`0x00205830`): language 0
     (2, `0x0028A800`), 1 (7, `0x0028A860`), 2 (7, `0x0028A9B0`), 3 (2, `0x0028AB00`), 4 (2,
     `0x0028AB60`), 5 (2, `0x0028ABC0`), 6 (2, `0x0028AC20`), then a jump to `0x002294C0` (not
     read). The language word has to become a piece or an external input of `rebuildList` in
     `clock_menus.mjs`.
- `hddosd-110U-whole2-back` (System Configuration to the main menu, circle) and
  `hddosd-110U-whole2-up` exist and have no verdict.
- Captures not taken: ROM 2.30 `back`, `adjust-open`, `adjust-cancel`, `adjust-confirm`,
  `adjust-hour`, `leave`, `mode1`, `mode4`; HDD OSD `down`, `adjust-cancel`, `adjust-confirm`,
  `mode1`, `mode4`, `to-clock`; ROM 2.30 PAL `rom-0230E-pal-whole2-enter`.
