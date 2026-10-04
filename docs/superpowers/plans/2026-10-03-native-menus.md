# Native Interactive Menus — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The OSD's main menu, System Configuration (glass cubes, list, entries, values, Square over the clock) and the transitions between them and the clock run natively and interactively from a keyboard or gamepad, ported from the verified JS model and proven through the GS parity rule.

**Architecture:** `scene/Menus` ports `References/model/clock_menus.mjs` (plus `clock_rest.mjs menuStep` and `clock_date.mjs`) as plain integer C++ that moves the clock's state from a 16-bit pad word; `scene/Cubes` ports `clock_cubes.mjs` through the rods' emitters; `scene/Text` gains the pages' strings (menu items, list title, entry, value, arrow, clock value) with the menus' live ramps; `scene/Clock` calls them in `clock_frame.mjs frame()`'s order. A new exporter path records the model's per-stage state, the cubes' draws and the pages' strings for every menu capture and for scripted pad sequences; tests compare bit for bit, the carried frame goes through `parity::fromScene` against each capture's GS dump, and frame 0 through ParityTool with exact per-pixel budgets. `app/Input` turns SDL3 keyboard and gamepad events into the pad words the OSD reads.

**Tech Stack:** C++23, Vulkan 1.4, SDL3, Dear ImGui, nlohmann/json, CMake/CTest; Node 25 for the exporter; Watson (PCSX2) for the three new captures.

**Spec:** `docs/superpowers/specs/2026-10-03-native-menus-design.md` (builds on `docs/superpowers/specs/2026-10-03-native-clock-design.md`). Real APIs consumed: `.superpowers/sdd/2026-10-03-native-clock/task-9-report.md`, `task-10-report.md`, `task-11-report.md` in `D:/CodingProjects/CrystalClockVK-wt/native`.

## Global Constraints

- English only. No comments except where the OSD's arithmetic needs one; every scene function names its facts page or model function in one line (`// clock_menus.mjs listInput (func_002316B8)`).
- Commits `Type(Scope): Short imperative description`, at most 72 characters. No attribution, co-author or "generated" line anywhere: commits, code, docs, reports.
- Never `git push`. Never `git add` a directory (a hook refuses): name each file. `git rm` per file.
- No junctions, no symlinks, no `git worktree remove` on a tree that holds one. The main checkout's `References/` (dumps, fixtures, textures) is read by absolute path through configurable variables: CMake cache `CLOCK_FIXTURE`, `CLOCK_REFERENCES`, `CLOCK_DUMPS`, `CLOCK_SCENE`; environment `CLOCK_REFERENCES` for the node tools (default `D:/CodingProjects/CrystalClockVK/References`).
- Never modify tracked files of `D:/CodingProjects/CrystalClockVK`. Fixtures written under its `References/fixtures/` are git-ignored data; when the permission system refuses the write, write to `<worktree>/out/fixtures/<capture>/` and the controller installs them.
- `facts/` is the only trusted store; `docs/`, `context/`, `MEMORY.md` are never cited as fact. The JS model (`References/model/clock_*.mjs`) and the verifiers are the executable reference: never edit `References/model/*` or `References/scripts/*`; wrap their exports.
- Canon build HDD OSD 1.10U, NTSC. The model's ROM 2.30 branches are not ported.
- The parity rule (`src/parity/`, `GsParityRenderer`, `shaders/GsParity.*`, `tools/parity/`, `tools/ParityTool/`, its tests and budgets) is kept and never weakened. Budgets are exact per pixel and fail both ways (`unexpected difference`, `larger difference`, `tighten`, `stale budget entry`); a new entry only where PCSX2's software texture cache explains it (a bilinear tap outside the draw's converted texture rectangle reads zero).
- Scene code that does float arithmetic is templated on `Arithmetic` (`Cubes<A>`, `Text<A>`): `EeArithmetic` in tests, `NativeArithmetic` in the app; `ClockSceneNative` links no `EeArithmetic`.
- Windows, Vulkan 1.4, RX 6750 XT. Validation counts VALIDATION-type messages only.
- Browser shown and selectable, never entered. No sound. The Version page is deferred (see "Deferred: the Version page").
- A test that needs a fixture registers only when the fixture exists; configure prints which gates are active.
- Work happens in `D:/CodingProjects/CrystalClockVK-wt/menus` (`feat/native-menus`) and lane worktrees `D:/CodingProjects/CrystalClockVK-wt/menus-<lane>` (`lane/menus-<lane>`); the controller merges lanes. Nothing goes into `main` without Jean's word.

## Review Focus

1. **A tap shorter than one logic frame, or a key held down** (OS key repeat): exactly one `pressed` edge per physical press, none while held. Test: Task 7, `InputTest` cases `tap inside one frame` and `held key gives one edge`.
2. **Cross on Browser:** nothing happens — the main menu stays, no overlay mode 3, no leaving flag, no screen code 9999. Test: Task 4, `MenusTest` case `browser confirm suppressed` (option off: no state change over 140 frames of the `browser` scenario; option on: equal to the model); Task 9 soak asserts the same live.
3. **Buttons pressed while a transition moves, and the cursor at the ends of a list** (mashing cross, circle, square, triangle during the opening and closing of System Configuration; up at the first entry, down at the last main-menu item): exactly what the OSD does. Test: Task 1 generates the scripted scenarios `menu-cursor`, `enter-mash`, `config-wrap`, `square`, `entries`, `back` through the model; Task 4 `MenusTest` and Task 5 `CubesTest` compare every stage of every frame.
4. **Focus lost, or a gamepad unplugged, while a button is held:** the button is released; the next press gives a fresh edge. Test: Task 7, `InputTest` cases `focus lost releases` and `gamepad removed releases`.
5. **Long navigation** (the list position wrapping after 60 places, the selected-entry glow wrapping, the cubes' 16-bit spin wrapping): native state stays equal to the model. Test: Task 1 scenario `long` (70 downs, spin written to 0xFF00 at frame 0); Task 4 and Task 5 compare every frame.

## Rulings made while planning

- **R1.** Ports are specified by source (model file and function, or the HDD OSD asm file where the model is silent) plus exact test data, as in the native clock plan. The tests compare to the model value for value.
- **R2.** `Menus` is plain integer C++, not templated: `clock_menus.mjs` does no float arithmetic (it truncates stored floats, stores float constants, and uses BigInt dates). `Cubes<A>` and `Text<A>` stay templated.
- **R3.** HDD OSD 1.10U only: every `m.build === 'rom'` branch of the model is left out.
- **R4.** The list entries' crossfade words `D_0037029C` (first), `D_003702A0` (second), `D_003702A4` (second's index) are not in the model (facts/clock-frame.md Open: "Leaving-entry drawing is not modelled"), yet the entry text's alpha reads them (facts/text.md section 5). They are ported from `CrystalOSD/asm/browser/func_002316B8.s` (up/down: index = old selected, second = first, first = 0) and `CrystalOSD/asm/clock/func_00230FD8.s` (every frame after the glow: first = min(first + 8, 128) floored at 0, second = max(second − 8, 0) capped at 128), and tested against the console's probed words and the strings' alpha on the new capture `whole3-down`.
- **R5.** Browser: the faithful model hides the main menu and leaves the clock on cross. `MenusOptions::browserEnters` (default `true`, the model) is `false` in the app.
- **R6.** The pad reader's repeat rule (`func_00235EB0`) is not in facts. Natively `repeating = pressed`: one step per press. It is read only by Clock Adjustment's field editor.
- **R7.** Triangle on the main menu (Version) and in System Configuration (Options) change nothing: the model pushes "not modelled" notes there.
- **R8.** The `cubes-*` captures are `verify_cubes.mjs` traces without the frame snapshot, so the scene exporter cannot run them. Cubes are tested on the whole-frame captures (`whole3-config`, `whole3-enter`, `whole2-down`, `whole2-up`, `whole2-back`, `whole2-to-clock`) and the scripted scenarios (`entries` reaches the confirmed value's pulse, which no capture holds); their pixels go through the scene gates on `whole3-config`, `whole3-enter`, `whole2-back`.
- **R9.** `whole-menu` holds no menus' pieces and no text probes: it cannot drive `Menus` and is not used.
- **R10.** `whole2-back` has no text probes: its gate absorbs the dump's font and button-picture passes by classification, and its pixel gate takes those passes from the oracle. The new `whole3-back` gates the same transition with text.
- **R11.** The app starts from captured moments: `whole3-menu` frame 0 (main menu) and the first clock-alone frame of `whole3-square` (`--clock`).
- **R12.** Live configuration items: outside an entry the app writes items 6..0xB from local time every frame; inside an entry the menus' writes stand; Clock Adjustment's confirm sets the app's time offset to the confirmed time.

## Captures and fixtures

Captures live in `D:/CodingProjects/Watson/Runtime/captures/`, fixtures in `D:/CodingProjects/CrystalClockVK/References/fixtures/<capture>/`. Checked 2026-10-03.

| Capture | Capture files | `scene.json` | `passes.json` | `f0` | Used by |
|---|---|---|---|---|---|
| `hddosd-110U-whole3-menu` | exist | create (T1) | create (T8) | create (T8) | T1, T4, T5, T6, T8 gate + pixels, T9 start |
| `hddosd-110U-whole3-enter` | exist | create (T1) | create (T8) | create (T8) | T1, T4, T5, T6, T8 gate + pixels, synthetic base |
| `hddosd-110U-whole2-back` | exist | re-export (T1; the old one predates text and menus keys) | create (T8) | create (T8) | T4, T5, T8 gate (no text) + pixels |
| `hddosd-110U-whole3-config` | exist | create (T1) | create (T8) | exists, budget exists | T4, T5, T6, T8 gate + pixels |
| `hddosd-110U-whole3-adjust-hour` | exist | create (T1) | create (T8) | — | T4, T6, T8 gate |
| `hddosd-110U-whole2-to-clock` | exist | re-export (T1) | — | exists | T4, T5 |
| `hddosd-110U-whole2-down`, `-whole2-up`, `-whole2-leave`, `-whole2-adjust-open`, `-whole2-adjust-confirm`, `-whole2-adjust-cancel` | exist | create (T1) | — | — | T4, T5 |
| `hddosd-110U-whole3-clock` | exist | re-export (T1; old keys must stay byte-identical) | exists | exists | ClockParityTest regression |
| `hddosd-110U-whole-menu` | exist | — | — | — | not usable (R9) |
| **`hddosd-110U-whole3-down`** | **create (T2)** | T2 | T8 | — | T6 crossfade, T8 gate |
| **`hddosd-110U-whole3-square`** | **create (T2)** | T2 | T8 | — | T4, T6, T8 gate, T9 `--clock` start |
| **`hddosd-110U-whole3-back`** | **create (T2)** | T2 | T8 | — | T6, T8 gate with text |
| `synthetic-menus/<scenario>` | — | generate (T1) | — | — | T4, T5 |

Commands (Bash, from the worktree unless stated):
- scene: `CLOCK_BUILD=hdd CLOCK_REFERENCES=D:/CodingProjects/CrystalClockVK/References node tools/scene/export_fixture.mjs D:/CodingProjects/Watson/Runtime/captures/<capture>.trace.jsonl`
- passes: `node tools/parity/frame_passes.mjs D:/CodingProjects/Watson/Runtime/captures/<capture>.gs D:/CodingProjects/CrystalClockVK/References/fixtures/<capture>/passes.json`
- oracle frame 0: `node tools/parity/make_fixture.mjs D:/CodingProjects/Watson/Runtime/captures/<capture>.gs D:/CodingProjects/CrystalClockVK/References/fixtures/<capture>/f0`
- synthetic: `CLOCK_BUILD=hdd CLOCK_REFERENCES=D:/CodingProjects/CrystalClockVK/References node tools/scene/menus_synthetic.mjs`

## Lanes

| Wave | Task | Where | Starts after |
|---|---|---|---|
| 0 | Setup: `git merge feat/native-clock` (brings `8d9dd91`, Task 11's fix round), configure, build, `ctest -C Debug` baseline 17/17 | integration `CrystalClockVK-wt/menus` | — |
| A | T1 Exporter for the menus | integration | setup |
| A | T2 Captures to create (Watson, data only; its export step waits for T1) | capture lane (main checkout's `References/scripts`, no commits) | setup |
| A | T7 Input mapping | `CrystalClockVK-wt/menus-input`, `lane/menus-input` | setup |
| — | T3 Menu state types and readers | integration | T1 |
| B | T4 Menus | `CrystalClockVK-wt/menus-state`, `lane/menus-state` | T3 |
| B | T5 Cubes | `CrystalClockVK-wt/menus-cubes`, `lane/menus-cubes` | T3 |
| B | T6 Menu text | `CrystalClockVK-wt/menus-text`, `lane/menus-text` | T3 (its crossfade test also needs T2's `whole3-down` export) |
| — | merge `lane/menus-input`, `lane/menus-state`, `lane/menus-cubes`, `lane/menus-text` | integration | B and T7 |
| C | T8 Frame assembly, carried geometry, pixel gates | integration | merge |
| D | T9 Live navigation, soak, screenshots, PNG sequence | integration | T8 |

Shared files and their owners: `CMakeLists.txt` (each lane appends its own block; the controller merges); `src/scene/SceneInputs.{hpp,cpp}` (append-only: T3 readers, T8 `frameInputs`); `src/scene/Rods.{hpp,cpp}` and `src/scene/FrameHead.{hpp,cpp}` (T5 only); `src/scene/Text.*` (T6 only); `src/scene/Clock.*`, `tests/scene/ClockParityTest.cpp` (T8 only); `src/app/*` (T7 adds `Input.*`, T9 the rest). Lane worktrees are made with `git worktree add` from the integration commit named in the table, configured with the build command of the native clock's `constraints.md` (FETCHCONTENT source dirs and `-DCLOCK_FIXTURE=D:/CodingProjects/CrystalClockVK/References/fixtures/hddosd-110U-whole3-clock/f0`), and initialise imgui with `git submodule update --init --reference D:/CodingProjects/CrystalClockVK/3rdparty/imgui`. The ledger lives in `.superpowers/sdd/2026-10-03-native-menus/progress.md` of the integration worktree.

## File Structure

| Path | Responsibility |
|---|---|
| `tools/scene/instrument.mjs` | new pieces' decoders; wraps `menus`, `between`, `endOfFrame`, `menuStep`, `cubes`; records stages, cube draws, page strings; `CLOCK_REFERENCES` root |
| `tools/scene/export_fixture.mjs` | adds `listFade` from the text probes; `CLOCK_REFERENCES` root |
| `tools/scene/menus_synthetic.mjs` | scripted pad words through the model from `whole3-enter` frame 0 (stages only) |
| `tools/scene/export_menus.test.mjs` | the exporter's tests on menu captures and scenarios |
| `tools/scene/make_start.mjs` | menus' keys, `--frame <n|clock>` |
| `src/scene/MenuTypes.hpp` | `PadWords`, pad bits, `ConfigPage`, `ConfigEntry`, `MainMenu`, `MenusState`, `CubeState`, `MenuExternals`, `MenuWorld` |
| `src/scene/SceneInputs.{hpp,cpp}` | readers of the new keys |
| `tests/scene/StageCompare.{hpp,cpp}` | `StagePieces` from a stage (present keys only) and its exact comparison |
| `src/scene/ClockDate.{hpp,cpp}` | port of `clock_date.mjs` (`secondsOf`, `dateOf`, `dateCheck`) |
| `src/scene/Menus.{hpp,cpp}` | port of `clock_menus.mjs` (HDD), `menuStep`, the crossfade (R4) |
| `src/scene/RodEmitters.hpp` | the rods' refracted, textured, reflected emitters, shared with the cubes |
| `src/scene/Cubes.{hpp,cpp}` | port of `clock_cubes.mjs` (HDD ring) |
| `src/scene/Text.{hpp,cpp}` | `pages()`: menu items, list title, entry, value, arrow, clock value; live `TextRamps` |
| `tests/scene/StringCompare.{hpp,cpp}` | strings and font state compared bit for bit (moved out of ClockParityTest) |
| `src/scene/Clock.{hpp,cpp}` | menus, cubes, page text in the frame's order; `between` before the frame |
| `tests/scene/SceneGate.{hpp,cpp}` | ClockParityTest's dump comparison and scene-fixture writer, shared |
| `tests/scene/MenusParityTest.cpp` | carried frames of a menu capture against its dump and its oracle frame |
| `src/app/Input.{hpp,cpp}` | bindings, `PadReader`, SDL event feeding |
| `src/app/Screens.{hpp,cpp}` | the screen the menus show, for the panel and the soak |
| `src/app/Main.cpp`, `src/app/DebugPanel.{hpp,cpp}` | menus live, panel, soak, recording |
| `resources/menus/start-menu.json`, `resources/menus/start-clock.json` | the app's start states |

---

### Task 1: Exporter for the menus

**Files:**
- Modify: `tools/scene/instrument.mjs`, `tools/scene/export_fixture.mjs`, `tools/scene/export_fixture.test.mjs`
- Create: `tools/scene/menus_synthetic.mjs`, `tools/scene/export_menus.test.mjs`

**Interfaces:**
- Consumes: `References/model/clock_menus.mjs` (`menus`, `between`, `endOfFrame`), `clock_rest.mjs` (`menuStep`), `clock_cubes.mjs` (`cubes`), `References/scripts/verify_frame.mjs` (`verify`, `PROBES`), `References/scripts/verify_text2.mjs` (`PROBES`), `References/model/clock_memory.mjs` (`Memory`), `References/lib/trace.mjs` (`readTraceFor`).
- Produces (schema of `scene.json`, additions; floats are `"0x"` + 8 hex digits, everything else integers):
  - `frames[k].between`: `null` for frame 0, else `{ before, after, notes }` around `between()`; `before`/`after` decode `STAGE_PIECES`.
  - `frames[k].input` gains: `pad {held, pressed, released, repeating}`, `disc`, `screenCode`, `configPage {word0, entries, count, titleWidth, selected, word14, level, ramp, word2c, word30, glow}`, `configRamp`, `configEntries[9] {id, valueCount, valueIndex, item, valueTable, enter, stringCallback, frameCallback, confirm, cancel, focus, rest[3]}`, `mainMenu {word0, items, count, word0c, selected, word14, ramp}`, `versionRamp`, `dialogRamp`, `firstRunRamp`, `pagePointers[5]`, `entryActive`, `menuLengths[3]`, `listConstants {rate, divisor, pulse, standing}` (floats), `adjustFields[6] {item, lowest, highest}`, `configGate`, `configDirty[3]`, `rtcMirror[6]`, `cubeList {pulse (float), pulsed, position, left, speed, slowing}`, `cubeColours {selected, plain, live}`, `cubeRecord` (a rod record), `spin`, `cubeConstants {standingFade, ringFade, twoPi, turn, quarter, minusPi}` (floats), `centreFactors {cube, layer}` (floats), `cubeView`, `cubeScreen` (4x4), `layerClear`, `addRecord`, `halfRecord`, `chainRecord` (rectangles); `listFade {first, second, secondIndex}` when the capture has text probes.
  - `frames[k].expect.stages`: `{ cubes, menuStep, menus, endOfFrame }`, each `{ before, after }` decoded with `STAGE_PIECES`; `menus` also `notes`.
  - `frames[k].expect.cubes`: every packet `cubes()` sent that holds vertices, in order: `{ label, prim, fbp, vertices }` (`fbp` the FRAME_1 page last written, vertices as `gsDecoder` gives them).
  - `frames[k].expect.text.pages`: the pages' strings, as `text` and `hint`.
  - `frames[k].expect.listFade`: the three words at the list function's probe (`0x00231388`, after `func_00230FD8` of that frame), when the frame draws the list.
  - `STAGE_PIECES` = `time, mode, overlayLevel, vignetteRamp, menuRamp, tail, body, appearance, state, scaleTarget, timeFilled, scene, greyRamp, fadeRecord, copyRecord, spriteFade, configPage, configRamp, configEntries, mainMenu, versionRamp, dialogRamp, firstRunRamp, pagePointers, entryActive, menuLengths, listConstants, screenCode, adjustFields, configGate, configDirty, cubeRamp, cubeList, cubeColours, cubeRecord, spin, cubeConstants, centreFactors, cubeView, cubeScreen, layerClear, addRecord, halfRecord, chainRecord, configItems, item0, screen, level, videoMode, mechaconParam, rtcMirror, disc, pad`.
  - Synthetic files `References/fixtures/synthetic-menus/<scenario>/scene.json`: `{ capture: "synthetic-menus-<scenario>", build: "hdd", stagesOnly: true, frames: [{ index, between, input (STAGE_PIECES), expect: { stages, cubes } }] }`.

- [ ] **Step 1: Point the tools at a References root.** In `instrument.mjs` and `export_fixture.mjs`, replace every `'../../References/…'` import with one built from the root:

```js
import { pathToFileURL } from 'node:url';
export const REFERENCES = process.env.CLOCK_REFERENCES
  ? pathToFileURL(`${process.env.CLOCK_REFERENCES.replace(/\\/g, '/').replace(/\/$/, '')}/`).href
  : new URL('../../References/', import.meta.url).href;
const { floatBits, matrix, ints, apply } = await import(`${REFERENCES}model/clock_math.mjs`);
```

In `export_fixture.test.mjs`, take `VERIFY` from the same root: `const REFERENCES = process.env.CLOCK_REFERENCES ?? path.join(ROOT, 'References'); const VERIFY = path.join(REFERENCES, 'scripts/verify_frame.mjs');`.

Run: `CLOCK_REFERENCES=D:/CodingProjects/CrystalClockVK/References node --test tools/scene/export_fixture.test.mjs`
Expected: 5 of 5 pass (the existing tests, now runnable from the worktree without `References/dumps`).

- [ ] **Step 2: Write the failing test** `tools/scene/export_menus.test.mjs`:

```js
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REFERENCES = (process.env.CLOCK_REFERENCES ?? 'D:/CodingProjects/CrystalClockVK/References').replace(/\\/g, '/');
const CAPTURES = 'D:/CodingProjects/Watson/Runtime/captures';
const env = { ...process.env, CLOCK_BUILD: 'hdd', CLOCK_REFERENCES: REFERENCES };
const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'menus-'));
const run = (script, args) => execFileSync(process.execPath, [path.join(HERE, script), ...args], { env, stdio: 'pipe', maxBuffer: 1 << 28 });
const exportOf = (capture) => {
  const out = path.join(dir, `${capture}.json`);
  run('export_fixture.mjs', [`${CAPTURES}/${capture}.trace.jsonl`, out]);
  return JSON.parse(fs.readFileSync(out, 'utf8'));
};
const MENU_KEYS = ['pad', 'disc', 'screenCode', 'configPage', 'configRamp', 'configEntries', 'mainMenu', 'versionRamp', 'dialogRamp', 'firstRunRamp',
  'pagePointers', 'entryActive', 'menuLengths', 'listConstants', 'adjustFields', 'configGate', 'cubeList', 'cubeColours', 'cubeRecord', 'spin',
  'cubeConstants', 'centreFactors', 'cubeView', 'cubeScreen', 'layerClear', 'addRecord', 'halfRecord', 'chainRecord'];
const STAGES = ['cubes', 'menuStep', 'menus', 'endOfFrame'];
const enter = exportOf('hddosd-110U-whole3-enter');

test('every frame of a menu capture holds the menus\' pieces and its stages', () => {
  for (const frame of enter.frames) {
    for (const key of MENU_KEYS) assert.ok(key in frame.input, `frame ${frame.index}: input.${key}`);
    assert.equal(frame.input.configEntries.length, 9);
    for (const stage of STAGES) {
      assert.ok(frame.expect.stages[stage].before.configPage, `frame ${frame.index} ${stage}.before`);
      assert.ok(frame.expect.stages[stage].after.configPage, `frame ${frame.index} ${stage}.after`);
    }
    assert.equal(frame.between === null, frame.index === 0);
  }
});

test('the opening of System Configuration, the cubes and the page strings are recorded', () => {
  const opened = enter.frames.find((f) => f.expect.stages.menus.before.configPage.ramp.state === 0 && f.expect.stages.menus.after.configPage.ramp.state === 1);
  assert.ok(opened, 'no frame starts the page ramp');
  assert.ok(opened.input.pad.pressed & 0x20, 'cross is pressed in the frame that opens the page');
  assert.ok(enter.frames.some((f) => f.expect.cubes.length > 0 && f.expect.cubes[0].label.startsWith('cube')));
  assert.ok(enter.frames.some((f) => f.expect.text.pages.length > 0));
  assert.ok(enter.frames.some((f) => f.expect.listFade));
});

test('whole3-clock keeps every old key byte for byte', () => {
  const fresh = exportOf('hddosd-110U-whole3-clock');
  const installed = JSON.parse(fs.readFileSync(`${REFERENCES}/fixtures/hddosd-110U-whole3-clock/scene.json`, 'utf8'));
  for (const frame of fresh.frames) {
    delete frame.between; delete frame.input.listFade; delete frame.expect.stages; delete frame.expect.cubes; delete frame.expect.listFade;
    if (frame.expect.text) delete frame.expect.text.pages;
  }
  assert.deepEqual(fresh.frames, installed.frames);
});

test('the scripted scenarios run through the model', () => {
  const out = path.join(dir, 'synthetic');
  run('menus_synthetic.mjs', [out]);
  for (const name of ['menu-cursor', 'enter-mash', 'config-wrap', 'square', 'entries', 'back', 'browser', 'long']) {
    const scene = JSON.parse(fs.readFileSync(path.join(out, name, 'scene.json'), 'utf8'));
    assert.equal(scene.stagesOnly, true);
    assert.ok(scene.frames.length > 30, name);
    assert.ok(scene.frames.every((f) => STAGES.every((s) => f.expect.stages[s])), name);
  }
  const square = JSON.parse(fs.readFileSync(path.join(out, 'square', 'scene.json'), 'utf8'));
  const states = square.frames.map((f) => f.expect.stages.menuStep.after.menuRamp.state);
  assert.ok(states.includes(2) && states.lastIndexOf(3) > states.indexOf(2), 'square hides the menu, then shows it');
  const long = JSON.parse(fs.readFileSync(path.join(out, 'long', 'scene.json'), 'utf8'));
  const positions = long.frames.map((f) => f.expect.stages.cubes.after.cubeList.position);
  assert.ok(positions.some((p, i) => i > 0 && p > positions[i - 1] + 90000), 'the list position wraps');
  assert.ok(long.frames.some((f, i) => i > 0 && f.input.spin < long.frames[i - 1].input.spin), 'the spin wraps');
});
```

- [ ] **Step 3: Run it to see it fail.**

Run: `CLOCK_REFERENCES=D:/CodingProjects/CrystalClockVK/References node --test tools/scene/export_menus.test.mjs`
Expected: FAIL, first message `frame 0: input.pad`.

- [ ] **Step 4: Decoders.** Add to `PIECES` in `instrument.mjs` (and `export const STAGE_PIECES = [...]` with the list above):

```js
  pad: (b) => ({ held: b.readUInt32LE(0), pressed: b.readUInt32LE(4), released: b.readUInt32LE(8), repeating: b.readUInt32LE(12) }),
  disc: int,
  screenCode: int,
  configPage: (b) => ({ word0: b.readInt32LE(0), entries: b.readUInt32LE(4), count: b.readInt32LE(8), titleWidth: b.readInt32LE(0xc), selected: b.readInt32LE(0x10),
    word14: b.readInt32LE(0x14), level: b.readInt32LE(0x18), ramp: ramp(b.subarray(0x1c, 0x2c)), word2c: b.readInt32LE(0x2c), word30: b.readInt32LE(0x30), glow: b.readInt32LE(0x34) }),
  configRamp: ramp,
  configEntries: (b) => Array.from({ length: 9 }, (_, n) => { const e = b.subarray(n * 0x38, n * 0x38 + 0x38); return {
    id: e.readInt32LE(0), valueCount: e.readInt32LE(4), valueIndex: e.readInt32LE(8), item: e.readInt32LE(0xc), valueTable: e.readUInt32LE(0x10),
    enter: e.readUInt32LE(0x14), stringCallback: e.readUInt32LE(0x18), frameCallback: e.readUInt32LE(0x1c), confirm: e.readUInt32LE(0x20), cancel: e.readUInt32LE(0x24),
    focus: e.readUInt32LE(0x28), rest: ints(e, 0x2c, 3) }; }),
  mainMenu: (b) => ({ word0: b.readInt32LE(0), items: b.readUInt32LE(4), count: b.readInt32LE(8), word0c: b.readInt32LE(0xc), selected: b.readInt32LE(0x10),
    word14: b.readInt32LE(0x14), ramp: ramp(b.subarray(0x18, 0x28)) }),
  versionRamp: ramp, dialogRamp: ramp, firstRunRamp: ramp,
  pagePointers: (b) => ints(b, 0, 5),
  entryActive: int,
  menuLengths: (b) => ints(b, 0, 3),
  listConstants: named(['rate', 'divisor', 'pulse', 'standing']),
  adjustFields: (b) => Array.from({ length: 6 }, (_, n) => ({ item: b.readInt32LE(n * 12), lowest: b.readInt32LE(n * 12 + 4), highest: b.readInt32LE(n * 12 + 8) })),
  configGate: int,
  configDirty: (b) => ints(b, 0, 3),
  rtcMirror: (b) => ints(b, 0, 6),
  cubeList: (b) => ({ pulse: fl(b, 0), pulsed: b.readInt32LE(4), position: b.readInt32LE(8), left: b.readInt32LE(0xc), speed: b.readInt32LE(0x10), slowing: b.readInt32LE(0x14) }),
  cubeColours: (b) => ({ selected: rgba(b, 0), plain: rgba(b, 0x10), live: rgba(b, 0x20) }),
  cubeRecord: template,
  spin: int,
  cubeConstants: named(['standingFade', 'ringFade', 'twoPi', 'turn', 'quarter', 'minusPi']),
  centreFactors: named(['cube', 'layer']),
  cubeView: (b) => rows(matrix(b, 0)),
  cubeScreen: (b) => rows(matrix(b, 0)),
  layerClear: rgba,
  addRecord: rectangle, halfRecord: rectangle, chainRecord: rectangle,
```

Add `const decodeSome = (m, names) => Object.fromEntries(names.filter((n) => n in PIECES && m.has(n)).map((n) => [n, PIECES[n](m.at(n))]));`. In `gsDecoder`, remember FRAME_1's page (`else if (r === 0x4c) reg.fbp = Number(value & 0x1ffn);`) and return it as `fbp`; the head's records are built field by field, so their JSON is unchanged.

- [ ] **Step 5: Wraps and stages.** Extend `WRAPPED`:

```js
const WRAPPED = {
  'clock_frame.mjs': ['frame', 'scene', 'transform', 'refracted', 'textured', 'reflected'],
  'clock_camera.mjs': ['camera'],
  'clock_menus.mjs': ['menus', 'between', 'endOfFrame'],
  'clock_rest.mjs': ['menuStep'],
  'clock_cubes.mjs': ['cubes'],
};
```

In `recorder(model, { stagesOnly = false } = {})`, before the `if (!current)` line of `probe`:

```js
    if (name === 'between') {
      const m = args[0];
      const before = decodeSome(m, STAGE_PIECES);
      const out = fn(...args);
      pendingBetween = { before, after: decodeSome(m, STAGE_PIECES), notes: [...args[1]] };
      return out;
    }
```

and after it:

```js
    if (['cubes', 'menuStep', 'menus', 'endOfFrame'].includes(name)) {
      const m = name === 'cubes' ? args[0].m : args[0];
      const from = name === 'cubes' ? args[0].packets.length : 0;
      const before = decodeSome(m, STAGE_PIECES);
      const out = fn(...args);
      current.stages[name] = { before, after: decodeSome(m, STAGE_PIECES), ...(name === 'menus' ? { notes: [...args[1]] } : {}) };
      if (name === 'cubes') {
        const decodeWrites = gsDecoder();
        for (const packet of args[0].packets.slice(from)) {
          if (!packet.writes) continue;
          const draw = decodeWrites(packet.writes);
          if (draw.vertices.length) current.cubeDraws.push({ label: packet.label, prim: draw.prim, fbp: draw.fbp, vertices: draw.vertices });
        }
      }
      return out;
    }
```

The frame record starts with `stages: {}, cubeDraws: [], between: pendingBetween` (then `pendingBetween = null`). A stage the model did not call (a capture without the pieces) is recorded as `null`. `finish` adds `between: record.between` to the frame, `stages`, `cubes: record.cubeDraws` to `expect`, and `pages: textStrings(held.strings.pages)` to `text.strings`. With `stagesOnly`, `finish` returns `{ index, between, input: decodeSome(m at entry, STAGE_PIECES), expect: { stages, cubes } }` and skips the rods, orbs and head. `install(options)` passes `options` to `recorder`.

- [ ] **Step 6: The list's crossfade words.** In `export_fixture.mjs`, after `addTextRamps`, add:

```js
/**
 * The list entries' crossfade (D_0037029C first, D_003702A0 second, D_003702A4 second's index; writers func_002316B8
 * and func_00230FD8, CrystalOSD/asm), read at the list function's probe (0x00231388, verify_text2 PROBES, gp words
 * 0x00370130 + 0x1C0). expect.listFade: the frame's own words (after func_00230FD8). input.listFade: the frame
 * before's; frame 0 starts at rest: func_00230FD8 saturates first to 128 and second to 0 within 16 frames, so a
 * capture that starts at rest starts at (128, 0, index).
 */
async function addListFade(traceFile, frames) {
  const { readTraceFor } = await import(`${REFERENCES}lib/trace.mjs`);
  const { PROBES } = await import(`${REFERENCES}scripts/verify_text2.mjs`);
  const probes = readTraceFor(traceFile, PROBES).probes.filter((p) => !p.preroll);
  const word = (probe, address) => { for (const m of probe.mem) if (m.bytes && address >= m.address && address + 4 <= m.address + m.bytes.length) return m.bytes.readInt32LE(address - m.address); return null; };
  let last = null;
  frames.forEach((frame) => {
    const pages = frame.expect.text?.pages ?? [];
    const list = pages.length ? probes.filter((p) => p.pc === 0x00231388 && p.at <= pages.at(-1).at).at(-1) : null;
    const fade = list ? { first: word(list, 0x0037029c), second: word(list, 0x003702a0), secondIndex: word(list, 0x003702a4) } : null;
    if (frame.index === 0) {
      if (fade && (fade.first !== 128 || fade.second !== 0)) throw new Error(`${traceFile}: the list fade moves at frame 0`);
      frame.input.listFade = fade ?? { first: 128, second: 0, secondIndex: 0 };
    } else if (last) frame.input.listFade = last;
    if (fade) { frame.expect.listFade = fade; last = fade; }
  });
}
```

Call it in `exportScene` after `addTextRamps`, only when the frames hold text.

- [ ] **Step 7: The scripted scenarios** `tools/scene/menus_synthetic.mjs`:

```js
// Scripted pad words through the JS model, from the first whole frame of hddosd-110U-whole3-enter (the main menu at
// rest): every scenario starts from that snapshot, writes its pad words before the clock thread's step and the frame
// (verify_frame.mjs --carry's order), holds the time record of frame 0, and records the stages and the cubes' draws.
//   CLOCK_BUILD=hdd CLOCK_REFERENCES=<main References> node tools/scene/menus_synthetic.mjs [outDir]
//   default outDir: <CLOCK_REFERENCES>/fixtures/synthetic-menus
import fs from 'node:fs';
process.env.CLOCK_BUILD ??= 'hdd';
const { install, REFERENCES } = await import('./instrument.mjs');
const recording = await install({ stagesOnly: true });
const { frame, between } = await import(`${REFERENCES}model/clock_frame.mjs`);
const { Memory } = await import(`${REFERENCES}model/clock_memory.mjs`);
const { readTraceFor } = await import(`${REFERENCES}lib/trace.mjs`);
const { PROBES } = await import(`${REFERENCES}scripts/verify_frame.mjs`);

const TRACE = 'D:/CodingProjects/Watson/Runtime/captures/hddosd-110U-whole3-enter.trace.jsonl';
const START = 0x00225e80;
const B = { up: 0x1000, down: 0x4000, cross: 0x20, circle: 0x40, square: 0x80, triangle: 0x10 };
const bits = (json) => Buffer.from(json.bits.map((w) => Buffer.from(w.padStart(8, '0'), 'hex').reverse()).reduce((a, b) => Buffer.concat([a, b])));
const meshOf = (file) => { const j = JSON.parse(fs.readFileSync(file, 'utf8')); return { positions: bits(j.positions), normals: bits(j.normals), coordinates: bits(j.coordinates) }; };
const mesh = { ...meshOf(new URL('../../facts/data/rod-mesh.json', import.meta.url)), cube: meshOf(new URL(`${REFERENCES}model/cube-mesh.json`)) };

function snapshot() {
  const probes = readTraceFor(TRACE, PROBES).probes.filter((p) => !p.preroll);
  const from = probes.findIndex((p) => p.pc === START);
  const to = probes.findIndex((p, i) => i > from && p.pc === START);
  const inside = probes.slice(from, to);
  const blocks = [];
  for (let k = 0; k < 5; k++) blocks.push(...(inside.find((p) => p.pc === START + 4 * k)?.mem ?? []));
  return new Memory('hdd', blocks);
}

/** [frame, button] pairs; each press is one frame of `pressed` and six of `held`. */
const presses = (list) => (k) => list.filter(([at]) => k === at).reduce((w, [, b]) => w | B[b], 0);
const heldOf = (list) => (k) => list.filter(([at]) => k >= at && k < at + 6).reduce((w, [, b]) => w | B[b], 0);
function adjustIndex(m) { const e = m.at('configEntries'); for (let n = 0; n < 9; n++) if (e.readUInt32LE(n * 0x38 + 0x14) === 0x00226fd0) return n; throw new Error('no Clock Adjustment entry'); }
const SCENARIOS = {
  'menu-cursor': () => ({ frames: 40, list: [[2, 'down'], [8, 'down'], [14, 'up'], [20, 'up'], [26, 'up']] }),
  'enter-mash': () => ({ frames: 130, list: [[2, 'down'], [6, 'cross'], ...Array.from({ length: 23 }, (_, i) => [10 + 4 * i, ['cross', 'circle', 'square', 'up', 'down', 'triangle'][i % 6]])] }),
  'config-wrap': () => ({ frames: 200, list: [[2, 'down'], [6, 'cross'], [110, 'up'], [118, 'up'], [126, 'down'], [134, 'down'], [142, 'down'], [150, 'triangle']] }),
  square: () => ({ frames: 300, list: [[2, 'down'], [6, 'cross'], [110, 'square'], [200, 'square']] }),
  entries: (m) => { const a = adjustIndex(m); const downs = Array.from({ length: a }, (_, i) => [150 + 8 * i, 'down']); const t = 150 + 8 * a;
    return { frames: t + 120, list: [[2, 'down'], [6, 'cross'], [110, 'down'], [118, 'cross'], [126, 'cross'], [134, 'up'], ...downs, [t + 8, 'cross'], [t + 20, 'up'], [t + 30, 'circle'], [t + 50, 'cross'], [t + 70, 'cross']] }; },
  back: () => ({ frames: 220, list: [[2, 'down'], [6, 'cross'], [110, 'circle']] }),
  browser: () => ({ frames: 140, list: [[2, 'cross']] }),
  long: () => ({ frames: 420, spin: 0xff00, list: [[2, 'down'], [6, 'cross'], ...Array.from({ length: 70 }, (_, i) => [110 + 4 * i, 'down'])] }),
};

const out = process.argv[2] ?? `${process.env.CLOCK_REFERENCES ?? 'D:/CodingProjects/CrystalClockVK/References'}/fixtures/synthetic-menus`;
for (const [name, make] of Object.entries(SCENARIOS)) {
  const m = snapshot();
  const s = make(m);
  if (s.spin !== undefined) m.at('spin').writeInt32LE(s.spin, 0);
  const pressed = presses(s.list), held = heldOf(s.list);
  const index0 = m.int('index');
  recording.frames.length = 0;
  for (let k = 0; k < s.frames; k++) {
    const pad = m.at('pad');
    pad.writeUInt32LE(held(k), 0); pad.writeUInt32LE(pressed(k), 4); pad.writeUInt32LE(0, 8); pad.writeUInt32LE(pressed(k), 12);
    m.at('scene').writeInt32LE(k & 1, 8);
    m.setInt('index', (index0 + k) & 1);
    if (k > 0) between(m, []);
    frame({ memory: m }, mesh);
  }
  fs.mkdirSync(`${out}/${name}`, { recursive: true });
  fs.writeFileSync(`${out}/${name}/scene.json`, `${JSON.stringify({ capture: `synthetic-menus-${name}`, build: 'hdd', stagesOnly: true, frames: recording.frames })}\n`);
  console.log(`${name}: ${recording.frames.length} frames`);
}
```

The `entries` scenario enters entry 1 (the aspect entry) with cross, confirms with cross (the confirmed value's pulse), enters and cancels it, then moves to Clock Adjustment, enters it, raises a field, cancels, enters and confirms. Mesh bytes follow `verify_frame.mjs meshes()` (little-endian words); if `bits` differs from that function, copy that function's `bytesOf` exactly.

- [ ] **Step 8: Run the tests until they pass.**

Run: `CLOCK_REFERENCES=D:/CodingProjects/CrystalClockVK/References node --test tools/scene/export_menus.test.mjs tools/scene/export_fixture.test.mjs`
Expected: PASS, 9 of 9.

- [ ] **Step 9: Generate the fixtures** for every capture of the table marked T1: the scene command of "Captures and fixtures" for `whole3-menu`, `whole3-enter`, `whole3-config`, `whole3-adjust-hour`, `whole2-back`, `whole2-to-clock`, `whole2-down`, `whole2-up`, `whole2-leave`, `whole2-adjust-open`, `whole2-adjust-confirm`, `whole2-adjust-cancel`, `whole3-clock` (keep a copy of the installed one as `scene.pre-menus.json`), then the synthetic command. Each export prints `<out>: N frames, B bytes`; frame counts equal facts/clock-frame.md (whole3-menu 24, whole3-enter 75, whole3-config 25, whole3-adjust-hour 45, whole2-back 99, whole2-to-clock 79, whole2-down 45, whole2-up 35, whole2-leave 130, whole2-adjust-open 49, -confirm 49, -cancel 49, whole3-clock 25).

- [ ] **Step 10: Regression.** Configure the integration build and run the existing scene tests on the re-exported files:

Run: `cmake -S . -B build && cmake --build build --config Debug && ctest --test-dir build -C Debug --output-on-failure`
Expected: 17 of 17, including `ClockParityTest`, `ParityClockScene` and `HeadTest` (its glob now also takes the new captures; a failure there on a new capture is reported to the controller, not hidden).

- [ ] **Step 11: Commit.**

```bash
git add tools/scene/instrument.mjs tools/scene/export_fixture.mjs tools/scene/export_fixture.test.mjs tools/scene/menus_synthetic.mjs tools/scene/export_menus.test.mjs
git commit -m "Feat(Project): Export the menus' stages, cubes and page strings"
```

### Task 2: The captures to create

**Files:** none in the repository. Data: three captures in `D:/CodingProjects/Watson/Runtime/captures/`, their fixtures in the main checkout's `References/fixtures/`.

**Interfaces:**
- Consumes: the `capture` skill (`References/scripts/with_emulator.mjs`, `capture.mjs`), `verify_frame.mjs`, `verify_text2.mjs`; Task 1's exporter.
- Produces: `hddosd-110U-whole3-down`, `hddosd-110U-whole3-square`, `hddosd-110U-whole3-back` (`.gs`, `.trace.jsonl`, `.png`), each FOUND by both verifiers, and their `scene.json`.

- [ ] **Step 1: Capture System Configuration, down** (from `D:/CodingProjects/CrystalClockVK`, Bash):

```bash
node References/scripts/with_emulator.mjs node References/scripts/capture.mjs --verifiers verify_frame.mjs,verify_text2.mjs --build hdd --state config --frames 45 --name hddosd-110U-whole3-down --pad '[{"frame":3,"press":["down"],"frames":6}]'
```

- [ ] **Step 2: Capture Square hide, then show:**

```bash
node References/scripts/with_emulator.mjs node References/scripts/capture.mjs --verifiers verify_frame.mjs,verify_text2.mjs --build hdd --state config --frames 200 --name hddosd-110U-whole3-square --pad '[{"frame":3,"press":["square"],"frames":6},{"frame":110,"press":["square"],"frames":6}]'
```

- [ ] **Step 3: Capture System Configuration back to the main menu:**

```bash
node References/scripts/with_emulator.mjs node References/scripts/capture.mjs --verifiers verify_frame.mjs,verify_text2.mjs --build hdd --state config --frames 100 --name hddosd-110U-whole3-back --pad '[{"frame":3,"press":["circle"],"frames":6}]'
```

- [ ] **Step 4: Verify each** (`<c>` = each of the three names):

Run: `CLOCK_BUILD=hdd node References/scripts/verify_frame.mjs D:/CodingProjects/Watson/Runtime/captures/<c>.trace.jsonl --carry`
Expected: `verdict: FOUND N frames`, `every piece equal in every frame`, no `event:` line, text packets all equal.
Run: `CLOCK_BUILD=hdd node References/scripts/verify_text2.mjs D:/CodingProjects/Watson/Runtime/captures/<c>.trace.jsonl`
Expected: FOUND, places, alphas, panels and entries all equal.
A capture that is not FOUND is not used: report its output to the controller (a model gap is a measurement item, not a port item).

- [ ] **Step 5: Export** (after Task 1 has merged): the scene command for each of the three. Check in the output: `whole3-down` has a frame whose `expect.listFade.first` is below 128; `whole3-square` has `menuRamp.state` 2 in some frame's input and a later frame with state 3; `whole3-back` has `configPage.ramp.state` 3.

- [ ] **Step 6: Record** the commands, verdict lines and frame counts in the task report. No commit (data only). Adding rows for the new captures to `facts/clock-frame.md` is a facts change (skill `facts-page`) and Jean's call.

### Task 3: Menu state types and readers

**Files:**
- Create: `src/scene/MenuTypes.hpp`, `tests/scene/StageCompare.hpp`, `tests/scene/StageCompare.cpp`, `tests/scene/MenuInputsTest.cpp`
- Modify: `src/scene/Ramp.hpp` (equality), `src/scene/SceneInputs.hpp`, `src/scene/SceneInputs.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1's schema; `scene::ClockState`, `scene::HeadState`, `scene::RodRecord`, `scene::Rect`, `scene::Ramp`, `scene::Colour`, `scene::Mat4`.
- Produces (`src/scene/MenuTypes.hpp`):

```cpp
#pragma once
#include <array>
#include <cstdint>
#include <optional>

#include "scene/ClockState.hpp"
#include "scene/FrameHead.hpp"
#include "scene/Matrix.hpp"
#include "scene/Ramp.hpp"
#include "scene/Rods.hpp"

namespace scene {

// The pad words the pad reader (HDD func_00235EB0) leaves at 0x00370330: held, pressed, released, repeating.
struct PadWords {
    uint32_t held = 0, pressed = 0, released = 0, repeating = 0;
    bool operator==(const PadWords&) const = default;
};
// The bits the menus' code tests (References/model/clock_menus.mjs).
namespace pad {
constexpr uint32_t Triangle = 0x10, Cross = 0x20, Circle = 0x40, Square = 0x80;
constexpr uint32_t Up = 0x1000, Right = 0x2000, Down = 0x4000, Left = 0x8000;
}

// clock_memory.mjs configEntries: one entry of System Configuration's list, 0x38 bytes; callbacks are EE addresses.
struct ConfigEntry {
    int32_t id = 0, valueCount = 0, valueIndex = 0, item = 0;
    uint32_t valueTable = 0, enter = 0, stringCallback = 0, frameCallback = 0, confirm = 0, cancel = 0, focus = 0;
    std::array<int32_t, 3> rest{};
    bool operator==(const ConfigEntry&) const = default;
};
// clock_memory.mjs configPage (HDD 0x002B2DE8): level 0 the list, 1 inside an entry, 2 waiting for the save.
struct ConfigPage {
    int32_t word0 = 0;
    uint32_t entries = 0;
    int32_t count = 0, titleWidth = 0, selected = 0, word14 = 0, level = 0;
    Ramp ramp;
    int32_t word2c = 0, word30 = 0, glow = 0;
    bool operator==(const ConfigPage&) const = default;
};
// clock_memory.mjs mainMenu (HDD 0x002B2E60).
struct MainMenu {
    int32_t word0 = 0;
    uint32_t items = 0;
    int32_t count = 0, word0c = 0, selected = 0, word14 = 0;
    Ramp ramp;
    bool operator==(const MainMenu&) const = default;
};
struct AdjustField {
    int32_t item = 0, lowest = 0, highest = 0;
    bool operator==(const AdjustField&) const = default;
};
// clock_memory.mjs listConstants: the frame rate, the arrow's divisor, the ring's pulse, the standing cube's pulse.
struct ListConstants { float rate = 0, divisor = 0, pulse = 0, standing = 0; };
// D_0037029C, D_003702A0, D_003702A4 (R4): the list entries' crossfade.
struct ListFade {
    int32_t first = 128, second = 0, secondIndex = 0;
    bool operator==(const ListFade&) const = default;
};
using ConfigItems = std::array<int32_t, 20>;

// What the menus own (clock_menus.mjs); the clock owns the rest of what they write (MenuWorld).
struct MenusState {
    ConfigPage page;
    std::array<ConfigEntry, 9> entries{};
    MainMenu mainMenu;
    Ramp versionRamp, dialogRamp, firstRunRamp;
    std::array<int32_t, 5> pagePointers{};
    int32_t entryActive = 0;
    std::array<int32_t, 3> menuLengths{};
    ListConstants listConstants;
    int32_t screenCode = 0;
    std::array<AdjustField, 6> adjustFields{};
    std::optional<int32_t> configGate;
    ListFade listFade;
    int32_t body = 0;
    int32_t videoMode = 0;
};

// clock_memory.mjs cubeList: pulse, pulsed place, position, left to go, speed, slowing.
struct CubeList { float pulse = 0; int32_t pulsed = 0, position = 0, left = 0, speed = 0, slowing = 0; };
struct CubeColours { Colour selected{}, plain{}, live{}; };
// The cubes' state (clock_cubes.mjs); the spin is ClockState::spin.
struct CubeState {
    Ramp ramp;
    CubeList list;
    CubeColours colours;
    RodRecord record;
    std::array<float, 6> constants{};
    std::array<float, 2> centreFactors{};
    Mat4 view{}, screen{};
    Colour layerClear{};
    Rect added, half, chain;
};

// verify_frame.mjs EXTERNAL, the menus' part: taken from outside every frame.
struct MenuExternals {
    PadWords pad;
    int32_t disc = 0;
    std::optional<std::array<int32_t, 3>> configDirty;
    std::optional<std::array<int32_t, 6>> rtcMirror;
    std::optional<std::array<uint32_t, 2>> mechaconParam;
};

// Everything the menus' code reads and writes; configuration item 0 is items[0] (the same word, 0x00409130).
struct MenuWorld {
    ClockState& clock;
    HeadState& head;
    Ramp& spriteFade;
    MenusState& menus;
    CubeState& cubes;
    ConfigItems& items;
    int32_t width = 640, height = 224;
};

}
```

- Produces (`src/scene/SceneInputs.hpp`, appended):

```cpp
bool hasMenus(const nlohmann::json& input);                 // input holds configPage
MenusState menusState(const nlohmann::json& input);          // listFade defaults to (128, 0, 0) when absent
CubeState cubeState(const nlohmann::json& input);
MenuExternals menuExternals(const nlohmann::json& input);
ConfigItems configItems(const nlohmann::json& input);
PadWords padWords(const nlohmann::json& pad);
ListFade listFade(const nlohmann::json& fade);
```

- Produces (`tests/scene/StageCompare.hpp`):

```cpp
// A stage of scene.json (tools/scene/instrument.mjs STAGE_PIECES) as owned values; keys absent from the stage keep
// their defaults. `world()` refers to the members.
struct StagePieces {
    scene::ClockState clock;
    scene::HeadState head;
    scene::Ramp spriteFade;
    scene::MenusState menus;
    scene::CubeState cubes;
    scene::ConfigItems items{};
    int32_t width = 640, height = 224;
    scene::MenuWorld world() { return {clock, head, spriteFade, menus, cubes, items, width, height}; }
};
StagePieces stagePieces(const nlohmann::json& stage);
// Every key of `expected` compared with `ours`, floats by their bits; one line per difference ("cubeList.position: 3000 vs 6000").
std::vector<std::string> stageDifferences(const StagePieces& ours, const nlohmann::json& expected);
```

- [ ] **Step 1: Write the failing test** `tests/scene/MenuInputsTest.cpp`:

```cpp
#include <cstdio>
#include <string>

#include "SceneFixture.hpp"
#include "StageCompare.hpp"
#include "scene/SceneInputs.hpp"

namespace {

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 1;                                                            \
        }                                                                        \
    } while (0)

int sceneFile(const std::string& path) {
    const nlohmann::json scene = scenetest::loadScene(path);
    size_t frames = 0;
    for (const auto& frame : scene.at("frames")) {
        const std::string at = path + " frame " + std::to_string(frame.at("index").get<int>());
        const auto& input = frame.at("input");
        CHECK(scene::hasMenus(input));
        const scene::MenusState menus = scene::menusState(input);
        CHECK(menus.page.ramp.state == input.at("configRamp").at("state").get<int32_t>());
        CHECK(menus.page.ramp.counter == input.at("configRamp").at("counter").get<int32_t>());
        CHECK(menus.page.entries >= 0x002b2bf0u && (menus.page.entries - 0x002b2bf0u) % 0x38 == 0);
        CHECK(menus.mainMenu.count == 2);
        const scene::ConfigItems items = scene::configItems(input);
        CHECK(items[0] == input.at("item0").get<int32_t>());
        // A stage read and compared with itself has no difference; a changed field is found.
        for (const char* stage : {"cubes", "menuStep", "menus", "endOfFrame"}) {
            const auto& pieces = frame.at("expect").at("stages").at(stage).at("before");
            StagePieces s = stagePieces(pieces);
            const auto same = stageDifferences(s, pieces);
            if (!same.empty()) { std::fprintf(stderr, "%s %s: %s\n", at.c_str(), stage, same.front().c_str()); return 1; }
            s.cubes.list.position += 1;
            s.menus.page.glow += 1;
            CHECK(stageDifferences(s, pieces).size() == 2);
        }
        ++frames;
    }
    std::printf("%s: %zu frames read\n", path.c_str(), frames);
    return 0;
}

}

int main(int argc, char** argv) {
    return scenetest::run(argc, argv, [](int count, char** arguments) {
        for (int i = 1; i < count; ++i)
            if (int failed = sceneFile(arguments[i])) return failed;
        return 0;
    });
}
```

Add to `CMakeLists.txt` after the `ClockParityTest` block:

```cmake
get_filename_component(MENU_FIXTURES ${CLOCK_FIXTURE}/../.. ABSOLUTE)
file(GLOB MENU_SCENES CONFIGURE_DEPENDS ${MENU_FIXTURES}/hddosd-110U-whole3-*/scene.json ${MENU_FIXTURES}/hddosd-110U-whole2-*/scene.json ${MENU_FIXTURES}/synthetic-menus/*/scene.json)
list(FILTER MENU_SCENES EXCLUDE REGEX "whole3-clock")
add_library(StageCompare STATIC tests/scene/StageCompare.cpp)
target_link_libraries(StageCompare PUBLIC SceneTestSupport)
add_executable(MenuInputsTest tests/scene/MenuInputsTest.cpp)
target_link_libraries(MenuInputsTest PRIVATE StageCompare)
if(MENU_SCENES)
    add_test(NAME MenuInputsTest COMMAND MenuInputsTest ${MENU_SCENES})
    list(LENGTH MENU_SCENES MENU_SCENE_COUNT)
    message(STATUS "menu scenes active: ${MENU_SCENE_COUNT}")
else()
    message(STATUS "menu scenes inactive: none under ${MENU_FIXTURES}")
endif()
```

`synthetic-menus` scenes lack `configRamp` and `item0` in `input` only when the model left them out; they carry every `STAGE_PIECES` key, so the checks above hold for them too.

- [ ] **Step 2: Run it to see it fail.**

Run: `cmake -S . -B build && cmake --build build --config Debug --target MenuInputsTest`
Expected: FAIL to compile: `'hasMenus': is not a member of 'scene'`.

- [ ] **Step 3: Implement** `MenuTypes.hpp` (above), `bool operator==(const Ramp&) const = default;` in `Ramp`, the readers in `SceneInputs.cpp` (each key as Task 1's decoder writes it, floats through `hexFloat`, matrices through `hexMat4`, the rod record through the existing `rodRecord`, rectangles through the existing `rect`; `menuExternals` sets each optional only when its key is present), and `StageCompare.cpp` (`stagePieces` reads every key that is present into its owner: `time`, `mode`, `overlayLevel`, `vignetteRamp`, `menuRamp`, `tail`, `appearance`, `state`, `scaleTarget`, `timeFilled`, `scene`, `level`, `spin` into `clock`; `greyRamp`, `fadeRecord`, `copyRecord` into `head`; `spriteFade`; the menus' keys into `menus` (`configRamp` must equal `configPage.ramp`, else throw); the cubes' keys into `cubes`; `configItems` then `item0` into `items` (they must agree, else throw); `screen` into `width`/`height`. `stageDifferences` compares the same keys back; `disc`, `pad`, `rtcMirror`, `mechaconParam`, `configDirty`, `videoMode`, `body`, `menuLengths`, `listConstants`, `cubeConstants`, `centreFactors`, `cubeView`, `cubeScreen` are compared against the values read, so a reader that drops a key fails).

- [ ] **Step 4: Run it to see it pass.**

Run: `cmake --build build --config Debug --target MenuInputsTest && ctest --test-dir build -C Debug -R MenuInputsTest --output-on-failure`
Expected: PASS, one `frames read` line per scene file (24 for whole3-menu, 75 for whole3-enter, …).

- [ ] **Step 5: Commit.**

```bash
git add src/scene/MenuTypes.hpp src/scene/Ramp.hpp src/scene/SceneInputs.hpp src/scene/SceneInputs.cpp tests/scene/StageCompare.hpp tests/scene/StageCompare.cpp tests/scene/MenuInputsTest.cpp CMakeLists.txt
git commit -m "Feat(Scene): Read the menus' and cubes' state from the fixtures"
```

### Task 4: Menus

**Files:**
- Create: `src/scene/ClockDate.hpp`, `src/scene/ClockDate.cpp`, `src/scene/Menus.hpp`, `src/scene/Menus.cpp`, `tests/scene/MenusTest.cpp`
- Modify: `CMakeLists.txt` (sources in `ClockScene` and `ClockSceneNative`; the test)

**Interfaces:**
- Consumes: Task 3's `MenuTypes.hpp`, `StagePieces`, `stageDifferences`, `menuExternals`, `padWords`; `scene::tickRamp`.
- Produces:

```cpp
// src/scene/ClockDate.hpp — References/model/clock_date.mjs (HDD OSD 1.10U).
namespace scene {
int64_t secondsOf(int32_t year, int32_t month, int32_t day, int32_t hour, int32_t minute, int32_t second);
std::array<int32_t, 6> dateOf(int64_t seconds);
constexpr int32_t kBaseZone = 540;
struct DateCheck { std::array<int32_t, 6> items{}; std::array<std::array<int32_t, 2>, 6> ranges{}; };
DateCheck dateCheck(const std::array<int32_t, 6>& items, uint32_t settings);
}

// src/scene/Menus.hpp
namespace scene {
struct MenusOptions { bool browserEnters = true; };
// References/model/clock_menus.mjs (HDD OSD 1.10U) and clock_rest.mjs menuStep: pad words in, ramps and modes out.
class Menus {
public:
    explicit Menus(MenusOptions options = {}) : m_options(options) {}
    static void menuStep(MenuWorld& world, const MenuExternals& ext);                                         // clock_rest.mjs menuStep
    void step(MenuWorld& world, const MenuExternals& ext, std::vector<std::string>& notes) const;             // menus()
    static void between(MenuWorld& world, const MenuExternals& ext, std::vector<std::string>& notes);        // between()
    static void endOfFrame(MenuWorld& world, const MenuExternals& ext);                                       // endOfFrame()
    static void setMode(MenuWorld& world, int32_t mode);                                                      // func_00234C28
    static void show(Ramp& ramp);                                                                             // func_00234AC0
    static void hide(Ramp& ramp);                                                                             // func_00234AE0
    const MenusOptions& options() const { return m_options; }
private:
    MenusOptions m_options;
};
}
```

Port map (one C++ function per model function, same order of reads and writes; HDD branches only):

| Model | C++ (in `Menus.cpp`, anonymous namespace unless public) |
|---|---|
| `show`, `hide`, `spritesHide`, `spritesShow`, `appearance(m, up)` | `Menus::show`, `Menus::hide`, `spritesHide`, `spritesShow`, `appearance(world, up)` (writes `clock.appearance` and `clock.state.rods[(i + currentRod) % 12].appearance = 0`) |
| `setMode` | `Menus::setMode` (fade = `head.fade`: x1 = width << 4, y1 = height << 4, colour per mode) |
| `aspectOf`, `reloadItem0`, `endOfFrame`, `dirty`, `setGate` | same names; `items[0]` is item 0 |
| `CALLBACKS.hdd`, `timeFromItems`, `timeFromClock`, `orderFields`, `adjustmentOpens`, `checkDate`, `adjustmentFrame`, `valueIndex`, `stringCallback`, `callback` | same names; addresses as constants: `kAspectConfirm 0x00227d30`, `kAdjustEnter 0x00226fd0`, `kAdjustConfirm 0x00227b90`, `kAdjustCancel 0x00227be8`, `kEditor 0x00227ad0`, `kAdjustString 0x00227420`, `kItemString 0x00228470`, `kConfigEntries 0x002b2bf0`; the selected entry is `entries[(page.entries - kConfigEntries) / 0x38 + page.selected]` (throw when outside 0..8) |
| `listMove`, `listAlpha`, `menuAlpha`, `cubesUp` (HDD: `show(cubes.ramp)`) | same names |
| `configPage` | `configPage(world, ext, notes)`; after the glow, the crossfade step of R4 (`listFade.first = clamp(first + 8)`, `listFade.second = clamp(second - 8)`, each floored at 0 and capped at 128) |
| `listInput` | `listInput`; on up and down, after `listMove` and before the selected entry moves: `listFade.secondIndex = page.selected; listFade.second = listFade.first; listFade.first = 0;` (R4) |
| `entryInput` | `entryInput` (HDD: pulse, `level = dirty ? 2 : 0`, gate 1) |
| `nothingElse`, `mainMenu`, `startSystemConfiguration` | same names; in `mainMenu`, cross on item 0 with mode 0 hides the ramp only when `m_options.browserEnters` |
| `menus` | `Menus::step` (notes for version, dialog, first-run ramps not hidden; tick version ramp; `configPage`; `mainMenu`; tick first-run and dialog ramps) |
| `between` | `Menus::between` (screen code 0x74 kept; `disc` from `ext`) |
| `menuStep` (clock_rest.mjs) | `Menus::menuStep` (HDD: no cube mode) |

The `+0x28` focus callbacks that `func_002316B8` calls on up and down are not in the model; zero events on `whole2-down`/`whole2-up` show they write nothing the model keeps. Not ported.

- [ ] **Step 1: Write the failing test** `tests/scene/MenusTest.cpp`:

```cpp
#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "SceneFixture.hpp"
#include "StageCompare.hpp"
#include "scene/ClockDate.hpp"
#include "scene/Menus.hpp"
#include "scene/SceneInputs.hpp"

namespace {

using nlohmann::json;

int report(const std::string& at, const std::vector<std::string>& differences) {
    if (differences.empty()) return 0;
    std::fprintf(stderr, "%s: %zu differences, first: %s\n", at.c_str(), differences.size(), differences.front().c_str());
    return 1;
}

// Each stage of each frame from its own `before`, compared with its `after`.
int stages(const std::string& path, const scene::Menus& menus) {
    const json scene = scenetest::loadScene(path);
    size_t frames = 0;
    for (const json& frame : scene.at("frames")) {
        const std::string at = path + " frame " + std::to_string(frame.at("index").get<int>());
        const scene::MenuExternals ext = scene::menuExternals(frame.at("input"));
        const json& s = frame.at("expect").at("stages");
        std::vector<std::string> notes;
        if (!frame.at("between").is_null()) {
            StagePieces p = stagePieces(frame.at("between").at("before"));
            auto world = p.world();
            scene::Menus::between(world, ext, notes);
            if (report(at + " between", stageDifferences(p, frame.at("between").at("after")))) return 1;
        }
        if (!s.at("menuStep").is_null()) {
            StagePieces p = stagePieces(s.at("menuStep").at("before"));
            auto world = p.world();
            scene::Menus::menuStep(world, ext);
            if (report(at + " menuStep", stageDifferences(p, s.at("menuStep").at("after")))) return 1;
        }
        {
            StagePieces p = stagePieces(s.at("menus").at("before"));
            if (frame.at("input").contains("listFade")) p.menus.listFade = scene::listFade(frame.at("input").at("listFade"));
            auto world = p.world();
            notes.clear();
            menus.step(world, ext, notes);
            if (report(at + " menus", stageDifferences(p, s.at("menus").at("after")))) return 1;
            if (notes.size() != s.at("menus").at("notes").size()) { std::fprintf(stderr, "%s: %zu notes, the model %zu\n", at.c_str(), notes.size(), s.at("menus").at("notes").size()); return 1; }
            if (frame.at("expect").contains("listFade") && !(p.menus.listFade == scene::listFade(frame.at("expect").at("listFade")))) {
                std::fprintf(stderr, "%s: list fade %d %d %d, the console %s\n", at.c_str(), p.menus.listFade.first, p.menus.listFade.second, p.menus.listFade.secondIndex,
                             frame.at("expect").at("listFade").dump().c_str());
                return 1;
            }
        }
        {
            StagePieces p = stagePieces(s.at("endOfFrame").at("before"));
            auto world = p.world();
            scene::Menus::endOfFrame(world, ext);
            if (report(at + " endOfFrame", stageDifferences(p, s.at("endOfFrame").at("after")))) return 1;
        }
        ++frames;
    }
    std::printf("%s: %zu frames, every stage equal\n", path.c_str(), frames);
    return 0;
}

// Review Focus 2: with the option off, cross on Browser changes nothing the menus own and never starts leaving.
int browserSuppressed(const std::string& path) {
    const json scene = scenetest::loadScene(path);
    const scene::Menus app({false});
    const json& first = scene.at("frames").at(0);
    StagePieces p = stagePieces(first.at("expect").at("stages").at("menus").at("before"));
    for (const json& frame : scene.at("frames")) {
        auto world = p.world();
        std::vector<std::string> notes;
        const scene::MenuExternals ext = scene::menuExternals(frame.at("input"));
        if (frame.at("index").get<int>() > 0) scene::Menus::between(world, ext, notes);
        app.step(world, ext, notes);
        if (p.clock.mode == 3 || p.clock.scene.leaving != 0 || p.menus.screenCode == 9999 || p.menus.mainMenu.ramp.state == 3) {
            std::fprintf(stderr, "%s frame %d: Browser entered (mode %d, leaving %d, screen code %d, menu ramp %d)\n", path.c_str(), frame.at("index").get<int>(),
                         p.clock.mode, p.clock.scene.leaving, p.menus.screenCode, p.menus.mainMenu.ramp.state);
            return 1;
        }
    }
    std::printf("%s: Browser not entered with the option off\n", path.c_str());
    return 0;
}

int dates() {
    const int64_t s = scene::secondsOf(2026, 10, 3, 23, 59, 59);
    const auto back = scene::dateOf(s + 1);
    if (back != std::array<int32_t, 6>{2026, 10, 4, 0, 0, 0}) { std::fprintf(stderr, "dateOf\n"); return 1; }
    const scene::DateCheck c = scene::dateCheck({2024, 2, 31, 0, 0, 0}, 0x07000010u);
    if (c.items[2] != 29 || c.ranges[2][1] != 29) { std::fprintf(stderr, "dateCheck: day %d, range %d\n", c.items[2], c.ranges[2][1]); return 1; }
    return 0;
}

}

int main(int argc, char** argv) {
    return scenetest::run(argc, argv, [](int count, char** arguments) {
        if (int failed = dates()) return failed;
        const scene::Menus model;
        for (int i = 1; i < count; ++i) {
            const std::string path = arguments[i];
            if (int failed = stages(path, model)) return failed;
            if (path.find("synthetic-menus/browser") != std::string::npos || path.find("synthetic-menus\\browser") != std::string::npos)
                if (int failed = browserSuppressed(path)) return failed;
        }
        return 0;
    });
}
```

The two `dates()` expectations are the model's: run `node -e "import('file:///D:/CodingProjects/CrystalClockVK/References/model/clock_date.mjs').then(d => console.log(d.dateOf(d.secondsOf(2026,10,3,23,59,59) + 1n), JSON.stringify(d.dateCheck([2024,2,31,0,0,0], 0x07000010))))"` first (`secondsOf` takes numbers and returns a BigInt, as `timeFromClock` calls it) and put its printed values in the test if they differ from the ones above: the model decides.

CMake:

```cmake
target_sources(ClockScene PRIVATE src/scene/ClockDate.cpp src/scene/Menus.cpp)
target_sources(ClockSceneNative PRIVATE src/scene/ClockDate.cpp src/scene/Menus.cpp)
add_executable(MenusTest tests/scene/MenusTest.cpp)
target_link_libraries(MenusTest PRIVATE StageCompare)
if(MENU_SCENES)
    add_test(NAME MenusTest COMMAND MenusTest ${MENU_SCENES})
endif()
```

- [ ] **Step 2: Run it to see it fail.**

Run: `cmake -S . -B build && cmake --build build --config Debug --target MenusTest`
Expected: FAIL to compile (`scene/Menus.hpp` not found). Then add empty stubs (`step`, `between`, `menuStep`, `endOfFrame` doing nothing) and run `ctest --test-dir build -C Debug -R MenusTest --output-on-failure`.
Expected: FAIL, `... whole2-back... frame 1 between: N differences` or `frame 0 menus: ... configPage.ramp.counter`.

- [ ] **Step 3: Implement** `ClockDate.cpp` (port of `clock_date.mjs`: `leap`, `secondsOf`, `dateOf`, `zoned`, `daysIn`, `dateCheck`, 64-bit integers for the BigInt arithmetic, `/` and `%` truncating as `__divdi3`) and `Menus.cpp` by the port map.

- [ ] **Step 4: Run it to see it pass.**

Run: `cmake --build build --config Debug --target MenusTest && ctest --test-dir build -C Debug -R MenusTest --output-on-failure`
Expected: PASS: one `every stage equal` line per scene file (the twelve captures, the three new ones when present, the eight scenarios) and `Browser not entered with the option off`.

- [ ] **Step 5: Prove the test bites.** Each mutation, built and run alone, must fail `MenusTest` with the stage named; revert each:
  1. `listInput`: up does not wrap to `count - 1` → `config-wrap ... menus`.
  2. The crossfade step adds 7 → `whole3-down ... list fade` (or, before T2's export lands, `synthetic` is silent: record which file failed).
  3. `mainMenu`: down wraps past the last item → `menu-cursor ... menus`.
  4. `between`: screen code 9999 written always → `browser ... between`.
  5. `menuStep`: square accepted while the ramp rises → `square` or `enter-mash ... menuStep`.
  Record the five failure lines in the report.

- [ ] **Step 6: Commit.**

```bash
git add src/scene/ClockDate.hpp src/scene/ClockDate.cpp src/scene/Menus.hpp src/scene/Menus.cpp tests/scene/MenusTest.cpp CMakeLists.txt
git commit -m "Feat(Scene): Port the menus' code"
```

### Task 5: Cubes

**Files:**
- Create: `src/scene/RodEmitters.hpp`, `src/scene/Cubes.hpp`, `src/scene/Cubes.cpp`, `tests/scene/CubesTest.cpp`
- Modify: `src/scene/Rods.cpp` (the `Emit<A>` struct moves to `RodEmitters.hpp`, unchanged), `src/scene/FrameHead.hpp`, `src/scene/FrameHead.cpp` (expose the rectangle helper), `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 3's `CubeState`, `StagePieces`, `stageDifferences`; `Rods<A>::transform`, `Rods<A>::edgeTerm`, `scene::loadRodMesh` (the cube mesh has the rod mesh's format: `References/model/cube-mesh.json`, tracked in the repository), `cosf` of `EeArithmetic`/`NativeArithmetic`.
- Produces:

```cpp
// src/scene/RodEmitters.hpp — the rods' emitters (facts/clock-rod-draw.md), shared with the cubes (facts/config-cubes.md).
template <class A>
struct RodEmitters {
    const RodsInput& input;
    RodFaceDraw refracted(const RodFace& face, const RodRecord& rod, float cx, float cy, int32_t extra, RodPiece piece) const;
    RodFaceDraw textured(const RodFace& face, const Colour& colour, float ds, float dt, RodPiece piece) const;
    RodFaceDraw reflected(const RodFace& face, const Colour& colour, RodPiece piece, bool edgeSmoothing = false) const;
};

// src/scene/FrameHead.hpp (added): the two packets of a rectangle record (HDD func_00233770), as FrameHead draws them.
enum class Part { Background, Blur, Copy, Tint, Vignette, Fade, BlurAfter, Bars, Column, CubeHalf, CubeAdded, CubeChainShrink, CubeChainStretch, CubeToDisplay };
HeadDraw headRectangle(const Rect& record, int32_t width, int32_t height, Part part);

// src/scene/Cubes.hpp — References/model/clock_cubes.mjs, HDD OSD 1.10U (the ring of six).
enum class CubeSend { RefractedFar, GrainOffsetFar, GrainPlainFar, EdgeColour, Depth, HalfBuffer, RefractedNear, GrainOffsetNear, GrainPlainNear,
                      LayerClear, LayerReflection, LayerAlpha, Added, ChainShrink, ChainStretch, ToDisplay };
struct CubeDraw {
    CubeSend send = CubeSend::RefractedFar;
    std::string label;                    // the model's packet label ("cube: refracted", "cube layer: alpha", ...)
    std::vector<RodFaceDraw> faces;       // strip sends: one strip of four per face, native units as RodsFrame
    std::optional<HeadDraw> rectangle;    // HalfBuffer, Added, ChainShrink, ChainStretch, ToDisplay
    Colour clear{};                       // LayerClear
};
struct CubeFrameInputs { int32_t width = 640, height = 224, field = 0, body = 0; };
template <class A>
class Cubes {
public:
    explicit Cubes(RodMesh mesh);
    // cubes(): tick the ramp, one step of the list, then (ramp not idle) every cube, the layer, the buffers onto the display.
    std::vector<CubeDraw> frame(CubeState& cubes, HeadState& head, const ClockState& clock, const CubeFrameInputs& in);
};
```

Port map: `ringStep`, `wrap`, `mix`, `place`, `turned`, `cube` (the eight sends, `centreFactors.cube`), `layer` (`reflected` with edge smoothing for PRIM 0x194, the alpha quad), `chain(level < 5 ? 5 - level : 0)` and the three rectangles, in `clock_cubes.mjs` order. The halfRecord, addRecord, chainRecord and `head.copy` (`copyRecord`, blend 1) are written as the model writes them. The fourth word of the placement vector is 0 (facts/config-cubes.md). The standing cubes and every `!hdd` branch are left out.

- [ ] **Step 1: Write the failing test** `tests/scene/CubesTest.cpp`:

```cpp
#include <cstdio>
#include <string>

#include "SceneFixture.hpp"
#include "StageCompare.hpp"
#include "scene/Cubes.hpp"
#include "scene/SceneInputs.hpp"

namespace {

using nlohmann::json;

// A native strip vertex back to the GS value the model sent: 12.4 x, y with the screen's offset, the 24-bit depth.
bool sameVertex(const scene::RodVertex& v, const json& gs, int32_t width, int32_t height) {
    const int32_t ox = (0x800 - (width >> 1)) << 4, oy = (0x800 - (height >> 1)) << 4;
    return static_cast<int32_t>(v.x * 16.0f) + ox == gs.at("x").get<int32_t>() && static_cast<int32_t>(v.y * 16.0f) + oy == gs.at("y").get<int32_t>() &&
           (static_cast<uint32_t>(static_cast<int64_t>(v.z * 16.0f)) & 0xffffff) == gs.at("z").get<uint32_t>();
}

int draws(const std::vector<scene::CubeDraw>& ours, const json& theirs, int32_t width, int32_t height, const std::string& at) {
    size_t k = 0;
    for (const scene::CubeDraw& d : ours) {
        if (d.send == scene::CubeSend::LayerClear) { ++k; continue; }
        if (k >= theirs.size()) { std::fprintf(stderr, "%s: more draws than the model's %zu\n", at.c_str(), theirs.size()); return 1; }
        const json& t = theirs.at(k++);
        if (d.label != t.at("label").get<std::string>()) { std::fprintf(stderr, "%s draw %zu: %s vs %s\n", at.c_str(), k - 1, d.label.c_str(), t.at("label").get<std::string>().c_str()); return 1; }
        const json& vs = t.at("vertices");
        size_t n = 0;
        if (d.rectangle) n = d.rectangle->vertices.size();
        for (const scene::RodFaceDraw& f : d.faces)
            for (const scene::RodVertex& v : f.strip) {
                if (n >= vs.size() || !sameVertex(v, vs.at(n), width, height)) { std::fprintf(stderr, "%s draw %zu (%s) vertex %zu differs\n", at.c_str(), k - 1, d.label.c_str(), n); return 1; }
                ++n;
            }
        if (n != vs.size()) { std::fprintf(stderr, "%s draw %zu (%s): %zu vertices, the model %zu\n", at.c_str(), k - 1, d.label.c_str(), n, vs.size()); return 1; }
    }
    if (k != theirs.size()) { std::fprintf(stderr, "%s: %zu draws, the model %zu\n", at.c_str(), k, theirs.size()); return 1; }
    return 0;
}

template <class A>
int sceneFile(const std::string& path, const scene::RodMesh& mesh, bool compare) {
    const json scene = scenetest::loadScene(path);
    size_t drawn = 0;
    for (const json& frame : scene.at("frames")) {
        const std::string at = path + " frame " + std::to_string(frame.at("index").get<int>());
        const json& stage = frame.at("expect").at("stages").at("cubes");
        if (stage.is_null()) continue;
        StagePieces p = stagePieces(stage.at("before"));
        scene::Cubes<A> cubes(mesh);
        const json& input = frame.at("input");
        const scene::CubeFrameInputs in{p.width, p.height, input.at("scene").at("field").get<int32_t>(), input.at("body").get<int32_t>()};
        const auto out = cubes.frame(p.cubes, p.head, p.clock, in);
        if (!compare) continue;
        const auto differences = stageDifferences(p, stage.at("after"));
        if (!differences.empty()) { std::fprintf(stderr, "%s: %s\n", at.c_str(), differences.front().c_str()); return 1; }
        if (draws(out, frame.at("expect").at("cubes"), p.width, p.height, at)) return 1;
        drawn += out.size();
    }
    if (compare) std::printf("%s: every frame equal, %zu draws\n", path.c_str(), drawn);
    return 0;
}

}

int main(int argc, char** argv) {
    return scenetest::run(argc, argv, [](int count, char** arguments) {
        const scene::RodMesh mesh = scene::loadRodMesh(arguments[1]);
        for (int i = 2; i < count; ++i) {
            if (int failed = sceneFile<scene::EeArithmetic>(arguments[i], mesh, true)) return failed;
            if (int failed = sceneFile<scene::NativeArithmetic>(arguments[i], mesh, false)) return failed;
        }
        return 0;
    });
}
```

`stagePieces` puts the frame's `spin` into `clock.spin` (the cubes read it). `sameVertex` checks the strip sends; the rectangle sends are checked through `headRectangle`, the helper FrameHead's own tests already hold, by counting them in `n`. If the conversion of a strip vertex differs from the one `RodsTest.cpp` uses, use `RodsTest.cpp`'s helper verbatim (it is the documented one).

CMake:

```cmake
target_sources(ClockScene PRIVATE src/scene/Cubes.cpp)
target_sources(ClockSceneNative PRIVATE src/scene/Cubes.cpp)
add_executable(CubesTest tests/scene/CubesTest.cpp)
target_link_libraries(CubesTest PRIVATE StageCompare)
if(MENU_SCENES)
    add_test(NAME CubesTest COMMAND CubesTest ${CMAKE_SOURCE_DIR}/References/model/cube-mesh.json ${MENU_SCENES})
endif()
```

- [ ] **Step 2: Run it to see it fail.**

Run: `cmake -S . -B build && cmake --build build --config Debug --target CubesTest`
Expected: FAIL to compile (`scene/Cubes.hpp` not found); with a stub returning no draws: `... frame 0: cubeRamp.counter ...` or `0 draws, the model 160`.

- [ ] **Step 3: Move the emitters.** Cut `struct Emit<A>` from `Rods.cpp` into `RodEmitters.hpp` as `RodEmitters<A>` with the `edgeSmoothing` argument on `reflected` (default `false`, the rods' value); `Rods.cpp` uses `RodEmitters<A>`. Expose `rectangle` of `FrameHead.cpp` as `headRectangle(record, width, height, part)`.

Run: `cmake --build build --config Debug && ctest --test-dir build -C Debug -R "RodsTest|HeadTest|ClockParityTest" --output-on-failure`
Expected: PASS, unchanged counts (a pure move).

- [ ] **Step 4: Implement** `Cubes.cpp` by the port map.

- [ ] **Step 5: Run it to see it pass.**

Run: `cmake --build build --config Debug --target CubesTest && ctest --test-dir build -C Debug -R CubesTest --output-on-failure`
Expected: PASS: `every frame equal` for every capture and scenario; `whole2-down` and `long` show the list moving, `entries` the pulse.

- [ ] **Step 6: Prove the test bites** (each alone, then revert): `wrap` without the negative branch (`long` fails), the pulse not added on the pulsed place (`entries` fails), the layer reflection without edge smoothing (a vertex or label mismatch on `whole3-config` fails in Task 8's state comparison — record that this one is Task 8's and check it there), `chain` with `5 - level` unclamped (`whole2-to-clock` fails). Record the failure lines.

- [ ] **Step 7: Commit.**

```bash
git add src/scene/RodEmitters.hpp src/scene/Rods.cpp src/scene/FrameHead.hpp src/scene/FrameHead.cpp src/scene/Cubes.hpp src/scene/Cubes.cpp tests/scene/CubesTest.cpp CMakeLists.txt
git commit -m "Feat(Scene): Port the cubes of System Configuration"
```

### Task 6: Menu text

**Files:**
- Modify: `src/scene/Text.hpp`, `src/scene/Text.cpp`, `CMakeLists.txt`
- Create: `tests/scene/StringCompare.hpp`, `tests/scene/StringCompare.cpp`, `tests/scene/MenuTextTest.cpp`

**Interfaces:**
- Consumes: `Text<A>` (Task 11: `Text(font, program, TextInputs)`, `frame(const TextFrameInputs&)`, `cache()`, `font()`), `scene::textInputs` via `scene::clockInputs`, Task 3's `MenusState`, `menusState`, `listFade`, `configItems`.
- Produces (`Text.hpp`):

```cpp
// facts/text.md section 5: what the pages' strings read — the main menu, System Configuration's list and entries,
// the crossfade (R4), the configuration items (the clock value), and the ramps of the alpha rules.
struct PagesInputs {
    MainMenu mainMenu;
    ConfigPage page;
    std::array<ConfigEntry, 9> entries{};
    ListFade listFade;
    ListConstants listConstants;
    std::array<AdjustField, 6> adjustFields{};
    ConfigItems items{};
    Ramp menuRamp;
    int32_t tail = 0, overlayLevel = 0;
    TextRamps ramps;
    int32_t width = 640, height = 224;
};
struct PagesFrame {
    TextFrame text;
    std::optional<int32_t> titleWidth;     // configPage + 0xC as the list's drawing measures it (browser_str_related2)
};
// TextRamps from the menus' live ramps (config, mainMenu, version, dialogClosing, firstRun, lead, body); the rest from `constants`.
TextRamps textRampsOf(const MenusState& menus, const TextRamps& constants);
PagesInputs pagesOf(const MenusState& menus, const ClockState& clock, const ConfigItems& items, const TextRamps& ramps, int32_t width, int32_t height);

// in Text<A>:
    PagesFrame pages(const PagesInputs& in);      // the pages function's text, before the bars, the cache carried
// in TextFrameInputs:
    std::optional<TextRamps> ramps;               // the menus' ramps this frame; the constants of TextInputs without it
```

Sources, per caller (the place, colour, size and alpha rules are `References/scripts/verify_text2.mjs` `PLACES`, `LIST_Y`, `sequencePlace`, `alphaOf`, `menuAlpha`, `configAlpha`, `arrowAlpha`; the strings are what the callers hand the font code, read in `D:/CodingProjects/CrystalOSD/asm`):

| Caller | Strings | Read in |
|---|---|---|
| `draw_clock_menu_items` `0x00232170` | item `n` = `get_lang_string(word at mainMenu.items + 16 n)`; chosen colour `D_002B2540`, plain `D_002B2550` (read from the ELF through `ProgramImage`) | `graph/draw_clock_menu_items_hkdosd_p4_tgt.s` |
| list `0x00231388` | title (and its measurement, which writes `page.titleWidth`) | `browser/browser_str_related.s`, `browser/browser_str_related2.s` |
| `func_002311E8` | the selected entry (`entries[i].id`), and the leaving one while `listFade.second != 0`; alphas `by128(first × configAlpha)`, `by128(second × configAlpha)` | `clock/func_002311E8.s` |
| `clock_config_get_item_str` `0x00228470` via `draw_menu_item` | the entry's value from its value table | the file holding `clock_config_get_item_str` (find with `grep -rl clock_config_get_item_str D:/CodingProjects/CrystalOSD/asm`) |
| `clock_str_related` `0x002270A8` | Clock Adjustment's value, field by field (`sequencePlace`) | the file holding `clock_str_related` |
| arrow `0x00231624` | `\ao018`, alpha `arrowAlpha(page.glow, listConstants.divisor)` | `browser/browser_str_related.s` |

- [ ] **Step 1: Move the string comparison.** Copy `compareStrings` and `sameState` from `tests/scene/ClockParityTest.cpp` into `tests/scene/StringCompare.{hpp,cpp}` (namespace `scenetest`), generalised to take the list of parts: `int compareStrings(const std::vector<scene::StringRun>& ours, const nlohmann::json& expect, std::initializer_list<const char*> parts, const std::string& at);` and `int compareCache(const scene::FontCache& ours, const nlohmann::json& theirs, const std::string& at);`. `ClockParityTest.cpp` is not touched (Task 8 switches it).

- [ ] **Step 2: Write the failing test** `tests/scene/MenuTextTest.cpp`:

```cpp
#include <cstdio>
#include <memory>
#include <optional>
#include <string>

#include "SceneFixture.hpp"
#include "StageCompare.hpp"
#include "StringCompare.hpp"
#include "scene/SceneInputs.hpp"
#include "scene/Text.hpp"

namespace {

using nlohmann::json;

int sceneFile(const std::string& path, const std::shared_ptr<const scene::Font>& font, const std::shared_ptr<const scene::ProgramImage>& program, const scene::RodMesh& mesh) {
    const json scene = scenetest::loadScene(path);
    const json& frames = scene.at("frames");
    const scene::ClockInputs first = scene::clockInputs(frames.at(0).at("input"), mesh);
    scene::Text<scene::EeArithmetic> text(font, program, first.text);
    size_t strings = 0;
    for (size_t k = 0; k < frames.size(); ++k) {
        const json& frame = frames.at(k);
        const std::string at = path + " frame " + std::to_string(k);
        const json& input = frame.at("input");
        const scene::ClockInputs here = scene::clockInputs(input, mesh);
        scene::MenusState menus = scene::menusState(frame.at("expect").at("stages").at("menus").at("after"));
        if (frame.at("expect").contains("listFade")) menus.listFade = scene::listFade(frame.at("expect").at("listFade"));
        const scene::ConfigItems items = scene::configItems(input);
        const scene::TextRamps ramps = scene::textRampsOf(menus, here.text.ramps);
        scene::ClockState clock = here.state;
        clock.menuRamp = stagePieces(frame.at("expect").at("stages").at("menuStep").at("after")).clock.menuRamp;
        const scene::PagesFrame pages = text.pages(scene::pagesOf(menus, clock, items, ramps, here.width, here.height));
        scene::TextFrameInputs in;
        in.items = {items[6], items[7], items[8], items[9], items[10], items[11]};
        in.item0 = items[0];
        in.overlayLevel = clock.overlayLevel;
        in.tail = clock.tail;
        in.menu = clock.menuRamp;
        in.width = here.width;
        in.height = here.height;
        in.ramps = ramps;
        const scene::TextFrame rest = text.frame(in);
        std::vector<scene::StringRun> all = pages.text.strings;
        all.insert(all.end(), rest.strings.begin(), rest.strings.end());
        if (scenetest::compareStrings(all, frame.at("expect").at("text"), {"pages", "text", "hint"}, at)) return 1;
        strings += all.size();
        if (k + 1 < frames.size()) {
            const json& next = frames.at(k + 1).at("input");
            if (scenetest::compareCache(text.cache(), next.at("font"), at + " cache")) return 1;
            if (pages.titleWidth && *pages.titleWidth != next.at("configPage").at("titleWidth").get<int32_t>()) {
                std::fprintf(stderr, "%s: title width %d, the console %d\n", at.c_str(), *pages.titleWidth, next.at("configPage").at("titleWidth").get<int32_t>());
                return 1;
            }
        }
    }
    std::printf("%s: %zu frames, %zu strings equal\n", path.c_str(), frames.size(), strings);
    return 0;
}

}

int main(int argc, char** argv) {
    return scenetest::run(argc, argv, [](int count, char** arguments) {
        const auto font = std::make_shared<const scene::Font>(scene::Font::load(arguments[1]));
        const auto program = std::make_shared<const scene::ProgramImage>(scene::ProgramImage::load(arguments[2]));
        const scene::RodMesh mesh = scene::loadRodMesh(arguments[3]);
        for (int i = 4; i < count; ++i)
            if (int failed = sceneFile(arguments[i], font, program, mesh)) return failed;
        return 0;
    });
}
```

The `menuRamp` the hint reads is the clock's after `menuStep` (the text runs after it in the frame), hence `stagePieces(...menuStep.after)`. The `font` key of frame k + 1 is the cache at that frame's first character, which follows frame k's last string.

CMake:

```cmake
add_library(StringCompare STATIC tests/scene/StringCompare.cpp)
target_link_libraries(StringCompare PUBLIC SceneTestSupport)
add_executable(MenuTextTest tests/scene/MenuTextTest.cpp)
target_link_libraries(MenuTextTest PRIVATE StringCompare StageCompare)
file(GLOB MENU_TEXT_SCENES CONFIGURE_DEPENDS ${MENU_FIXTURES}/hddosd-110U-whole3-*/scene.json)
list(FILTER MENU_TEXT_SCENES EXCLUDE REGEX "whole3-clock")
if(MENU_TEXT_SCENES AND EXISTS ${CLOCK_DUMPS}/FNTOSD)
    add_test(NAME MenuTextTest COMMAND MenuTextTest ${CLOCK_DUMPS}/FNTOSD ${CLOCK_DUMPS}/hddosd.elf ${CMAKE_SOURCE_DIR}/facts/data/rod-mesh.json ${MENU_TEXT_SCENES})
endif()
```

- [ ] **Step 3: Run it to see it fail.**

Run: `cmake -S . -B build && cmake --build build --config Debug --target MenuTextTest`
Expected: FAIL to compile (`pages` is not a member of `Text`); with a stub `pages` returning nothing: `... whole3-enter... frame 0: N strings, the capture M` (the message `compareStrings` prints).

- [ ] **Step 4: Implement** `pages`, `textRampsOf`, `pagesOf` and the `ramps` override in `Text<A>::frame` by the sources table; every number comes from the ELF through `ProgramImage` or from `verify_text2.mjs`'s rule with its address named in a one-line comment.

- [ ] **Step 5: Run it to see it pass.**

Run: `cmake --build build --config Debug --target MenuTextTest && ctest --test-dir build -C Debug -R "MenuTextTest|ClockParityTest" --output-on-failure`
Expected: PASS: every whole3 menu capture `strings equal` (`whole3-down` included once T2 has exported it), `ClockParityTest` unchanged (the clock without menus passes no `ramps` and draws no pages).

- [ ] **Step 6: Prove the test bites** (each alone, then revert): items 1 above the middle (`y - 13`) → `whole3-menu`; the leaving entry drawn with `first` instead of `second` → `whole3-down`; `arrowAlpha` with rate 50 → `whole3-config`; `textRampsOf` using the constant config ramp → `whole3-enter`. Record the failure lines.

- [ ] **Step 7: Commit.**

```bash
git add src/scene/Text.hpp src/scene/Text.cpp tests/scene/StringCompare.hpp tests/scene/StringCompare.cpp tests/scene/MenuTextTest.cpp CMakeLists.txt
git commit -m "Feat(Scene): Port the menus' and the list's text"
```

### Task 7: Input mapping

**Files:**
- Create: `src/app/Input.hpp`, `src/app/Input.cpp`, `tests/app/InputTest.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: SDL3 event types (`SDL_Event`, `SDL_Scancode`, `SDL_GamepadButton`, `SDL_JoystickID`); `scene::PadWords` and `scene::pad` bits (Task 3 — in this lane, which starts before Task 3, declare them in `src/app/Input.hpp` under `namespace app::padbits` with the same values and switch to `scene::` when the lanes merge; the merge step does that switch).
- Produces:

```cpp
namespace app {
enum class PadButton { Up, Down, Left, Right, Cross, Circle, Square, Triangle };
uint32_t bitOf(PadButton button);                        // the OSD's pad word bit

// The pad words of one logic frame from button events (spec: edge and hold as the pad thread sees them).
// A press and release between two frames still gives one `pressed` (and one `released`); a held button gives
// `pressed` once. `repeating` is `pressed` (R6).
class PadReader {
public:
    void down(uint32_t source, uint32_t bit);            // source 0: keyboard; a gamepad: its SDL_JoystickID
    void up(uint32_t source, uint32_t bit);
    void dropSource(uint32_t source);                    // a gamepad removed: its buttons released
    void clear();                                        // focus lost: every button released
    scene::PadWords frame();                             // once per logic frame
private:
    uint32_t held() const;
    std::map<uint32_t, uint32_t> m_held;
    uint32_t m_previous = 0, m_pressed = 0, m_released = 0;
};

struct Bindings {
    std::map<SDL_Scancode, PadButton> keys;
    std::map<SDL_GamepadButton, PadButton> buttons;
    static Bindings defaults();                          // arrows = d-pad, Return/Z = cross, Escape/X = circle, Backspace/S = square, T = triangle;
                                                         // gamepad by position: south cross, east circle, west square, north triangle, d-pad
    void bindKey(SDL_Scancode key, PadButton button);    // the key now gives `button` only
    void bindButton(SDL_GamepadButton pad, PadButton button);
};

// Key down/up (OS repeats ignored), gamepad button down/up, gamepad removed, window focus lost.
void feed(PadReader& reader, const Bindings& bindings, const SDL_Event& event);
}
```

- [ ] **Step 1: Write the failing test** `tests/app/InputTest.cpp`:

```cpp
#include <cstdio>
#include <fstream>
#include <string>

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include "app/Input.hpp"

namespace {

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 1;                                                            \
        }                                                                        \
    } while (0)

SDL_Event key(SDL_EventType type, SDL_Scancode code, bool repeat = false) {
    SDL_Event e{};
    e.type = type;
    e.key.scancode = code;
    e.key.repeat = repeat;
    return e;
}
SDL_Event button(SDL_EventType type, SDL_JoystickID which, SDL_GamepadButton b) {
    SDL_Event e{};
    e.type = type;
    e.gbutton.which = which;
    e.gbutton.button = static_cast<Uint8>(b);
    return e;
}

int bits() {
    CHECK(app::bitOf(app::PadButton::Up) == 0x1000 && app::bitOf(app::PadButton::Right) == 0x2000);
    CHECK(app::bitOf(app::PadButton::Down) == 0x4000 && app::bitOf(app::PadButton::Left) == 0x8000);
    CHECK(app::bitOf(app::PadButton::Triangle) == 0x10 && app::bitOf(app::PadButton::Cross) == 0x20);
    CHECK(app::bitOf(app::PadButton::Circle) == 0x40 && app::bitOf(app::PadButton::Square) == 0x80);
    return 0;
}

int defaults() {
    const app::Bindings b = app::Bindings::defaults();
    CHECK(b.keys.at(SDL_SCANCODE_RETURN) == app::PadButton::Cross && b.keys.at(SDL_SCANCODE_Z) == app::PadButton::Cross);
    CHECK(b.keys.at(SDL_SCANCODE_ESCAPE) == app::PadButton::Circle && b.keys.at(SDL_SCANCODE_X) == app::PadButton::Circle);
    CHECK(b.keys.at(SDL_SCANCODE_BACKSPACE) == app::PadButton::Square && b.keys.at(SDL_SCANCODE_S) == app::PadButton::Square);
    CHECK(b.keys.at(SDL_SCANCODE_T) == app::PadButton::Triangle && b.keys.at(SDL_SCANCODE_UP) == app::PadButton::Up);
    CHECK(b.buttons.at(SDL_GAMEPAD_BUTTON_SOUTH) == app::PadButton::Cross && b.buttons.at(SDL_GAMEPAD_BUTTON_EAST) == app::PadButton::Circle);
    CHECK(b.buttons.at(SDL_GAMEPAD_BUTTON_WEST) == app::PadButton::Square && b.buttons.at(SDL_GAMEPAD_BUTTON_NORTH) == app::PadButton::Triangle);
    app::Bindings r = b;
    r.bindKey(SDL_SCANCODE_Z, app::PadButton::Triangle);
    CHECK(r.keys.at(SDL_SCANCODE_Z) == app::PadButton::Triangle && r.keys.at(SDL_SCANCODE_RETURN) == app::PadButton::Cross);
    return 0;
}

// Review Focus 1.
int edges() {
    const app::Bindings b = app::Bindings::defaults();
    app::PadReader reader;
    app::feed(reader, b, key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_RETURN));
    app::feed(reader, b, key(SDL_EVENT_KEY_UP, SDL_SCANCODE_RETURN));
    scene::PadWords w = reader.frame();
    CHECK(w.pressed == 0x20 && w.released == 0x20 && w.held == 0 && w.repeating == 0x20);   // tap inside one frame
    w = reader.frame();
    CHECK(w.pressed == 0 && w.released == 0);
    app::feed(reader, b, key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_DOWN));
    CHECK(reader.frame().pressed == 0x4000);
    app::feed(reader, b, key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_DOWN, true));                 // held key gives one edge
    for (int i = 0; i < 5; ++i) { w = reader.frame(); CHECK(w.pressed == 0 && w.held == 0x4000); }
    app::feed(reader, b, key(SDL_EVENT_KEY_UP, SDL_SCANCODE_DOWN));
    w = reader.frame();
    CHECK(w.released == 0x4000 && w.held == 0);
    // Keyboard and gamepad hold the same button: it stays held until both let go.
    app::feed(reader, b, key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_Z));
    app::feed(reader, b, button(SDL_EVENT_GAMEPAD_BUTTON_DOWN, 7, SDL_GAMEPAD_BUTTON_SOUTH));
    CHECK(reader.frame().pressed == 0x20);
    app::feed(reader, b, key(SDL_EVENT_KEY_UP, SDL_SCANCODE_Z));
    CHECK(reader.frame().held == 0x20);
    return 0;
}

// Review Focus 4.
int releases() {
    const app::Bindings b = app::Bindings::defaults();
    app::PadReader reader;
    app::feed(reader, b, button(SDL_EVENT_GAMEPAD_BUTTON_DOWN, 3, SDL_GAMEPAD_BUTTON_EAST));
    CHECK(reader.frame().held == 0x40);
    SDL_Event removed{};
    removed.type = SDL_EVENT_GAMEPAD_REMOVED;
    removed.gdevice.which = 3;
    app::feed(reader, b, removed);                                                           // gamepad removed releases
    scene::PadWords w = reader.frame();
    CHECK(w.held == 0 && w.released == 0x40);
    app::feed(reader, b, button(SDL_EVENT_GAMEPAD_BUTTON_DOWN, 4, SDL_GAMEPAD_BUTTON_EAST));
    CHECK(reader.frame().pressed == 0x40);
    app::feed(reader, b, key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_LEFT));
    reader.frame();
    SDL_Event focus{};
    focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    app::feed(reader, b, focus);                                                             // focus lost releases
    w = reader.frame();
    CHECK(w.held == 0 && (w.released & 0x8000) && (w.released & 0x40));
    app::feed(reader, b, key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_LEFT));
    CHECK(reader.frame().pressed == 0x8000);
    return 0;
}

// The cross bit is the one the console's pad word holds in the frame whose cross opens System Configuration.
int capture(const char* path) {
    std::ifstream in(path);
    const nlohmann::json scene = nlohmann::json::parse(in);
    for (const auto& frame : scene.at("frames")) {
        const auto& s = frame.at("expect").at("stages").at("menus");
        if (s.at("before").at("configPage").at("ramp").at("state") == 0 && s.at("after").at("configPage").at("ramp").at("state") == 1) {
            CHECK((frame.at("input").at("pad").at("pressed").get<uint32_t>() & app::bitOf(app::PadButton::Cross)) != 0);
            std::printf("cross bit seen in frame %d of the capture\n", frame.at("index").get<int>());
            return 0;
        }
    }
    std::fprintf(stderr, "no frame opens System Configuration in %s\n", path);
    return 1;
}

}

int main(int argc, char** argv) {
    if (int failed = bits()) return failed;
    if (int failed = defaults()) return failed;
    if (int failed = edges()) return failed;
    if (int failed = releases()) return failed;
    if (argc > 1)
        if (int failed = capture(argv[1])) return failed;
    std::printf("input: all equal\n");
    return 0;
}
```

CMake:

```cmake
add_library(ClockInput STATIC src/app/Input.cpp)
target_include_directories(ClockInput PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ClockInput PUBLIC SDL3::SDL3)
add_executable(InputTest tests/app/InputTest.cpp)
target_link_libraries(InputTest PRIVATE ClockInput nlohmann_json::nlohmann_json)
get_filename_component(ENTER_SCENE ${CLOCK_FIXTURE}/../../hddosd-110U-whole3-enter/scene.json ABSOLUTE)
if(EXISTS ${ENTER_SCENE})
    add_test(NAME InputTest COMMAND InputTest ${ENTER_SCENE})
else()
    add_test(NAME InputTest COMMAND InputTest)
endif()
```

- [ ] **Step 2: Run it to see it fail.**

Run: `cmake -S . -B build && cmake --build build --config Debug --target InputTest`
Expected: FAIL to compile (`app/Input.hpp` not found); with stubs returning zero words: `InputTest.cpp:...: w.pressed == 0x20 && ...`.

- [ ] **Step 3: Implement** `Input.cpp`:

```cpp
#include "app/Input.hpp"

namespace app {

uint32_t bitOf(PadButton button) {
    switch (button) {
    case PadButton::Up: return scene::pad::Up;
    case PadButton::Down: return scene::pad::Down;
    case PadButton::Left: return scene::pad::Left;
    case PadButton::Right: return scene::pad::Right;
    case PadButton::Cross: return scene::pad::Cross;
    case PadButton::Circle: return scene::pad::Circle;
    case PadButton::Square: return scene::pad::Square;
    case PadButton::Triangle: return scene::pad::Triangle;
    }
    return 0;
}

uint32_t PadReader::held() const {
    uint32_t out = 0;
    for (const auto& [source, bits] : m_held) out |= bits;
    return out;
}
void PadReader::down(uint32_t source, uint32_t bit) {
    if (!(held() & bit)) m_pressed |= bit;
    m_held[source] |= bit;
}
void PadReader::up(uint32_t source, uint32_t bit) {
    m_held[source] &= ~bit;
    if (!(held() & bit)) m_released |= bit;
}
void PadReader::dropSource(uint32_t source) {
    const auto found = m_held.find(source);
    if (found == m_held.end()) return;
    const uint32_t bits = found->second;
    m_held.erase(found);
    m_released |= bits & ~held();
}
void PadReader::clear() {
    m_released |= held();
    m_held.clear();
}
scene::PadWords PadReader::frame() {
    const uint32_t now = held();
    scene::PadWords w{now, (now & ~m_previous) | m_pressed, (m_previous & ~now) | m_released, 0};
    w.repeating = w.pressed;
    m_previous = now;
    m_pressed = m_released = 0;
    return w;
}

Bindings Bindings::defaults() {
    Bindings b;
    b.keys = {{SDL_SCANCODE_UP, PadButton::Up},         {SDL_SCANCODE_DOWN, PadButton::Down},     {SDL_SCANCODE_LEFT, PadButton::Left},
              {SDL_SCANCODE_RIGHT, PadButton::Right},   {SDL_SCANCODE_RETURN, PadButton::Cross},  {SDL_SCANCODE_Z, PadButton::Cross},
              {SDL_SCANCODE_ESCAPE, PadButton::Circle}, {SDL_SCANCODE_X, PadButton::Circle},      {SDL_SCANCODE_BACKSPACE, PadButton::Square},
              {SDL_SCANCODE_S, PadButton::Square},      {SDL_SCANCODE_T, PadButton::Triangle}};
    b.buttons = {{SDL_GAMEPAD_BUTTON_DPAD_UP, PadButton::Up},     {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PadButton::Down}, {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PadButton::Left},
                 {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PadButton::Right}, {SDL_GAMEPAD_BUTTON_SOUTH, PadButton::Cross},  {SDL_GAMEPAD_BUTTON_EAST, PadButton::Circle},
                 {SDL_GAMEPAD_BUTTON_WEST, PadButton::Square},     {SDL_GAMEPAD_BUTTON_NORTH, PadButton::Triangle}};
    return b;
}
void Bindings::bindKey(SDL_Scancode key, PadButton button) { keys[key] = button; }
void Bindings::bindButton(SDL_GamepadButton pad, PadButton button) { buttons[pad] = button; }

void feed(PadReader& reader, const Bindings& bindings, const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        if (event.key.repeat) return;
        const auto found = bindings.keys.find(event.key.scancode);
        if (found == bindings.keys.end()) return;
        if (event.type == SDL_EVENT_KEY_DOWN) reader.down(0, bitOf(found->second));
        else reader.up(0, bitOf(found->second));
        return;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        const auto found = bindings.buttons.find(static_cast<SDL_GamepadButton>(event.gbutton.button));
        if (found == bindings.buttons.end()) return;
        const uint32_t source = static_cast<uint32_t>(event.gbutton.which);
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) reader.down(source, bitOf(found->second));
        else reader.up(source, bitOf(found->second));
        return;
    }
    case SDL_EVENT_GAMEPAD_REMOVED: reader.dropSource(static_cast<uint32_t>(event.gdevice.which)); return;
    case SDL_EVENT_WINDOW_FOCUS_LOST: reader.clear(); return;
    default: return;
    }
}

}
```

A gamepad's `SDL_JoystickID` is never 0 (SDL3), so source 0 is free for the keyboard.

- [ ] **Step 4: Run it to see it pass.**

Run: `cmake --build build --config Debug --target InputTest && ctest --test-dir build -C Debug -R InputTest --output-on-failure`
Expected: PASS, `input: all equal` (and `cross bit seen in frame N` once the fixture exists).

- [ ] **Step 5: Commit.**

```bash
git add src/app/Input.hpp src/app/Input.cpp tests/app/InputTest.cpp CMakeLists.txt
git commit -m "Feat(App): Map keyboard and gamepad to the OSD's pad words"
```

### Task 8: Frame assembly, carried geometry, pixel gates

**Files:**
- Modify: `src/scene/Clock.hpp`, `src/scene/Clock.cpp`, `src/scene/SceneInputs.hpp`, `src/scene/SceneInputs.cpp`, `tests/scene/ClockParityTest.cpp`, `CMakeLists.txt`
- Create: `tests/scene/SceneGate.hpp`, `tests/scene/SceneGate.cpp`, `tests/scene/MenusParityTest.cpp`, `tools/ParityTool/budgets/hddosd-110U-whole3-menu.json`, `tools/ParityTool/budgets/hddosd-110U-whole3-enter.json`, `tools/ParityTool/budgets/hddosd-110U-whole2-back.json`

**Interfaces:**
- Consumes: `Menus`, `MenusOptions` (T4); `Cubes<A>`, `CubeDraw`, `CubeSend` (T5); `Text<A>::pages`, `pagesOf`, `textRampsOf`, `StringCompare` (T6); `parity::fromScene`, `parity::clockLayout`; ClockParityTest's `compareFrame`, `writeSceneFixture`, `dumpPass`, `stateDifference`, `passDifference`, `sameVertex`, `primitivesMatch`, `compareCache`, `compareAfter`.
- Produces:

```cpp
// src/scene/Clock.hpp (added)
struct MenusInputs {
    MenusState menus;
    CubeState cubes;
    RodMesh cubeMesh;
    ConfigItems items{};
    MenusOptions options;
};
// in ClockInputs:
    std::optional<MenusInputs> menus;            // absent: the clock screen alone, as before
// in FrameInputs:
    MenuExternals menu;                          // pad, disc, configDirty, rtcMirror, mechaconParam
    std::optional<ConfigItems> configItems;      // items 1..0x13 external; item 0 the model's when the gate is modelled
    std::optional<int32_t> timeFilled;           // the time keeper's word (verify_frame.mjs EXTERNAL)
    bool threadStep = true;                      // run between() first; false for a frame recorded after it (frame 0)
// in Clock<A>:
    const MenusState* menus() const;
    const CubeState* cubes() const;
    const std::vector<std::string>& notes() const;   // the model's notes of the last frame
// src/scene/SceneInputs.hpp: clockInputs reads `menus` when hasMenus(input) (cube mesh passed in); frameInputs reads the externals.
ClockInputs clockInputs(const nlohmann::json& input, const RodMesh& mesh, const RodMesh* cubeMesh);
```

Order of a frame with menus (`clock_frame.mjs frame()` and `verify_frame.mjs`'s carry loop): externals in → `between` (when `threadStep`) → camera → display clear → head → rods, orbs, extra passes → overlay → trips after → **cubes** → **menuStep** → **menus** → **page text** (cut before and after) → bars → `textAt` → date, time, hint (with `textRampsOf`) → column → `ClockLogic::step` → **endOfFrame**. Each `CubeSend` takes its state from facts/config-cubes.md "Drawing one cube" and "The pass" and the model's `stateWriters` calls in `clock_cubes.mjs` (`frameTexture` = the display as 24-bit colour, `buffer(n)`, `bind(n, …)`, `blend(mode, ztst)`), named in `Clock.cpp` with a one-line comment per send.

- [ ] **Step 1: Share the gate code.** Move `vertexText`, `regionMode`, `sameAddress`, `sameVertex`, `stateDifference`, `passDifference`, `passLabel`, `TextCount`, `dumpPass`, `compareFrame`, `primitivesMatch`, `writeSceneFixture` from `ClockParityTest.cpp` into `tests/scene/SceneGate.{hpp,cpp}` (namespace `scenetest`), and switch `ClockParityTest.cpp` to `SceneGate` and `StringCompare` (`compareStrings(..., {"text", "hint"}, at)`). `compareFrame` gains `bool absorbText` (default `false`): when true, a dump pass sampling `t2f04-*` with PSM 0x14 (font) or a sprite on texture 8 or 9 (button picture) that the scene does not produce is counted, not failed (R10). `writeSceneFixture` gains the same flag: absorbed passes keep the oracle's draw, `skip` = `"text from the oracle"`.

Run: `cmake --build build --config Debug && ctest --test-dir build -C Debug -R "ClockParityTest|ParityClockScene" --output-on-failure`
Expected: PASS, same output as before the move (4628 passes, 8 observed / 8 budgeted, 0 breaches).

- [ ] **Step 2: Create the dump fixtures.** For `whole3-menu`, `whole3-enter`, `whole2-back`, `whole3-config`, `whole3-adjust-hour`, `whole3-down`, `whole3-square`, `whole3-back`: the passes command; for `whole3-menu`, `whole3-enter`, `whole2-back`: the oracle frame 0 command. Each passes run prints the frame count; `make_fixture.mjs` writes `f0/frame.json`, `f0/oracle`, `f0/start`, `f0/textures`.

- [ ] **Step 3: Budgets of the dump-only gates.** For `<c>` in `whole3-menu`, `whole3-enter`, `whole2-back`:

Run: `bin/ParityTool.exe D:/CodingProjects/CrystalClockVK/References/fixtures/hddosd-110U-<c>/f0 build/parity-report/<c> bin/shaders`
Expected: exit 0, the group table, `build/parity-report/<c>/observed.json`.

For every entry of `observed.json`, record in the report: scope (draw), target, pixel, delta; whether the draw is textured and bilinear; the draw's texture-coordinate rectangle (min/max u, v of its vertices in `f0/frame.json`); the pixel's interpolated texture coordinate; its distance to the rectangle's edge. An entry is budgeted only when the draw is bilinear and the sample lies within one texel of the rectangle's edge (PCSX2's texture cache converts only that rectangle). Any other entry is a renderer bug: stop and report it. Write the budgeted entries to `tools/ParityTool/budgets/hddosd-110U-<c>.json` as `{"differences": [{"delta":…, "scope":…, "target":…, "x":…, "y":…}, …]}` and add the gates:

```cmake
add_parity_test(ParityMenu whole3-menu)
add_parity_test(ParityEnter whole3-enter)
add_parity_test(ParityBack whole2-back)
```

Run: `cmake -S . -B build && ctest --test-dir build -C Debug -R "ParityMenu|ParityEnter|ParityBack" --output-on-failure`
Expected: PASS, each `budget: N differing pixels observed, N budgeted, 0 breaches`; configure prints them among `parity gates active`.
Both ways, once per budget, reverted after: delete one entry → FAIL `unexpected difference: …`; add `{"delta":1,"scope":"chained","target":"fb0000","x":0,"y":0}` → FAIL `stale budget entry: chained fb0000 (0,0)`.

- [ ] **Step 4: Write the failing test** `tests/scene/MenusParityTest.cpp`:

```cpp
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "SceneFixture.hpp"
#include "SceneGate.hpp"
#include "StageCompare.hpp"
#include "StringCompare.hpp"
#include "parity/FromScene.hpp"
#include "scene/Clock.hpp"
#include "scene/SceneInputs.hpp"

namespace {

using nlohmann::json;
using EeClock = scene::Clock<scene::EeArithmetic>;

struct Arguments {
    std::string scene, rodMesh, cubeMesh, passes, font, program, f0, out;
    bool noText = false;
};

Arguments parse(int count, char** a) {
    Arguments r{a[1], a[2], a[3], a[4], a[5], a[6]};
    for (int i = 7; i < count; ++i) {
        const std::string arg = a[i];
        if (arg == "--f0" && i + 1 < count) r.f0 = a[++i];
        else if (arg == "--out" && i + 1 < count) r.out = a[++i];
        else if (arg == "--no-text") r.noText = true;
    }
    return r;
}

int run(const Arguments& args) {
    const json scene = scenetest::loadScene(args.scene);
    const json dump = scenetest::loadScene(args.passes);
    const scene::RodMesh rods = scene::loadRodMesh(args.rodMesh), cubes = scene::loadRodMesh(args.cubeMesh);
    const json& frames = scene.at("frames");
    scene::ClockInputs inputs = scene::clockInputs(frames.at(0).at("input"), rods, &cubes);
    if (!inputs.menus) { std::fprintf(stderr, "%s holds no menus\n", args.scene.c_str()); return 1; }
    std::shared_ptr<const scene::Font> font;
    if (!args.noText) {
        font = std::make_shared<const scene::Font>(scene::Font::load(args.font));
        inputs.font = font;
        inputs.program = std::make_shared<const scene::ProgramImage>(scene::ProgramImage::load(args.program));
    }
    EeClock clock(inputs);
    const size_t compared = std::min(frames.size(), dump.at("frames").size());
    size_t passes = 0, strings = 0;
    scenetest::TextCount absorbed;
    std::optional<scene::Frame> zero;
    for (size_t k = 0; k < frames.size(); ++k) {
        const json& frame = frames.at(k);
        const std::string at = "frame " + std::to_string(k);
        scene::FrameInputs in = scene::frameInputs(frame.at("input"));
        in.threadStep = k > 0;
        const scene::Frame out = clock.frame(in);
        if (k == 0) zero = out;
        if (scenetest::compareAfter(clock, frame.at("expect").at("after"), at)) return 1;
        const auto menus = stageDifferences(StagePieces::of(clock), frame.at("expect").at("stages").at("endOfFrame").at("after"));
        if (!menus.empty()) { std::fprintf(stderr, "%s: %s\n", at.c_str(), menus.front().c_str()); return 1; }
        if (frame.at("expect").contains("listFade") && !(clock.menus()->listFade == scene::listFade(frame.at("expect").at("listFade")))) {
            std::fprintf(stderr, "%s: list fade differs from the console's\n", at.c_str());
            return 1;
        }
        if (!args.noText) {
            if (scenetest::compareStrings(clock.strings(), frame.at("expect").at("text"), {"pages", "text", "hint"}, at)) return 1;
            strings += clock.strings().size();
        }
        if (k < compared) {
            const parity::GsFrame gs = parity::fromScene(out, parity::clockLayout(out.width, out.height, out.displayIndex));
            if (scenetest::compareFrame(gs, dump.at("frames").at(k), absorbed, at, args.noText)) return 1;
            passes += gs.passes.size();
        }
    }
    if (!args.f0.empty())
        if (scenetest::writeSceneFixture(parity::fromScene(*zero, parity::clockLayout(zero->width, zero->height, zero->displayIndex)), *zero, font.get(), args.f0, args.out, args.noText)) return 1;
    std::printf("%s: %zu frames carried, %zu compared with the dump, %zu passes equal, %zu strings equal, %zu text passes absorbed\n", args.scene.c_str(), frames.size(), compared,
                passes, strings, absorbed.font + absorbed.hint);
    return 0;
}

}

int main(int argc, char** argv) {
    return scenetest::run(argc, argv, [](int count, char** arguments) {
        if (count < 7) {
            std::fprintf(stderr, "usage: MenusParityTest <scene.json> <rod-mesh.json> <cube-mesh.json> <passes.json> <FNTOSD> <hddosd.elf> [--f0 dir --out dir] [--no-text]\n");
            return 1;
        }
        return run(parse(count, arguments));
    });
}
```

`StagePieces::of(const Clock<A>&)` (added to `StageCompare.hpp` in this task) copies the clock's state, head, sprite ramp, menus, cubes and items into a `StagePieces`; `compareAfter` (moved to `SceneGate`) keeps comparing the clock's own pieces; `writeSceneFixture` takes the font pointer (null without text). Scene frame k is dump frame k: before comparing, `run` checks that frame 0's orb trail vertices are the dump's (`compareFrame` fails on the first pass otherwise, which names the misalignment).

CMake (after the `add_parity_test` lines, before the `parity gates active` message):

```cmake
add_library(SceneGate STATIC tests/scene/SceneGate.cpp)
target_link_libraries(SceneGate PUBLIC SceneTestSupport StringCompare StageCompare ClockParity)
target_link_libraries(ClockParityTest PRIVATE SceneGate)
add_executable(MenusParityTest tests/scene/MenusParityTest.cpp)
target_link_libraries(MenusParityTest PRIVATE SceneGate)
function(add_menus_gate NAME CAPTURE)
    set(DIR ${FIXTURE_ROOT}/hddosd-110U-${CAPTURE})
    if(NOT (EXISTS ${DIR}/scene.json AND EXISTS ${DIR}/passes.json AND EXISTS ${CLOCK_DUMPS}/FNTOSD))
        return()
    endif()
    set(ARGS ${DIR}/scene.json ${CMAKE_SOURCE_DIR}/facts/data/rod-mesh.json ${CMAKE_SOURCE_DIR}/References/model/cube-mesh.json ${DIR}/passes.json ${CLOCK_DUMPS}/FNTOSD ${CLOCK_DUMPS}/hddosd.elf ${ARGN})
    set(ACTIVE ${PARITY_ACTIVE} ${NAME})
    if(EXISTS ${DIR}/f0/frame.json)
        list(APPEND ARGS --f0 ${DIR}/f0 --out ${CMAKE_BINARY_DIR}/scene-fixture/${CAPTURE})
        add_test(NAME ${NAME}Pixels COMMAND ParityTool ${CMAKE_BINARY_DIR}/scene-fixture/${CAPTURE} ${CMAKE_BINARY_DIR}/parity-report/${CAPTURE}-scene ${CMAKE_SOURCE_DIR}/bin/shaders
            ${CMAKE_SOURCE_DIR}/tools/ParityTool/budgets/hddosd-110U-${CAPTURE}.json)
        set_tests_properties(${NAME}Pixels PROPERTIES FIXTURES_REQUIRED ${NAME}Fixture)
        list(APPEND ACTIVE ${NAME}Pixels)
    endif()
    add_test(NAME ${NAME} COMMAND MenusParityTest ${ARGS})
    set_tests_properties(${NAME} PROPERTIES FIXTURES_SETUP ${NAME}Fixture)
    set(PARITY_ACTIVE ${ACTIVE} PARENT_SCOPE)
endfunction()
add_menus_gate(MenusMenu whole3-menu)
add_menus_gate(MenusEnter whole3-enter)
add_menus_gate(MenusBack whole2-back --no-text)
add_menus_gate(MenusConfig whole3-config)
add_menus_gate(MenusAdjust whole3-adjust-hour)
add_menus_gate(MenusDown whole3-down)
add_menus_gate(MenusSquare whole3-square)
add_menus_gate(MenusBackText whole3-back)
```

- [ ] **Step 5: Run it to see it fail.**

Run: `cmake -S . -B build && cmake --build build --config Debug --target MenusParityTest`
Expected: FAIL to compile (`clockInputs` takes two arguments; `menus` is not a member of `ClockInputs`).

- [ ] **Step 6: Implement** `Clock` with menus in the order above (a `std::optional<Menus>`, `std::optional<Cubes<A>>`, `MenusState`, `CubeState`, `ConfigItems` held by the clock; `MenuWorld` built per call from them and the clock's own state; the `CubeDraw` sends emitted through the existing `emitRodSend`-style conversion and `emitHead`; `Part` names for the cube rectangles in `partName`), `clockInputs(input, mesh, cubeMesh)` (the old two-argument form forwards `nullptr`) and `frameInputs` reading `menuExternals`, `configItems`, `timeFilled`.

- [ ] **Step 7: Run it to see it pass.**

Run: `cmake --build build --config Debug && ctest --test-dir build -C Debug -R "Menus|Parity|ClockParityTest" --output-on-failure`
Expected: PASS. Each `Menus*` prints its frames carried, passes and strings equal; `MenusBack` prints its absorbed text passes; `MenusMenuPixels`, `MenusEnterPixels`, `MenusBackPixels`, `MenusConfigPixels` print `budget: N differing pixels observed, N budgeted, 0 breaches` with the same N as the dump-only gate of the same capture and `validation errors: 0`; `ClockParityTest` and `ParityClockScene` unchanged.

- [ ] **Step 8: Prove the gates bite** (each alone, then revert): cubes drawn after `menuStep` instead of before (`MenusEnter`: `scene pass … differs from dump draw-…`); `between` skipped (`MenusBack`: after-state `screenCode` or `mainMenu.ramp`); the layer reflection without edge smoothing (`MenusConfig`: `antialias`); the page text after the bars (`MenusMenu`: a pass out of order); one budget entry tightened by 1 (`MenusMenuPixels`: `larger difference`). Record the failure lines.

- [ ] **Step 9: Whole suite.**

Run: `ctest --test-dir build -C Debug --output-on-failure`
Expected: every test passes; record the count (17 + the new ones) and the time.

- [ ] **Step 10: Commit** (two commits):

```bash
git add tests/scene/SceneGate.hpp tests/scene/SceneGate.cpp tests/scene/ClockParityTest.cpp tools/ParityTool/budgets/hddosd-110U-whole3-menu.json tools/ParityTool/budgets/hddosd-110U-whole3-enter.json tools/ParityTool/budgets/hddosd-110U-whole2-back.json CMakeLists.txt
git commit -m "Refactor(Scene): Share the dump gate and add the menu captures' gates"
git add src/scene/Clock.hpp src/scene/Clock.cpp src/scene/SceneInputs.hpp src/scene/SceneInputs.cpp tests/scene/StageCompare.hpp tests/scene/StageCompare.cpp tests/scene/MenusParityTest.cpp CMakeLists.txt
git commit -m "Feat(Scene): Assemble the menus' frames and gate them through the rule"
```

### Task 9: Live navigation, soak, screenshots, PNG sequence

**Files:**
- Create: `src/app/Screens.hpp`, `src/app/Screens.cpp`, `tests/app/ScreensTest.cpp`, `resources/menus/start-menu.json`, `resources/menus/start-clock.json`
- Modify: `src/app/Main.cpp`, `src/app/DebugPanel.hpp`, `src/app/DebugPanel.cpp`, `tools/scene/make_start.mjs`, `tests/render/NativeRendererTest.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `Clock<NativeArithmetic>` with `MenusInputs` (T8), `MenusOptions{false}` (R5), `app::PadReader`, `app::Bindings`, `app::feed` (T7), `scene::firstInput`, `scene::clockInputs(input, mesh, cubeMesh)`, `render::NativeRenderer`, `app::writePng` (the existing PNG writer in `src/app/Png.hpp`).
- Produces:

```cpp
// src/app/Screens.hpp
namespace app {
enum class Screen { MainMenu, OpeningConfiguration, Configuration, InsideEntry, ClosingConfiguration, HidingMenu, ClockAlone, ShowingMenu };
Screen screenOf(const scene::MenusState& menus, const scene::Ramp& menuRamp);
const char* screenName(Screen screen);
}
// PanelState gains: PadButton remapButton; bool remapListening = false;
// PanelInfo gains: std::string screen; scene::PadWords pad; scene::Ramp mainMenu, config, menu, cubes; int32_t menuSelected = 0, entrySelected = 0, level = 0;
```

- [ ] **Step 1: Start files.** `make_start.mjs`: add to `KEYS` every Task 1 input key (`pad`, `disc`, `screenCode`, `configPage`, `configRamp`, `configEntries`, `mainMenu`, `versionRamp`, `dialogRamp`, `firstRunRamp`, `pagePointers`, `entryActive`, `menuLengths`, `listConstants`, `adjustFields`, `configGate`, `configDirty`, `rtcMirror`, `cubeList`, `cubeColours`, `cubeRecord`, `spin`, `cubeConstants`, `centreFactors`, `cubeView`, `cubeScreen`, `layerClear`, `addRecord`, `halfRecord`, `chainRecord`, `listFade`, `body`, `cubeRamp`) and an option `--frame <n|clock>` (`clock`: the first frame whose `input.menuRamp.state` is 2). Then:

Run: `node tools/scene/make_start.mjs D:/CodingProjects/CrystalClockVK/References/fixtures/hddosd-110U-whole3-menu/scene.json resources/menus/start-menu.json`
Run: `node tools/scene/make_start.mjs D:/CodingProjects/CrystalClockVK/References/fixtures/hddosd-110U-whole3-square/scene.json resources/menus/start-clock.json --frame clock`
Expected: each prints its key count and size (tens of KB).

- [ ] **Step 2: Write the failing test** `tests/app/ScreensTest.cpp`:

```cpp
#include <cstdio>

#include "app/Screens.hpp"

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 1;                                                            \
        }                                                                        \
    } while (0)

int main() {
    scene::MenusState m;
    scene::Ramp menu;
    CHECK(app::screenOf(m, menu) == app::Screen::MainMenu);
    m.page.ramp.state = 1;
    CHECK(app::screenOf(m, menu) == app::Screen::OpeningConfiguration);
    m.page.ramp.state = 2;
    CHECK(app::screenOf(m, menu) == app::Screen::Configuration);
    m.page.level = 1;
    CHECK(app::screenOf(m, menu) == app::Screen::InsideEntry);
    m.page.level = 0;
    menu.state = 1;
    CHECK(app::screenOf(m, menu) == app::Screen::HidingMenu);
    menu.state = 2;
    CHECK(app::screenOf(m, menu) == app::Screen::ClockAlone);
    menu.state = 3;
    CHECK(app::screenOf(m, menu) == app::Screen::ShowingMenu);
    menu.state = 0;
    m.page.ramp.state = 3;
    CHECK(app::screenOf(m, menu) == app::Screen::ClosingConfiguration);
    std::printf("screens: all equal\n");
    return 0;
}
```

CMake:

```cmake
target_sources(ClockInput PRIVATE src/app/Screens.cpp)
target_link_libraries(ClockInput PUBLIC ClockSceneNative)
add_executable(ScreensTest tests/app/ScreensTest.cpp)
target_link_libraries(ScreensTest PRIVATE ClockInput)
add_test(NAME ScreensTest COMMAND ScreensTest)
target_link_libraries(CrystalClock PRIVATE ClockInput)
target_compile_definitions(CrystalClock PRIVATE CLOCK_MENUS_START="${CMAKE_SOURCE_DIR}/resources/menus/start-menu.json"
    CLOCK_CLOCK_START="${CMAKE_SOURCE_DIR}/resources/menus/start-clock.json" CLOCK_CUBE_MESH="${CMAKE_SOURCE_DIR}/References/model/cube-mesh.json"
    CLOCK_RECORDINGS="${CMAKE_SOURCE_DIR}/out/recordings")
```

Run: `cmake -S . -B build && cmake --build build --config Debug --target ScreensTest`
Expected: FAIL to compile (`app/Screens.hpp` not found).

- [ ] **Step 3: Implement** `Screens.cpp`:

```cpp
#include "app/Screens.hpp"

namespace app {

Screen screenOf(const scene::MenusState& menus, const scene::Ramp& menuRamp) {
    switch (menus.page.ramp.state) {
    case 0: return Screen::MainMenu;
    case 1: return Screen::OpeningConfiguration;
    case 3: return Screen::ClosingConfiguration;
    default: break;
    }
    switch (menuRamp.state) {
    case 1: return Screen::HidingMenu;
    case 2: return Screen::ClockAlone;
    case 3: return Screen::ShowingMenu;
    default: return menus.page.level == 1 ? Screen::InsideEntry : Screen::Configuration;
    }
}

const char* screenName(Screen screen) {
    switch (screen) {
    case Screen::MainMenu: return "main menu";
    case Screen::OpeningConfiguration: return "opening System Configuration";
    case Screen::Configuration: return "System Configuration";
    case Screen::InsideEntry: return "inside an entry";
    case Screen::ClosingConfiguration: return "closing System Configuration";
    case Screen::HidingMenu: return "hiding the menu";
    case Screen::ClockAlone: return "clock alone";
    case Screen::ShowingMenu: return "showing the menu";
    }
    return "?";
}

}
```

Run: `cmake --build build --config Debug --target ScreensTest && ctest --test-dir build -C Debug -R ScreensTest --output-on-failure`
Expected: PASS, `screens: all equal`.

- [ ] **Step 4: The live app** (`Main.cpp`):
  - start from `CLOCK_MENUS_START` (main menu) or, with `--clock`, `CLOCK_CLOCK_START`; `--start <scene.json>` still takes any capture; the cube mesh from `CLOCK_CUBE_MESH` (`--cube-mesh`); `clockInputs.menus->options.browserEnters = false`;
  - `app::Bindings bindings = app::Bindings::defaults(); app::PadReader reader;` every SDL event goes to `app::feed(reader, bindings, event)` (ImGui keeps the keyboard only while a panel widget has focus: skip `feed` when `ImGui::GetIO().WantCaptureKeyboard`); `SDL_EVENT_GAMEPAD_ADDED` opens the gamepad with `SDL_OpenGamepad`, `SDL_EVENT_GAMEPAD_REMOVED` closes it after `feed`;
  - in `produce`: `inputs.menu.pad = reader.frame();`, `inputs.menu.rtcMirror` = now in UTC+9 (the console clock is kept in the base zone, `kBaseZone` 540 minutes), `inputs.configItems`: the clock's items with 6..0xB from local time unless `clock.menus()->page.level == 1` (R12); when the level goes from 1 to 0 through Clock Adjustment's confirm (the selected entry's `confirm == 0x00227b90`), set `offset` so that local time + offset is the confirmed items' time; the first produced frame has `threadStep = false`, every later one `true`;
  - panel (`DebugPanel`): screen name, pad words (hex), the main menu, config, menu and cube ramps, the selected item and entry and level; a remap table (one row per `PadButton`: its keys and buttons; "bind" listens for the next key or gamepad button and calls `bindKey`/`bindButton`);
  - `--record <frames>`: from the logic frame where the soak (or the panel button "record") asks, write the Display target of each of the next `<frames>` logic frames at 640x448 as `out/recordings/<name>/frame-0000.png` …, through the existing screenshot readback and `writePng`.

- [ ] **Step 5: Soak script.** Replace the `--soak` script when menus are present (the clock-only script stays for `--start` of a capture without menus) by a timeline that injects presses with `reader.down(0, bit)` and `reader.up(0, bit)` (held 6 logic frames): main menu down, up, up (clamped); triangle (nothing); cross on Browser (nothing, R5); down, cross → System Configuration (record 110 frames as `enter`); down ×3, up ×5 (wrap); cross into the aspect entry, right, circle (cancel); cross into the aspect entry, cross (confirm, the pulse); square → clock alone (screenshot `clock-alone`); square → System Configuration; circle → main menu (record 100 frames as `back`); plus the resolution, MSAA, minimize and resize steps of the existing script interleaved. Screenshots `main-menu`, `configuration`, `entry`, `clock-alone` at native and window size. At the end print `screens visited: <names in order>` and assert: the visited list equals the expected one, the mode was never 3, `scene.leaving` stayed 0, the screen code never 9999.

Run: `bin/CrystalClock.exe --soak 60`
Expected: `soak: 60.0 s, N frames presented, M logic frames, 0 validation errors` and `screens visited: main menu, opening System Configuration, System Configuration, inside an entry, System Configuration, inside an entry, System Configuration, hiding the menu, clock alone, showing the menu, System Configuration, closing System Configuration, main menu`; exit code 0.
Run: `bin/CrystalClock.exe --soak 60 --clock`
Expected: the same, starting from `clock alone`.

- [ ] **Step 6: Native renderer on menu frames.** In `tests/render/NativeRendererTest.cpp`, when the `whole3-config` and `whole3-enter` scene files exist (passed as extra arguments from CMake, `EXISTS` guarded), carry 8 frames of `Clock<NativeArithmetic>` with menus from each frame 0 at 4x: largest written and blended alpha at most 0x80, every cube send drawn, 0 validation errors.

Run: `cmake -S . -B build && ctest --test-dir build -C Debug -R "NativeRendererTest|InputTest|ScreensTest" --output-on-failure`
Expected: PASS.

- [ ] **Step 7: Eye check material for Jean.** Report the paths: `out/screenshots/main-menu-*.png`, `configuration-*.png`, `entry-*.png`, `clock-alone-*.png`, `out/recordings/enter/frame-*.png` (110), `out/recordings/back/frame-*.png` (100); one line on how to view the sequence (`ffmpeg -framerate 59.94 -i out/recordings/enter/frame-%04d.png enter.mp4`, ffmpeg not required by the project).

- [ ] **Step 8: Whole suite and commits.**

Run: `ctest --test-dir build -C Debug --output-on-failure`
Expected: every test passes.

```bash
git add src/app/Screens.hpp src/app/Screens.cpp tests/app/ScreensTest.cpp tools/scene/make_start.mjs resources/menus/start-menu.json resources/menus/start-clock.json CMakeLists.txt
git commit -m "Feat(App): Add the menus' start states and screens"
git add src/app/Main.cpp src/app/DebugPanel.hpp src/app/DebugPanel.cpp tests/render/NativeRendererTest.cpp CMakeLists.txt
git commit -m "Feat(App): Navigate the menus live from keyboard and gamepad"
```

## Deferred: the Version page

The spec puts the Version page in this slice. Nothing measured drives it yet:
- `clock_menus.mjs mainMenu` answers triangle with the note "the version page (triangle on the main menu) is not modelled"; the page's own code (what opens it, its ramp `versionRamp` beyond the tick, its rows, how circle leaves it) is not in the model or in facts.
- facts/text.md verifies the page's text places and alpha (`func_0022A1D8`, `0x0022A5C0..0x0022A74C`) only from the `text2-version` captures, which are text traces without the frame snapshot.
- No whole-frame capture of it exists.

What it needs first (measurement, Jean's call, skills `measure` and `capture`): a capture `hddosd-110U-whole3-version` (`--state menu --frames 120 --pad '[{"frame":3,"press":["triangle"],"frames":6},{"frame":60,"press":["circle"],"frames":6}]'`, verifiers `verify_frame.mjs,verify_text2.mjs`); the page's code read and modelled in `clock_menus.mjs` until `verify_frame.mjs --carry` says FOUND with zero events on it; the facts page updated. Then a follow-up plan ports it with Task 4's and Task 6's shapes (stages from the exporter, the page's strings through `Text<A>::pages`).

## Self-review

- **Spec coverage.** Main menu with Browser shown, selectable, not entered: T4 (`browserEnters`), T6 (items), T8, T9. System Configuration cubes (ring, pulse): T5, T8. List with cursor: T4, T6. Entering and leaving an entry, values shown: T4 (`entries` scenario, adjust captures), T6 (values, clock value). Square hide/show: T4 (`square` scenario, `whole3-square`), T8. Transitions main menu ⇄ System Configuration ⇄ clock: T4, T8 (`whole3-enter`, `whole2-back`, `whole3-back`, `whole3-square`). Version page: deferred (gap). Exporter path (slice 1): T1. Cubes isolated and through the rule (slice 2): T5, T8 (R8). Menu text (slice 3): T6. Frame assembly, carried geometry, pixel gates on `whole3-menu`, `whole3-enter`, `whole2-back` (slice 4): T8. Input mapping, live navigation, screenshots, PNG frames (slice 5): T7, T9. Debug panel shows screen, ramps, pad word, remap: T9. App starts at the main menu or at the clock with a flag: T9.
- **Placeholder scan.** No "TBD", "similar to" or unnamed type; the ports point at the model function or asm file they copy (R1).
- **Type consistency.** `PadWords`, `MenusState`, `CubeState`, `MenuExternals`, `MenuWorld`, `StagePieces`, `stagePieces`, `stageDifferences` (T3) are the names T4, T5, T6, T8 use; `Menus::step/between/menuStep/endOfFrame/setMode` (T4) are the ones T8 calls; `Cubes<A>::frame(CubeState&, HeadState&, const ClockState&, const CubeFrameInputs&)` (T5) is T8's call; `Text<A>::pages(const PagesInputs&) -> PagesFrame`, `pagesOf`, `textRampsOf` (T6) are T8's; `app::PadReader::frame() -> scene::PadWords` (T7) feeds `FrameInputs::menu.pad` (T8, T9).
- **Review Focus.** Each of the five has its test in the owning task (T7 ×2, T4, T1 + T4 + T5, T1 + T4 + T5).
