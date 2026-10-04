# Native opening — design

Approved: controller, 2026-10-04 mandate

Date: 2026-10-04. Builds on `docs/superpowers/specs/2026-10-03-native-clock-design.md` (native scene, parity rule,
native renderer, live window; merged on `main`) and runs beside `docs/superpowers/specs/2026-10-03-native-menus-design.md`
(branch `feat/native-menus`, in progress). Branch `feat/native-opening`, worktree `CrystalClockVK-wt/opening`, from `main` (2b7edcd).

## Goal

The PS2 OSD's power-on sequence runs natively in the live window: the camera falling through the fog past five glass
cubes and four light orbs, the logo, the dive, the fade to black, and the hand-off to the clock. It is produced by native
scene code ported from the verified model of `facts/opening.md`, proven equal to the console's draws through the GS parity
rule, and drawn by the native renderer. `CrystalClock --boot` plays it and then continues into the clock.

## Decisions (controller, self-approved under Jean's 2026-10-04 mandate; Jean asked for the "boot" front)

| Decision | Choice |
|---|---|
| Canon build | HDD OSD 1.10U, NTSC. ROM 2.30 is a second witness for tests only |
| Scope | The normal intro (scene 0, module 1) from the module's first frame to the hand-off into the clock: camera and stage machine, fog, light orbs and trails, five cubes, the ghost, the dive blur, the fade, the logo, the letterbox bars, the towers, the hand-off decision and the clock's entry |
| Later slices | The illegal-disc scene (module 4, `facts/opening.md` section 9), PAL (section 7), ROM 2.30's other disc-state tables, the hard-disk boot branches |
| Towers | The VU1 microprogram's math is ported natively as scene code (`TowerVu1`), not a VU1 emulator. It is proven against the verified model and the console's PATH1 packets. Without a play history there are no towers (as on a fresh console): the app takes a history from an option |
| Parity rule | As the native clock: the scene is gated through `GsParityRenderer` against PCSX2's software renderer with exact per-pixel budgets that fail both ways; geometry is gated draw by draw against the console's GS dump |
| Arithmetic | `Opening<A>` is templated like `Clock<A>`: `EeArithmetic` (round toward zero, exact sums, the EE `sinf`/`cosf`) in tests, `NativeArithmetic` in the app |
| Live app | `--boot` plays the intro and hands off to the clock. The main menu items arrive when the menus branch merges; until then the hand-off ends at the clock alone (the `Screen` seam below) |
| Sound | A separate front. The scene reports the sound commands the console sends, as events with the verified ids and arguments; the app drops them |
| Old code and documents | None involved. Sony data is never committed (textures are extracted to the git-ignored `References/textures/`) |

## What is measured (facts) and what the design takes from it

`facts/opening.md` (every number below is its; sections in parentheses).

- **Frame map (2).** A frame is, in order: scissor; towers (PATH1, only with a history); the previous frame's ghost; the frame
  copied at half width into the store; fog (6 layers); lights (while camera z < 73); cubes (5, 10 passes each, while z < 73); the
  dive blur (level 1..3); the fade rectangle; the logo; the letterbox bars; buffer swap. 596 packets a frame at the fullest.
  No pad is read anywhere in the module.
- **Timeline (3, verified).** Stage machine with thresholds 16, 56, 104, 320, 672, 800, 1160; stage 1 waits for disc state in
  {0x64, 0x6A..0x70, 0x72..0x74} and counter > 120; stage 2 starts the dive; stage 3 ends the scene. Integrator in a fixed
  operation order. 246 drawn frames with the no-disc state 0x64; the module's counter 1 shows disc state 0x65, from counter 2 it is 0x64.
- **Objects (4, verified bit for bit).** Cubes (corner and face work, ten passes, refraction, mirror, fixed, graded colour);
  lights (four, trails in a ring of 128, `phase` from `rand()`: the lights differ from run to run); fog (17 x 17 mesh, six layers,
  scrolling offsets carried); ghost and logo; the flat helpers (copy, blur, fade, bars); the library results (`sinf`, `cosf`,
  `sceVu0RotMatrix`, camera, view-screen matrices) and the arithmetic helpers (exact `add`, `sub`, quotient, root).
- **Towers (5, verified).** `OpeningInitTowersFog` builds 126 cells from the play history (21 entries x 6 cells of a 14 x 9
  grid); per frame one pass over the grid builds a 0x840-byte chain per tower; the 229-instruction microprogram turns it into
  six strips of four vertices. A vertex outside the bounds is written without the drawing kick, and so are the three after it.
  The routine that moves coordinates with the normal is dead code in the unpatched program and is not ported.
- **Hand-off (8, verified).** `opening_transition_to_clock` decides by the disc-state snapshot, the forced-clock word and the
  hard-disk words. With no disc: module 2 (the clock), previous module 1. Around it: 29 frames with nothing sent, then the clock's
  first frame; the clock's entry schedule from that frame is `facts/clock-transitions.md` section 2 (all seven orbs fly in).
- **Textures (2, read).** Table `D_002AF870`: 0 logo (256 x 64), 2/3/5 fog (64 x 64), 6 towers, 8 light sprites, 10/11/12 the cubes'
  mirror map and two fixed patterns. The fog and cube textures are 16-bit in GS memory.

## What is not measured (gaps, each with its measurement task in the plan)

| Gap | Effect on this design | Plan task |
|---|---|---|
| The GS rule does not yet draw most of the opening: of 15 722 passes in `hddosd-110U-opening-full` only 249 are free of a skip reason; 14 000+ are skipped for the 24-bit depth buffer (PSM 0x31), 16-bit textures (PSM 0x02), per-pixel alpha blending (PABE) and alpha correction (FBA) | The rule is extended (Z24, CT16, PABE, FBA), each extension proven on the opening's own frames before the opening is gated through it | Task 1 (audit), Task 12 |
| Towers: the intro captures have no towers (empty history); the whole intro with towers has a dump but no probes (`hddosd-110U-opening2-whole`) | The towers' dump gate needs the history and the lights phase of the same run | Task 2 |
| The mip-mapping state of the towers' texture (`facts/opening.md` says 256 x 256 mip-mapped; a rule that skips when the minification filter differs from magnification would hide it) | Native sampling of texture 6 is decided only after the audit | Task 1, Task 12 step 6 (conditional) |
| Opening textures are not extracted (only the clock's ten are) | Sony data: extracted from GS memory to `References/textures/opening/`, never committed | Task 3 |
| The clock's entry after the opening (`whole-boot-opening`) has never been run by the native clock (it starts from a mid-run steady frame) | The hand-off needs the clock's T0 state and the orbs flying in | Task 2 step 4, Task 14 |
| What the 29 empty frames before the clock's first frame wait for (module resource loading) | Natively a constant of 34 frames from the last drawn scene frame to the clock's first, flagged as measured, not as a rule | the plan's gap table (`re-scout`, the controller's to dispatch) |
| `rand()`'s seed at power-on | The lights' phase is an app option (default random in 0xD80..0x16A8) | none (not a fact the picture depends on) |
| Disc states other than 0x64 (the stage-1 hold set 0x65..0x69, 0x71; the hard-disk words; exec 0 wait) | Ported as the verified table with the two stage-1 classes; only 0x64 is gated end to end, the hold set is gated on its stage captures | — (listed in `facts/README.md`) |
| The sound command ids' meaning | Events carry ids and arguments only | — |
| The fade's 'W' mode, PAL towers between the captured spans, mutation coverage of the stage verifiers | Not in this slice | — |

## Architecture

```
src/scene/opening/            plain C++, no Vulkan; namespace scene::opening
  Types.hpp                   BootOptions, History, DiscSchedule, SoundEvent, HandOff
  Vu0.hpp                     libvu0 as the opening calls it (rot, trans, mul, camera, view-screen, clip), templated on A
  Timeline.{hpp,cpp}          stage machine, integrator, camera and matrices, logo/fade/blur levels         facts/opening.md 3
  Fog.{hpp,cpp}               mesh, brightness, six layers, carried offsets                                 4.3, 4.6
  Lights.{hpp,cpp}            four orbs, glow and core quads, trail ring and strip                          4.2
  Cubes.{hpp,cpp}             five cubes: corner and face work, ten passes                                  4.1
  Flat.{hpp,cpp}              ghost, frame copy, blur trips, fade, bars, logo, scissor                      4.4, 4.5
  Towers.{hpp,cpp}            set-up from the history, brightness table, per-frame chains                   5.2
  TowerVu1.{hpp,cpp}          the microprogram's three parts as math, the hidden-vertex rule                5.1
  Opening.{hpp,cpp}           Opening<A>::frame(): assembles one scene::Frame in the OSD's order, steps     2
  Handoff.{hpp,cpp}           opening_transition_to_clock's decision and the measured gap                   8
src/scene/Frame.hpp           grows the vocabulary the opening needs (below)
src/parity/FromScene.*        opening layout; unit conversion only
src/render/NativeRenderer.*   more targets, 16-bit texture sets, per-pixel alpha, the new blends
src/app/Screen.hpp            Screen { step(); frame(); done() }; ClockScreen, OpeningScreen, the --boot chain
tools/scene/export_opening.mjs   the capture's probes decoded to named values (isolated tests)
```

Layering is the clock's: `scene/` depends on nothing; `render/` on `scene/`; `app/` on all; `parity/` on `scene/`. Every scene function
names its facts section in one line.

### The scene's inputs

The intro is a closed system: no pad, no clock time. Its externals are small and named in `BootOptions`:

| Input | Source in the console | Native default |
|---|---|---|
| disc state per module counter | drive status (`0x001F000C`) | 0x65 at counter 1, 0x64 from 2 (no disc) |
| lights `phase` | `rand() % 0x929 + 0xD80` (`D_00370A74`) | random in that range; `--lights-phase` |
| play history (towers) | `0x001F0198`, 21 entries of 22 bytes | empty (no towers); `--towers demo` loads the verified 21-entry history |
| clock forced / hard-disk ready / exec | `0x002AD22C`, `0x002AD230`, `0x002AD234` | not forced, not ready (the plain branch) |
| video mode | `evenOddFrame`, `ay` | NTSC; PAL asserts |

A run is determined by these. The tests feed each capture's own values (read from its probes) and compare every frame.

### `scene::Frame` additions

The clock's `Frame` (`src/scene/Frame.hpp`) is kept; the opening adds only what its draws need, none of it GS-shaped except the
two fields the rule needs to name a format.

- `TargetName` gains `Store` (the half-width copy the ghost reads) and `Extra` (the 1024 x 256 buffer the cubes refract through);
  `Display` is the page drawn this frame (`displayIndex`).
- `BlendOp` gains `AddDestinationAlpha` (`Cs x Ad + Cd`, cube passes 2, 4, 7, 9) and `SubtractFixed` (`Cd - Cs x fixed`, the fade's
  black mode as the dump shows it); the list is closed by the audit (plan Task 1) against the dump's blend histogram.
- `Material` gains `perPixelAlpha` (PABE) and `alphaCorrection` (FBA) if the audit finds draws that carry them; `Frame` gains
  `textureSet` (`Clock` or `Opening`) and `depthBits` (32 or 24) for the layout.
- Strips and fans are expanded to triangle lists in scene code; a vertex the VU1 writes without the kick leaves out the triangles that
  use it, as the GS draws them.
- One `Pass` per run of draws under one GS state, as the dump parser groups them (a fog layer is one pass, the sixteen rows and
  their sixteen packets together).

### `NativeRenderer`

As the clock's, plus: five targets (display page, refraction/work buffers renamed by use, store, extra) as colour attachments at
the output scale, sampled after barriers; the opening's textures from `References/textures/opening/` as RGBA8 (16-bit sources
expanded with the GS's rule: each 5-bit channel shifted left 3, alpha from bit 15 through `TEXA`); the two new blends by fixed
function; per-pixel alpha in the shader when a pass asks for it; MSAA replaces edge antialiasing. No GS rule runs in the window.

### `Handoff` and the `Screen` seam

`Handoff::decide(snapshot, forcedClock, hddReady, hddExec, cdda)` ports section 8's table (module, execute-app-type, previous-was-opening).
The app acts only on module 2 (the clock); every other result is reported in the panel and treated as module 2 (the illegal scene,
module 4, is a later slice). `Opening` reports `ended()` after the 246th frame; the app then shows black for the measured gap and starts
`ClockScreen` from the T0 state of `whole-boot-opening` with previous module 1 (all seven orbs fly in). When the menus branch merges,
`ClockScreen` is replaced by its `Menus`-driven version at the same seam (the main menu takes input from T0 + 129).

## Testing

| Level | Compares | Against | Criterion |
|---|---|---|---|
| Logic, isolated | `Timeline`, `Fog`, `Lights`, `Cubes`, `Towers`, `TowerVu1`, `Vu0`, `Handoff` per frame | `opening.json` decoded from the capture's probes by `tools/scene/export_opening.mjs` (the console's own memory, as probed) | exact, bit for bit, `EeArithmetic` |
| Carried run | `Opening<EeArithmetic>` from counter 1 over the whole capture, fed only the capture's externals | the same, every frame | exact |
| Scene against the dump | `Opening::frame()` -> `fromScene` -> `GsFrame` | `frame_passes.mjs` of the capture's own `.gs` dump: pass count and order, state, every vertex, per frame (adjacent scene passes of equal state are coalesced as the parser does) | exact |
| Through the rule | the same frame drawn by `GsParityRenderer` | PCSX2's software renderer on the dump (`make_fixture.mjs`, `oracle.mjs`) at chosen frames | exact per-pixel budgets, fail both ways (`unexpected difference`, `larger difference`, `tighten`, `stale budget entry`); a new entry only where PCSX2's texture cache explains it |
| Rule extension | Z24, CT16, PABE, FBA drawn by `GsParityRenderer` | the oracle on the opening's own frames (the 249 unskipped passes are the baseline that must not regress) | the existing gate, no budget grows |
| Native | `NativeRenderer` at 640 x 224 | the same oracle images | report only: differing share, largest difference, per target |
| Hand-off | `Handoff::decide`; `ClockScreen` from T0 | `verify_opening3_handoff.mjs` branches; `whole-boot-opening`'s scene export, 49 frames | exact |
| Eye | the window (`--boot`) | Jean's verdict | screenshots and a PNG frame sequence |

Captures (Watson `Runtime/captures`, fixtures under the main checkout's git-ignored `References/fixtures/`):

| Capture | Holds | Used for |
|---|---|---|
| `hddosd-110U-opening-full` (`.gs`, `.trace.jsonl`) | the whole intro, no towers; camera, lights, fog, cubes probes | logic, carried run, dump gate, pixel gates |
| `hddosd-110U-opening3-intro` | the whole intro with stage, fade, blur, logo probes | stage machine, logo alpha, fade and blur levels |
| `hddosd-110U-opening2-inputs` | library results (`sinf`, `cosf`, matrices) | `Vu0`, arithmetic |
| `hddosd-110U-opening2-ee-a`, `-ee-b`, `-ee-late` | towers from the 126-tower state, with chains | `Towers`, `TowerVu1` |
| `hddosd-110U-opening2-flat` | the flat draws, whole intro | `Flat` |
| `hddosd-110U-opening2-handoff-probes`, `opening3-illegal`, `opening3-*` stage set | hand-off branches | `Handoff` |
| `hddosd-110U-whole-boot-opening` | the clock's first 49 frames after the opening | clock entry |
| **`hddosd-110U-opening4-towers-whole`** (new, plan Task 2) | the whole intro with 126 towers, with history and phase probes | towers in the carried run and the dump gate |

## Slices

1. Measurement: the audit, the towers capture, the textures, the clock entry, the exporter.
2. Vocabulary and libraries: `Frame` additions, `Vu0`, arithmetic, `FromScene` layout.
3. Scene objects in parallel lanes: timeline, fog and lights, cubes, flat draws, towers.
4. Rule and renderer extensions in parallel lanes.
5. Assembly: `Opening<A>`, the carried run, the dump gate, the pixel gates.
6. Hand-off, clock entry, `--boot`, soak, screenshots.

## Constraints

English-only code, PascalCase, `facts/` the only trusted store, the GS rule only as a measuring rule, Windows and Vulkan 1.4, no
attribution, never push, merges into `main` with Jean's word. `References/model/*` and `References/scripts/*` are not edited (new files
only in measurement tasks). Sony data is never committed. Work in `CrystalClockVK-wt/opening`; lanes in
`CrystalClockVK-wt/opening-<lane>`.

## Out of scope

The illegal-disc scene, PAL, ROM 2.30 as a target, the hard-disk boot branches, sound, the main menu items (menus branch), the Browser,
improvements beyond resolution and MSAA, macOS.
