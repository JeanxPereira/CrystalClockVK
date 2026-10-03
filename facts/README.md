# Facts

Pages in this directory are the only notes in this repository meant to be relied on. Everything
under `docs/`, `context/` and the top-level `MEMORY.md` predates them and mixes builds.

Rules for a page here:

- It names the build every address belongs to. Two builds are in play: **HDD OSD 1.10U**
  (`hddosd.elf`, SHA-1 `e932f3508313e2807467a0f354acc56869ea77f6`), the canon build, with the
  CrystalOSD symbols; and **ROM 2.30** (BIOS `0230AC20080220`; `0230EC20080220` in PAL), the build
  run live in Watson.
- It says how each statement is known: *verified* (a script recomputes it bit for bit from inputs
  a trace probe recorded; the script and a capture are named), *measured* (seen in a capture, not
  recomputed), *read* (from disassembly, with the address range).
- It names the script or tool that produced its numbers, so they can be produced again.
- A measured number is not a constant until what writes it has been read. A page records the rule
  that produces a value, not only the value: a number seen in one capture may be eased, depend on
  the video mode, or differ between builds (the scene scale and the vertical proportion both do).
- It states what the system is, in present tense; a later measurement replaces the statement it
  corrects.

| Page | What it holds |
|---|---|
| `verification.md` | Every verifier, what it covers and its result on each build; the regression suite; where the builds differ |
| `clock-function-map.md` | Which HDD OSD function each ROM 2.30 clock function corresponds to |
| `clock-state.md` | How the time becomes the current rod, the angles, the progress and the colours |
| `clock-camera.md` | View and screen matrices, scale and proportion, sines, rod and orb placement |
| `clock-scene.md` | How a frame is assembled: the depth-sorted draw list, measured rod parameters |
| `clock-rod-draw.md` | What the rod drawing function computes, pass by pass |
| `clock-extra-passes.md` | The two reflection passes over the rods |
| `clock-orbs.md` | An orb's trail, ring and sprites |
| `clock-frame.md` | End to end: a model that produces every packet of the rods, orbs, extra passes, background, blur, overlays and System Configuration's cubes of a frame from the frame's inputs, carried frame to frame |
| `clock-frame-rest.md` | The rest of a clock frame: the background tube, blur trips, copies and tint, fade overlay, vignette, bars; text only as which function sends what |
| `clock-transitions.md` | The timeline: entering the clock scene, menu to System Configuration and back, leaving, the first-run path, the options dialog, exits for the drive's codes |
| `clock-gs-state.md` | The helpers that choose buffer, texture, blend and depth test, compared call by call across the builds |
| `clock-textures.md` | Where the textures come from and what each one is |
| `config-cubes.md` | The glass cubes of System Configuration: the rods' pipeline with a cube mesh; placement, ring, pulse |
| `text.md` | The font file, the glyph cache, a glyph's twelve-vertex fan, the string filter, alpha and place of each string |
| `pal.md` | How each build decides PAL, every value that changes with the video mode and its rule, the verifiers' results on real PAL runs |
| `opening.md` | The boot intro (module `opening`): frame map, timeline, cubes, lights, fog, logo, towers and their VU1 microprogram, ROM 2.30, PAL, the hand-off, the illegal-disc scene |
| `hddosd-boot.md` | Why HDD OSD would not start in the emulator, and how it is started here |
| `data/rod-mesh.json` | The rod's mesh from the HDD OSD file: 16 faces; equal in both builds |

Measured pages about ROM 2.30 live with the instrument: `Watson/docs/findings/`.

The patent (US 6,693,606, `docs/clock_patent/`) states the intent and agrees with what was
measured: twelve blocks, the hour picks the block, the minutes are a filling that starts full
and runs down, the ring turns, light spots circle a sphere and leave tracks. It holds no
numbers; those come only from the binary. Two leads not read yet, from psdevwiki's OSDSYS page:
patents JP2001154772A (display method) and JP2001148032A (image plotting).

A way to drive the clock for a capture: System Configuration, Clock Adjustment. While a field
is being edited the seconds are held at 0, the orbs gather at the centre, and up or down on the
hour field turns the hour (`watson_gif_trace {hold: ["up"]}` records it from the first frame).

## How to work here

- Skills in `.claude/skills/`: `measure` (one function to a verifier to a fact), `capture` (taking
  a capture from the emulator), `facts-page` (the page contract), `campaign` (many questions at
  once), `commit` (explicit paths, message form).
- Agents in `.claude/agents/`: `re-scout` (read-only scout of one question), `verifier-writer`
  (one verifier, mutation, manifest entries), `re-refuter` (re-runs one claim), `doc-writer`
  (one facts page).
- `node References/scripts/run_all.mjs` runs the regression suite (`run_all.manifest.json`);
  `node References/scripts/mutate.mjs <verifier>` checks a verifier fails when its arithmetic is
  changed; `node References/scripts/lint_facts.mjs facts/<page>.md` must end `FOUND` after every
  edit of a page.

## What is left before implementation

The next big step is the Vulkan side: a GS backend tested pixel for pixel against PCSX2's
software renderer replaying the GS dumps, with the verifiers as the tests of each module. What
the pages still list as open, grouped:

**The frame model** (`clock-frame.md`)
- `rom-0230A-whole2-enter` ends PARTIAL: `menuList` overlaps `configPage`, and the language word
  is not yet an input of `rebuildList` (entry 4's value count).
- `hddosd-110U-whole2-back` and `-whole2-up` have no verdict; captures not taken are listed on
  the page.
- Not met in a capture: the list moving up or down on HDD OSD, a confirmed value's pulse,
  overlay modes 1 and 4, ROM 2.30 Clock Adjustment, ROM 2.30 main menu to the Browser,
  configuration item 0 other than 0, a send with no face on one side of a rod, orb modes 2 and 3
  on ROM 2.30, a transition on ROM 2.30 in PAL.
- Text is not part of the model (`text.md`).

**Text** (`text.md`)
- ROM 2.30 place level (the language is not in the probes), the list entries' alpha, one
  colour-block mismatch, captures not taken.
- PAL: `DrawIcon`'s PAL branch, one crashing capture, no PAL capture of config, adjust or down,
  no ROM PAL capture.
- Languages other than English and the language table; decorations, line break and font blocks 1
  to 5; the Browser's text.

**Transitions and overlays** (`clock-transitions.md`, `clock-frame-rest.md`)
- The reset in `func_00234B88` is never seen; overlay mode 1 has no caller.
- The first-run intro: `verify_first_run.mjs` holds the rules, its traces are not compared (HDD
  OSD, ROM 2.30, PAL, ROM 2.30's later pages).
- Code `0x72` with its gate word written to 1; PAL fade rising on ROM 2.30; PAL hour turning
  with a record written to hh:59:59; a PAL written-state capture on ROM 2.30.
- Configuration item 0 other than 0 (bars, text height); the pages function beyond its blur
  trips; ROM 2.30's vignette sector function in full.

**The opening** (`opening.md`)
- Which hand-off branch let module 4 through; the hard-disk branches, stage 2's disc-state
  sounds, the disc states that hold stage 1 and the illegal scene's 0x72 and MECHACON exits are
  not reached by any capture; the sound command ids' meaning; the fade's 'W' mode.
- ROM 2.30: towers late in the dive, the stage-3 countdown and hard-disk hold, the hand-off's
  other branches, the illegal-disc scene, the intro in PAL; the fade and bars counts (30 and 348
  against 31 and 359).
- PAL: the illegal-disc scene and the towers on either build.

**Camera, scene, rods, orbs, state, GS state, textures, cubes**
- Which menu event calls each of the three configuration callbacks; orb modes 2 and 3 read only
  as far as the colours and targets.
- The sorted insertion and the depth key are not recomputed on their own.
- The float-to-unsigned conversion of the reflection coordinates below zero; which side of the
  rod flag 0 is.
- A change of hour while the clock runs freely; the time keeper by recomputation; who reads the
  hour-hand and second-hand globals.
- The helpers that bind the display buffer, copy, blur and overlay are not compared call by
  call.
- Where `TEXCSMOK` and `TEXCSTSL` are drawn; the font's textures.
- Cubes: PAL's rate and ramp are read only; no picture was compared; the placement vector's
  fourth word; the blur chain's own rule apart from `verify_frame.mjs --carry`.
