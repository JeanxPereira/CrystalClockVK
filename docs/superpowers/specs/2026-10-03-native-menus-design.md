# Native interactive menus — design

Date: 2026-10-03. Builds on `docs/superpowers/specs/2026-10-03-native-clock-design.md` (branch
`feat/native-clock`: native scene, parity rule, native renderer, live window, text).

## Goal

The OSD's screens around the clock run natively and interactively: the main menu, System Configuration with
its glass cubes and list, the Version page, and the transitions between them and the clock — driven by a
keyboard or gamepad, produced by native scene code ported from the verified models, proven through the GS
parity rule, drawn by the native renderer.

## Decisions (Jean, 2026-10-03)

- Scope of the first slice:
  - **Main menu**: the "Browser" and "System Configuration" items. Browser is shown and selectable but not
    entered (the cursor moves onto it; confirming it does nothing in this slice).
  - **System Configuration**: the glass cubes (ring, pulse), the option list with its cursor, entering and leaving
    an entry, the values shown, Square to hide/show the menu over the clock.
  - **Version page**.
  - **Transitions**: main menu ⇄ System Configuration ⇄ clock, as `clock-transitions.md` times them.
- Parity rule, renderer, units, arithmetic policies, isolation from Jean's work: as in the native clock design.
- Out of this slice: the Browser itself, Clock Adjustment's editing pages beyond what the list shows, the first-run
  path, the opening, sound (the sound front is separate; menu sounds join when it lands).

## What is measured (facts)

- `References/model/clock_menus.mjs`: the menus' code that moves the clock's state — main menu
  (`func_00232408`), System Configuration (`module_clock_231E48`, `func_00230FD8`, `func_00231C50`), the pages
  function `module_clock_232458`, the thread's loop `clock_input_check_handler_p6_p7_tgt`: pad words in, ramps and
  modes out. Ramps `{length, counter, changed, state}` share `tickRamp` with the clock.
- `facts/clock-transitions.md`: the timeline of each transition frame by frame (menu items ramp `fps / 6`, the
  weight, camera offset and scale, rods appearing, the leaving flag).
- `facts/config-cubes.md`: the cubes — the rods' pipeline with a cube mesh; placement, ring, pulse; captures
  `hddosd-110U-cubes-*`.
- `facts/text.md`: menu, list, values, arrow and the Version page's strings and places (verify_text2: `menu`,
  `config`, `version`, `adjust`, `down` captures), the same glyph cache Task 11 ported.
- `facts/clock-frame.md`: the whole-frame model already carries System Configuration's cubes; captures
  `hddosd-110U-whole3-menu` (main menu), `-whole3-enter` (menu → System Configuration), `-whole2-back`,
  `-whole-menu`.
- Open items that touch this slice (README): config-cubes "what each send does to the pixels is a reading"; the
  placement vector's fourth word; the font draws on that screen (now ported for the clock).

## Architecture

```
src/scene/Menus.{hpp,cpp}     port of clock_menus.mjs: pages, ramps, cursor, screen codes, transitions,
                              between(); pad mask in → state out                              plain C++
src/scene/Cubes.{hpp,cpp}     port of the cubes (mesh, ring, pulse) through the rods' pipeline
src/scene/Text.*              gains the menu, list, values, arrow and Version strings (same rules/cache)
src/scene/Clock.*             the frame includes the menus' passes (cubes, menu text) in the OSD's order;
                              the menus own the state they own (ledgered order, as Task 9)
src/app/Input.{hpp,cpp}       keyboard + gamepad (SDL3) → the PS2 pad mask the OSD reads
                              (cross, circle, square, triangle, d-pad); edge and hold as the pad thread sees
parity/                       the menu captures through the rule: geometry exact, pixels within the budgets
```

- The input becomes the same 16-bit pad word the OSD's code reads, once per frame. A capture's recorded pad
  words therefore replay through `Menus` exactly; the live window feeds SDL's state through the same mapping.
- Default mapping: keyboard arrows = d-pad, Enter/Z = cross, Esc/X = circle, Backspace/S = square, T =
  triangle; gamepad by position (south = cross, east = circle, west = square, north = triangle). Remappable in
  the debug panel.
- The live app starts at the main menu (or at the clock with a flag); the debug panel shows the current screen,
  ramps and pad word.

## Testing

- Isolated: `Menus<EeArithmetic>` against `clock_menus.mjs` outputs exported per frame (same exporter path as
  Task 3/9), bit for bit, on the menu and transition captures (main menu idle, menu → config, config → menu,
  Square hide/show, cursor moves, Version).
- Carried: one `Clock` with menus over each whole capture, state equal every frame; the frame's passes equal the
  dump's draws (state and every vertex), cubes and menu text included.
- Pixels: ParityTool on each capture's oracle frame with an exact per-pixel budget (fails both ways); new
  entries only where the PCSX2 texture cache explains them.
- Input: the pad mapping and edge/hold behaviour unit-tested; a scripted soak drives every screen and
  transition with 0 validation errors.

## Slices

1. Export menus inputs/outputs from the model for the menu captures; port `Menus` (isolated tests).
2. Cubes (isolated + through the rule on `cubes-*` captures).
3. Menu text (list, values, arrow, Version) through the existing Text.
4. Frame assembly with menus + carried geometry + pixel gates on `whole3-menu`, `whole3-enter`, `whole2-back`.
5. Input mapping + live window navigation; screenshots and a short screen recording (frames as PNG) for Jean.

## Constraints

As the native clock design: English-only code, PascalCase, `facts/` is the only trusted store, the GS rule only as a
measuring rule, Windows/Vulkan, no attribution, never push, merges into `main` with Jean's word. Work happens in
`CrystalClockVK-wt/menus` on `feat/native-menus`, branched from `feat/native-clock` (ce9400a); it is rebased or
merged onto `main` after the native clock lands.
