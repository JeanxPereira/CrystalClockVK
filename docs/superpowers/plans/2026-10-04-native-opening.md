# Native Opening — Implementation Plan

Approved: controller, 2026-10-04 mandate

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The PS2 OSD's boot intro (camera, stage machine, fog, light orbs and trails, five glass cubes, ghost, logo, dive blur, fade, letterbox bars, towers) and its hand-off into the clock run natively in the live window (`CrystalClock --boot`), ported from the verified model of `facts/opening.md` and proven through the GS parity rule.

**Architecture:** `scene/opening/` ports the intro as plain C++ templated on `Arithmetic` (`Timeline`, `Fog`, `Lights`, `Cubes`, `Flat`, `Towers`, `TowerVu1`, `Handoff`) and `Opening<A>::frame()` assembles one `scene::Frame` per module frame in the OSD's order. Tests compare every part with the console's own probed memory (exported by `tools/scene/export_opening.mjs`), the carried run with every frame of the capture, and the frame with the capture's GS dump draw by draw. `parity/GsParityRenderer` is extended (24-bit depth, 16-bit textures, per-pixel alpha, alpha correction) so the frame also goes through PCSX2's software renderer under exact per-pixel budgets. `render/NativeRenderer` draws the frame with a normal pipeline. `app` chains `OpeningScreen` into `ClockScreen`.

**Tech Stack:** C++23, Vulkan 1.4, SDL3, Dear ImGui, nlohmann/json, CMake/CTest; Node 25 for the exporter and the tools; Watson (PCSX2) for the new capture.

**Spec:** `docs/superpowers/specs/2026-10-04-native-opening-design.md` (builds on `docs/superpowers/specs/2026-10-03-native-clock-design.md` and `docs/superpowers/plans/2026-10-03-native-clock.md`; the latest style is `CrystalClockVK-wt/menus/docs/superpowers/plans/2026-10-03-native-menus.md`).

## Global Constraints

- English only. No comments except where the OSD's arithmetic needs one; every scene function names its facts section or verifier in one line (`// facts/opening.md 4.3 verify_opening_fog.mjs expected`).
- Commits `Type(Scope): Short imperative description`, at most 72 characters (types `Fix`, `Feat`, `Refactor`, `Perf`, `Build`, `Docs`, `GS`; scopes `Core`, `Renderer`, `App`, `GS`, `Shaders`, `CI`, `Project`, plus `Scene`, `Tools` as the clock plan used). No attribution, co-author or "generated" line anywhere: commits, code, docs, reports.
- Never `git push`. Never `git add` a directory (a hook refuses): name each file. `git rm` per file.
- No junctions, no symlinks, no `git worktree remove` on a tree that holds one. Never touch another worktree (`native`, `menus`, `menus-input`, `sound`, `assets`, `icon`). Lane worktrees are `D:/CodingProjects/CrystalClockVK-wt/opening-<lane>` on `lane/opening-<lane>`; the controller merges them.
- The main checkout's `References/` (dumps, fixtures, textures) is read by absolute path through configurable variables: CMake cache `CLOCK_FIXTURE`, `CLOCK_REFERENCES`, `CLOCK_DUMPS`, `CLOCK_SCENE`, plus new `OPENING_FIXTURES` and `OPENING_TEXTURES`; environment `CLOCK_REFERENCES` for the node tools (default `D:/CodingProjects/CrystalClockVK/References`). Never modify tracked files of `D:/CodingProjects/CrystalClockVK`. Fixtures written under its `References/fixtures/` are git-ignored data; when the permission system refuses the write, write to `<worktree>/out/fixtures/<capture>/` and the controller installs them.
- `facts/` is the only trusted store; `docs/` (but `docs/superpowers/`), `context/`, `MEMORY.md` are never cited as fact. `References/model/*` and `References/scripts/*` are never edited: tasks 1, 2, 3 and 4 may add new files there or in `tools/`, nothing else. A gap in the model is a measurement item for the controller (skills `measure`, `capture`), not a port item.
- Sony data is never committed: textures extracted from GS memory go to the git-ignored `References/textures/opening/` of the main checkout; no dump, trace, texture or fixture enters git. `git status` is read before every commit.
- Canon build HDD OSD 1.10U, NTSC. ROM 2.30 and PAL branches are not ported (`pal` asserts).
- The parity rule (`src/parity/`, `GsParityRenderer`, `shaders/GsParity.*`, `tools/parity/`, `tools/ParityTool/`, its tests and budgets) is kept and never weakened: Task 12 only adds formats, with the existing budgets unchanged. Budgets are exact per pixel and fail both ways (`unexpected difference`, `larger difference`, `tighten`, `stale budget entry`); a new entry only where PCSX2's software texture cache explains it (a bilinear tap outside the draw's converted texture rectangle reads zero).
- Scene code that does float arithmetic is templated on `Arithmetic`: `EeArithmetic` in tests, `NativeArithmetic` in the app; the app target links no `EeArithmetic`.
- Windows, Vulkan 1.4, RX 6750 XT. Validation counts VALIDATION-type messages only.
- A test that needs a fixture registers only when the fixture exists; configure prints which gates are active.
- Build: from the worktree, `cmake -S . -B build && cmake --build build --config Debug`; tests `ctest --test-dir build -C Debug --output-on-failure`. Lane worktrees are configured with the same cache variables as the integration tree (FETCHCONTENT source dirs and `-DCLOCK_FIXTURE=D:/CodingProjects/CrystalClockVK/References/fixtures/hddosd-110U-whole3-clock/f0`) and initialise imgui with `git submodule update --init --reference D:/CodingProjects/CrystalClockVK/3rdparty/imgui`.
- The ledger lives in `.superpowers/sdd/2026-10-04-native-opening/progress.md` of the integration worktree (git-ignored); each task writes `task-<n>-report.md` beside it.
- Nothing goes into `main` without Jean's word.

## Review Focus

1. **A scene that matches isolated frames and drifts in the carried run.** State carried between frames: the integrator's block, the roll, the fog offsets, the lights' matrix history and trail ring, the cubes' angles, the logo walk, the ghost's store. Test: Task 15 `OpeningCarriedTest` runs `Opening<EeArithmetic>` from module counter 1 over all of `hddosd-110U-opening-full` and compares every frame; Task 7 to 11 each carry their own state in their isolated test.
2. **The hidden-vertex rule.** A vertex outside the bounds is written without the kick, and so are the three after it; it keeps the colour of the vertex before it. Test: Task 11 `TowerVu1Test` on `hddosd-110U-opening2-ee-late` must see 467 vertices sent without drawing and equal packets; the tower pass of the dump gate (Task 15) must have fewer triangles on exactly those strips.
3. **The rule's new formats hiding a regression.** Z24, CT16, PABE and FBA change how existing passes are drawn only if they are mis-detected. Test: Task 12 re-runs the clock gates (`ParityClock`, `ParityConfig`, `ParityToClock`, `ParityClockScene`) with every budget file unchanged and the 249 unskipped opening passes' result identical before and after.
4. **The lights' run-to-run phase.** The tests take the capture's own phase from its probes; the app's random phase never reaches a test. Test: Task 8 `LightsTest` feeds the probed `D_00370A74`; Task 16 asserts `Opening<NativeArithmetic>` accepts `--lights-phase` and that two runs with equal options give equal frames.
5. **The intro's length and the stage-1 hold.** 246 drawn frames with disc state 0x64 and the dive at counter 121; the hold set (0x65..0x69, 0x71) waits for z > 56 instead (verified lengths 135, 173, 166 frames after the release in `verify_opening3_stages.mjs`). Test: Task 7 `TimelineTest` runs the schedules of `-intro`, `-disc65-b`, `-disc69`, `-disc71`.
6. **`fromScene` adapting to the fixture.** It must stay a pure unit conversion; the opening layout is a table of buffer and texture addresses read from `facts/opening.md`, never derived from the dump under test. Test: Task 5 `FromSceneOpeningTest` converts a synthetic frame and checks fields; Task 15 reads no dump value into the layout.
7. **The chain `opening -> black gap -> clock`.** The clock starts from the T0 state of `whole-boot-opening`, not from the steady `start.json`; no state of the intro leaks into the clock; the last drawn intro frame is black (fade capped at 0x80). Test: Task 14 `ClockEntryTest` (49 frames) and Task 16's soak across the seam (0 validation errors, resize and minimize during the intro).
8. **EeArithmetic leaking into the app.** `OpeningSceneNative` links no `EeArithmetic` source (the clock's `ClockSceneNative` rule). Test: Task 16 checks the link line.
9. **An alpha above 1 clamped silently** by the native renderer for the opening's blends (cube passes use destination alpha as the factor). Test: Task 13 asserts in debug on a pass whose written alpha exceeds the target's bound.

## Rulings made while planning

- **R1.** Ports are specified by source (the verifier file and function that models it, or the facts section) plus exact test data, as in the native clock plan. `References/scripts/verify_opening_*.mjs` and `References/model/opening-lib.mjs`, `opening-libm.mjs` are the executable reference; the `_v2` copies (exact sums) are the arithmetic to follow where a plain copy differs (`facts/opening.md` 4.6). An implementer copies arithmetic and call order, not structure.
- **R2.** There is no whole-frame JS model of the opening (the clock has `clock_frame.mjs`). The ground truth is the console itself: the capture's probes (intermediate values) and the capture's GS dump (the draws). The exporter decodes probes; it does not wrap a model.
- **R3.** The scene is a closed system fed by `BootOptions` (disc schedule, lights phase, play history, forced-clock and hard-disk words). Each capture's values are read from its own probes.
- **R4.** Strips and fans are expanded in scene code; one `Pass` per run of draws under one GS state, so the dump's parser groups and the scene's passes agree. Where the scene splits a run more finely than the parser (the lights' sixteen sprite packets), the dump gate coalesces adjacent scene passes of equal state.
- **R5.** Without a play history there are no towers; the app default is empty, `--towers demo` loads the verified 21-entry history. The towers' gate runs on captures that carry their history.
- **R6.** The gap before the clock's first frame (29 frames with nothing sent, then 5 with the clock's init packets) is a constant of 34 frames from the last drawn scene frame, labelled measured; the gap table asks the controller to read what it waits for.
- **R7.** Hand-off outcomes other than module 2 (execute-app types, the illegal scene) are decided and reported by `Handoff::decide`, and the app treats them as module 2 in this slice.
- **R8.** The routine of the microprogram that moves coordinates with the normal (entry 21 non-zero) is dead code in the unpatched program and is not ported; `TowerVu1` takes entry 21 as zero and asserts it.
- **R9.** Sound: `Opening` appends `SoundEvent {id, argument, counter}` to a list the app drops; ids and arguments only (`facts/opening.md` section 3 table).

## Captures and fixtures

Captures live in `D:/CodingProjects/Watson/Runtime/captures/` (plain `.trace.jsonl`, `.gs`, `.png`; nothing is stored `.gz` for these, checked 2026-10-04), states in `D:/CodingProjects/Watson/Runtime/states/`, fixtures in `D:/CodingProjects/CrystalClockVK/References/fixtures/<capture>/`. Frame index 0 of a capture is its first emulator frame; for `opening-full` that is emulator frame 120 and the module counter is index - 6.

| Capture | Files | `opening.json` | `passes.json` | oracle frames | Used by |
|---|---|---|---|---|---|
| `hddosd-110U-opening-full` | exist (254 dump frames, 249 non-empty, 15 722 passes) | create (T4) | create (T1: 101 MB, done once in a scratch folder, re-run by T4) | create (T15: counters 3, 60, 100, 150, 200, 222, 240) | T4, T7, T8, T9, T10, T15 |
| `hddosd-110U-opening3-intro` | exist | create (T4, stages only) | — | — | T7 |
| `hddosd-110U-opening3-disc65-b`, `-disc69`, `-disc71` (the `opening3-*` stage set) | exist | create (T4, stages only) | — | — | T7 |
| `hddosd-110U-opening2-inputs` | exist | create (T4) | — | — | T6, T7 |
| `hddosd-110U-opening2-flat` | exist | create (T4) | — | — | T10 |
| `hddosd-110U-opening2-ee-a`, `-ee-b`, `-ee-late` | exist | create (T4) | create (T4) | — | T11 |
| `hddosd-110U-opening2-handoff-probes`, `hddosd-110U-opening3-illegal`, `-forced`, `-ready-b`, `-ill74-c`, `-disc6a` .. `-disc73` | exist | create (T4, hand-off blocks only) | — | — | T14 |
| `hddosd-110U-whole-boot-opening` | exist (49 clock frames) | `scene.json` by `tools/scene/export_fixture.mjs` (T2) | create (T2) | — | T14, T15 |
| **`hddosd-110U-opening4-towers-whole`** | **create (T2)** | T4 | T4 | T15: counters 150, 230 | T11, T15 |
| `hddosd-1.10U-host-opening-towers-full.p2s` (state) | exists | — | — | — | T2 |

Commands (Bash, from the worktree unless stated):
- passes: `node tools/parity/frame_passes.mjs D:/CodingProjects/Watson/Runtime/captures/<capture>.gs D:/CodingProjects/CrystalClockVK/References/fixtures/<capture>/passes.json`
- oracle frame: `node tools/parity/make_fixture.mjs D:/CodingProjects/Watson/Runtime/captures/<capture>.gs D:/CodingProjects/CrystalClockVK/References/fixtures/<capture>-c<counter>/f0 <frame index>`
- opening export: `OPENING_REFERENCES=D:/CodingProjects/CrystalClockVK/References node tools/scene/export_opening.mjs D:/CodingProjects/Watson/Runtime/captures/<capture>.trace.jsonl`
- clock export (existing): `CLOCK_BUILD=hdd CLOCK_REFERENCES=D:/CodingProjects/CrystalClockVK/References node tools/scene/export_fixture.mjs D:/CodingProjects/Watson/Runtime/captures/hddosd-110U-whole-boot-opening.trace.jsonl`

## Lanes

| Wave | Task | Where | Starts after |
|---|---|---|---|
| 0 | Setup: configure, build, `ctest -C Debug` baseline recorded (clock gates active, count of tests); confirm `References/textures/` and `References/fixtures/` ignore rules with `git check-ignore` | integration `CrystalClockVK-wt/opening` | — |
| A | T1 Audit: the rule's skip reasons on the opening | integration | setup |
| A | T2 Captures: the towers-whole capture and the clock-entry fixtures (data only) | capture lane (main checkout's `References/scripts`, no commits) | setup |
| A | T3 Opening textures | `CrystalClockVK-wt/opening-assets`, `lane/opening-assets` | setup |
| A | T6 Arithmetic and `Vu0` | `CrystalClockVK-wt/opening-lib`, `lane/opening-lib` | setup |
| — | T4 Exporter for the opening (towers blocks wait for T2) | integration | T1 |
| — | T5 Vocabulary, types and fixture reader | integration | T1, T4 schema |
| B | T7 Timeline | `CrystalClockVK-wt/opening-timeline`, `lane/opening-timeline` | T4, T5, T6 |
| B | T8 Fog and lights | `CrystalClockVK-wt/opening-fogl`, `lane/opening-fogl` | T4, T5, T6 |
| B | T9 Cubes | `CrystalClockVK-wt/opening-cubes`, `lane/opening-cubes` | T4, T5, T6 |
| B | T10 Flat draws and overlays | `CrystalClockVK-wt/opening-flat`, `lane/opening-flat` | T4, T5 |
| B | T11 Towers and `TowerVu1` | `CrystalClockVK-wt/opening-towers`, `lane/opening-towers` | T2, T4, T5, T6 |
| B | T12 Rule extensions | `CrystalClockVK-wt/opening-parity`, `lane/opening-parity` | T1 |
| B | T13 Native renderer for the opening | `CrystalClockVK-wt/opening-renderer`, `lane/opening-renderer` | T3, T5 |
| B | T14 Hand-off and clock entry | `CrystalClockVK-wt/opening-handoff`, `lane/opening-handoff` | T2, T4, T5 |
| — | merge lanes `lib`, `assets`, `timeline`, `fogl`, `cubes`, `flat`, `towers`, `parity`, `renderer`, `handoff` | integration | B |
| C | T15 Assembly, carried run, dump gate, pixel gates | integration | merge |
| D | T16 `--boot`, soak, screenshots, PNG sequence | integration | T15 |

Shared files and their owners: `CMakeLists.txt` (each lane appends its own block under a `# opening: <lane>` line; the controller merges); `src/scene/Frame.hpp`, `src/scene/opening/Types.hpp`, `src/parity/FromScene.*` (T5 only; lanes read them); `tests/scene/opening/OpeningFixture.*` (T5 only); `src/scene/Arithmetic.*` and `src/scene/opening/Vu0.hpp` (T6 only); `src/parity/GsParityRenderer.*`, `shaders/GsParity.*`, `src/parity/Fixture.*`, `tools/parity/make_fixture.mjs` (T12 only); `src/render/NativeRenderer.*`, `shaders/Native.*` (T13 only); `src/scene/opening/Opening.*`, `tests/scene/opening/OpeningGateTest.cpp` (T15 only); `src/app/*` (T14 adds `Screen.hpp` and `ClockScreen.*`, T16 the rest). T7 to T11 each own exactly their `src/scene/opening/<Part>.*` and `tests/scene/opening/<Part>Test.cpp`.

## File Structure

| Path | Responsibility |
|---|---|
| `tools/parity/skip_report.mjs` | per capture, skip reasons, blends, formats, PABE/FBA/mip state of every pass |
| `References/scripts/extract_opening_textures.mjs` | opening textures from GS memory to PNG (new file; run by T3) |
| `tools/scene/export_opening.mjs`, `tools/scene/export_opening.test.mjs` | the capture's probes as named values |
| `src/scene/opening/Types.hpp` | `BootOptions`, `History`, `DiscSchedule`, `SoundEvent`, `HandOff`, per-part state structs |
| `src/scene/opening/Vu0.hpp` | libvu0 as the opening calls it, templated on `A` |
| `src/scene/Arithmetic.{hpp,cpp}` | gains `sinf`, exact `add`/`sub`/quotient/root where `opening-lib.mjs` differs |
| `src/scene/opening/{Timeline,Fog,Lights,Cubes,Flat,Towers,TowerVu1}.{hpp,cpp}` | the parts |
| `src/scene/opening/Opening.{hpp,cpp}` | `Opening<A>` assembly |
| `src/scene/opening/Handoff.{hpp,cpp}` | the decision and the measured gap |
| `src/scene/Frame.hpp` | the vocabulary additions |
| `src/parity/FromScene.{hpp,cpp}` | `openingLayout()` and the new fields |
| `tests/scene/opening/OpeningFixture.{hpp,cpp}` | reader of `opening.json` |
| `tests/scene/opening/*Test.cpp` | the parts' tests; `OpeningCarriedTest.cpp`, `OpeningGateTest.cpp` |
| `src/app/Screen.hpp`, `src/app/ClockScreen.{hpp,cpp}`, `src/app/OpeningScreen.{hpp,cpp}` | the seam and the two screens |
| `tools/ParityTool/budgets/hddosd-110U-opening-full-c<counter>.json` | exact budgets per gated frame |

---

### Task 1: Audit — the rule's skip reasons on the opening (measurement)

Approved: controller, 2026-10-04 mandate

**Files:**
- Create: `tools/parity/skip_report.mjs`, `tools/parity/skip_report.test.mjs`
- Data: a report in `.superpowers/sdd/2026-10-04-native-opening/task-1-report.md`; no fixtures

**Interfaces:**
- Consumes: `tools/parity/frame_passes.mjs` (`framePasses`), `tools/parity/make_fixture.mjs` (`describeState`), Watson `gs/parse.js` and `gs/registers.js` (as those tools import them).
- Produces: `node tools/parity/skip_report.mjs <dump.gs> [--frames a-b]` printing, per frame range and in total: pass counts per skip reason; for every pass carrying PABE, FBA, mip-mapping (`TEX1.MMIN` > 1 or `MXL` > 0), a non-zero `ZBUF.PSM`, `TEX0.PSM` 0x02, `COLCLAMP` 0, `DTHE`, `TEXA` use: the target, the texture id and size, `ALPHA`, `PRIM`, the frame index and the packet's first draw index; the closed list of `ALPHA` (A, B, C, D) combinations with their `FIX` and `PRIM` bits; every texture's `TBP0`, `TBW`, `PSM`, `TW`, `TH`, and `MXL`.

- [ ] **Step 1: Test first** `tools/parity/skip_report.test.mjs` (Node test runner): on a synthetic two-draw state set (built from `describeState`'s own inputs), the report counts one pass under `depth format 0x31` and one under none. RED (no tool).
- [ ] **Step 2:** Implement. Do not edit `frame_passes.mjs` or `make_fixture.mjs`: import their exports.
- [ ] **Step 3:** Run on `hddosd-110U-opening-full.gs`, `hddosd-110U-opening2-ee-a.gs`, `hddosd-110U-opening2-ee-late.gs`, `hddosd-110U-opening2-flat.gs`. Expected on `opening-full`: 254 frames, 15 722 passes, 249 with no reason; reasons `depth format 0x31` 8 763, `depth format 0x31; texture format 0x02` 5 436, `depth format 0x31; per-pixel alpha blending` 1 253, `alpha correction` 1 and the combinations listed by the audit already run on 2026-10-04.
- [ ] **Step 4:** In the report answer, from the output: (a) which draws carry PABE and FBA (which function sends them, by pass shape) and whether their blend differs from the non-PABE result (alpha always 0x80 there?); (b) the towers' texture 6: size (`facts/opening.md` section 2 says 256 x 256, mip-mapped; the dump's `TEX0` of the tower draws in `-ee-a` gives `TW`/`TH`), its `PSM`, `TEX1` and whether `MMIN` > 1; (c) the closed list of blends for `BlendOp`; (d) whether `make_fixture.mjs <dump> <out> <frame>` for a frame well inside the dump (frame 100 of `opening-full`) yields a `start` buffer holding the previous frame's picture (the ghost's store non-black) — run it once into the scratch folder; if the start is the dump's initial memory only, record it as a blocker for the pixel gates and propose the fallback (oracle replay of the preceding frames).
- [ ] **Step 5: Commit** `Feat(Tools): Report the rule's skip reasons on a GS dump`.

### Task 2: Captures — the towers-whole run and the clock entry (data only)

Approved: controller, 2026-10-04 mandate

**Files:** none in the repository. Data: one capture in `D:/CodingProjects/Watson/Runtime/captures/`, fixtures in the main checkout's `References/fixtures/`.

**Interfaces:**
- Consumes: the `capture` skill (`References/scripts/with_emulator.mjs`, `capture.mjs`), `verify_opening_towers_ee.mjs` and `verify_opening_lights.mjs` (their `PROBES` merge into one capture; they probe different addresses), the state `hddosd-1.10U-host-opening-towers-full.p2s` (breakpoint `0x00221D30`, history written, 126 towers).
- Produces: `hddosd-110U-opening4-towers-whole` (`.gs`, `.trace.jsonl`, `.png`): the whole intro with 126 towers and the probes that give its history, its lights phase and its towers' chains; the clock-entry fixtures of `hddosd-110U-whole-boot-opening`.

- [ ] **Step 1: Capture** (from `D:/CodingProjects/CrystalClockVK`, Bash; exact interpreters; the frame count covers the intro (246 drawn frames from module counter 1) with the states' start in front):

```bash
node References/scripts/with_emulator.mjs node References/scripts/capture.mjs --verifiers verify_opening_towers_ee.mjs,verify_opening_lights.mjs --build hdd --state-file D:/CodingProjects/Watson/Runtime/states/hddosd-1.10U-host-opening-towers-full.p2s --frames 262 --name hddosd-110U-opening4-towers-whole
```

Read `Watson/Runtime/captures/hddosd-110U-opening2-ee-late` and `facts/opening.md` section 1 first: the existing `-ee-late` capture was taken with an advance after the state; a capture from the state's first frame has the module's set-up in it (`OpeningInitTowersFog` runs at the breakpoint). If the capture is refused or the probes of `towers_ee` at `0x00221D30` do not fire, record the output and use the closest working flags (`--mode`, `--frames`, the same `--advance` the `-ee-late` capture used if `capture.mjs` documents one); the aim is fixed: history table, phase, per-tower chains and the GS dump for counters 1..246.
- [ ] **Step 2: Verify** with `node References/scripts/verify_opening_towers_ee.mjs <trace>` and `verify_opening_vu1_v2.mjs <trace>`: both `FOUND` (chains equal, packets equal); and `verify_opening_lights.mjs`: `FOUND`. Expected: towers per frame 126 from the intro's frame 7 to 252 of the run (`facts/opening.md` section 2). A capture that is not `FOUND` is not used: report its output to the controller (a model gap is a measurement item).
- [ ] **Step 3:** `node tools/parity/skip_report.mjs <dump>` (Task 1's tool, when merged) or `frame_passes.mjs` on the new dump: confirm PATH1 draws exist (strips of `PRIM 0x9C`, 6 per tower) and record the count of towers per frame.
- [ ] **Step 4: Clock-entry export:** run the clock exporter (Commands above) on `hddosd-110U-whole-boot-opening` and its passes command. Expected: `scene.json` frames = the 49 the facts page lists; the exporter refuses to write when the model does not reproduce the capture (it does: `FOUND`).
- [ ] **Step 5: Record** the commands, verdict lines, frame counts, tower counts and the capture's lights phase in the task report. No commit (data only).

### Task 3: Opening textures

Approved: controller, 2026-10-04 mandate

**Files:**
- Create: `References/scripts/extract_opening_textures.mjs`, `References/scripts/extract_opening_textures.test.mjs`
- Data (git-ignored, main checkout): `References/textures/opening/tex<index>-<w>x<h>.png`, `References/textures/opening/manifest.json`

**Interfaces:**
- Consumes: `facts/opening.md` section 2 (table `D_002AF870`: indices 0, 2, 3, 5, 6, 8, 10, 11, 12 for the intro, sizes and resource ids), the style of `References/scripts/extract_textures.mjs` (decoding PSMCT32 from the dump's GS memory), `References/scripts/extract_buffers.mjs` (`word32`), the audit's list of `TBP0`, `TBW`, `PSM`, `TW`, `TH` per texture (Task 1).
- Produces: PNGs of every intro texture as RGBA8, alpha as the GS expands it; `manifest.json` `{ index, resource, width, height, tbp0, tbw, psm, source: { capture, frame } }`.

- [ ] **Step 1: Test first:** a synthetic GS memory image with one PSMCT16 texel pattern (bits 0x7FFF, 0x8000, 0x03E0, 0x001F) decodes to RGBA `(248,248,248,TA1)`, `(0,0,0,TA1)`, `(0,248,0,TA0)`, `(248,0,0,TA0)`... exactly as the rule below gives; and a PSMCT32 image decodes as the clock script does. RED.
- [ ] **Step 2:** Implement. PSMCT16 rule (GS): each 5-bit channel `c` becomes `c << 3`; alpha is `TA1` when bit 15 is set, else `TA0` (`TEXA`), with `TEXA.AEM` zeroing alpha for black texels when set; use the `TEXA` the intro's draws carry (Task 1 output). The page layout of PSMCT16 (blocks of 16 x 8, pages of 64 x 64) is the GS's: the decoder follows the table in `GSLocalMemory` terms already used by `References/scripts/extract_buffers.mjs`; if that file has no 16-bit address function, add the 16-bit `word16` function in the new script and test it on the synthetic image.
- [ ] **Step 3:** Run on a dump that holds the textures in its start memory or its transfers: try `hddosd-110U-opening-full.gs` first (module init is emulator frame about 127; the dump starts at 120). If the textures are not yet uploaded at the dump's start memory, replay the dump's own IMAGE transfers of frame 0..10 into the memory image before decoding, or take them from the first frame of `hddosd-110U-opening2-flat.gs`; record which source worked. Output 9 PNGs and the manifest.
- [ ] **Step 4: Check** each texture against the dump: for 5 draws of each texture in `passes.json` of `opening-full`, the texture's id (`t<tbp0>-<tbw>-<psm>-<w>x<h>`) matches the manifest's. Tower texture 6 is extracted from a tower dump (`opening2-ee-a.gs`) when its size is settled (Task 1 step 4b).
- [ ] **Step 5:** `git check-ignore -v References/textures/opening/manifest.json` prints the ignore rule (if not ignored, add the pattern to `.gitignore` in this commit). **Commit** `Feat(Tools): Extract the opening's textures from GS memory`.

### Task 4: Exporter for the opening

Approved: controller, 2026-10-04 mandate

**Files:**
- Create: `tools/scene/export_opening.mjs`, `tools/scene/export_opening.test.mjs`

**Interfaces:**
- Consumes: the verifiers' exports (`PROBES`, `expected`, `run`, `setup`, `brightness`, `tower`, `expect`, `verify`) of `verify_opening_camera.mjs`, `verify_opening_stages.mjs`, `verify_opening_lights.mjs`, `verify_opening_fog.mjs`, `verify_opening_cubes.mjs`, `verify_opening_overlays.mjs`, `verify_opening_flat.mjs`, `verify_opening_inputs.mjs`, `verify_opening_towers_ee.mjs`, `verify_opening_vu1.mjs`, `verify_opening3_handoff.mjs`, `verify_opening3_stages.mjs`; `References/lib/trace.mjs` (`readTraceFor`); `References/model/opening-lib.mjs`.
- Produces `References/fixtures/<capture>/opening.json` (R2: probed memory only, no model values). Floats are `"0x"` + 8 hex digits; integers are numbers; byte ranges are hex strings.

```
{ capture, build: "hdd", video: "ntsc",
  externals: { firstCounter, phase, discAtCounter: [[counter, state]...], history: [ {name, count, mask, mainCell} x21 ] | null, emptyName,
               clockForced, hddReady, hddExec },
  frames: [ { index, counter,
    timeline: { stage, block: B at entry, camera [x,y,z,w], roll, up [4], threshold, discState, go,
                after: { block, camera, roll, up, matrices: { camera, viewScreen, worldToScreen, normalLight } }, result, fade: {argument}?, blur: {level}?, logo: {value, alpha}? },
    fog:    { offsets[6] at entry, offsetsAfter[6] },
    lights: { phase, matrices at entry [4 x 4 x 16 floats], ring {head, tail, entries[128]}, constants, after: same two },
    cubes:  [ { n, angles, rates, record: { corners, faces, facing, colours, edge } per verify_opening_cubes.mjs's compare list } ],
    towers: [ { column, row, chain: hex 0x840 bytes, ... } ] | absent } ],
  setup: { fogMesh[289], fogBrightness[289], towerCells, towerBrightness[400], sines/cosines the capture probed } }
```
  Per block the exporter carries exactly the values the named verifier reads from probes and compares, named after that verifier's own variables. A block is absent when the capture has no probe for it.
  Hand-off blocks (captures `opening2-handoff-probes`, `opening3-*`): `handoff: { snapshot, forced, hddReady, hddExec, cdda, execute, module, previous }` before and after, as `verify_opening3_handoff.mjs` reads them.

- [ ] **Step 1: Test first** `tools/scene/export_opening.test.mjs`: on `hddosd-110U-opening-full`, `opening.json` has `externals.phase` equal to the value `verify_opening_lights.mjs` prints for that capture, `frames.length` equal to the camera verifier's count of module calls (247 on `-intro`; read it from `verify_opening_camera.mjs` output on this capture), every float a 10-character hex string, `cubes` blocks summing to 994, and re-running gives identical bytes; the verifiers on the same capture still say FOUND (the exporter imports, never edits them). RED.
- [ ] **Step 2:** Implement (cache the parsed trace once per run; 200 MB traces: stream with `readTraceFor`, never `JSON.parse` the file whole).
- [ ] **Step 3:** Generate (Commands above) for `hddosd-110U-opening-full`, `-opening3-intro`, `-opening3-disc65-b`, `-disc69`, `-disc71`, `-opening2-inputs`, `-opening2-flat`, `-opening2-ee-a`, `-ee-b`, `-ee-late`, the hand-off captures of the table, and `-opening4-towers-whole` once Task 2 has made it. Passes: `frame_passes.mjs` for `opening-full`, `-ee-a`, `-ee-late`, `-opening4-towers-whole`.
- [ ] **Step 4: Commit** `Feat(Tools): Export the opening's probed values for the native tests`.

### Task 5: Vocabulary, types and the fixture reader

Approved: controller, 2026-10-04 mandate

**Files:**
- Create: `src/scene/opening/Types.hpp`, `tests/scene/opening/OpeningFixture.hpp`, `tests/scene/opening/OpeningFixture.cpp`, `tests/scene/opening/FromSceneOpeningTest.cpp`
- Modify: `src/scene/Frame.hpp`, `src/parity/FromScene.hpp`, `src/parity/FromScene.cpp`, `CMakeLists.txt`

**Interfaces — Produces:**
```cpp
namespace scene {
enum class TargetName { Display, RefractionSource, Work, Store, Extra };       // Store: half-width copy (GS 0x2300); Extra: 1024 x 256
enum class BlendOp { Opaque, Add, AlphaOver, Subtract, FixedOver, FixedAdd, AddDestinationAlpha, SubtractFixed };   // closed by Task 1's list
enum class TextureSet { Clock, Opening };
struct Material { /* existing */ bool perPixelAlpha = false; bool alphaCorrection = false; };
struct Frame    { /* existing */ TextureSet textureSet = TextureSet::Clock; int32_t depthBits = 32; };
}
namespace scene::opening {
struct HistoryEntry { std::array<char, 16> name{}; uint8_t count = 0, mask = 0, mainCell = 0; };
using History = std::array<HistoryEntry, 21>;
struct DiscSchedule { uint32_t first = 0x65, later = 0x64; };                   // state at counter 1, from counter 2
struct BootOptions { DiscSchedule disc; uint32_t lightsPhase = 0xD80; std::optional<History> history;
                     bool clockForced = false, hddReady = false; int32_t hddExec = 0; bool pal = false; };
struct SoundEvent { uint32_t id; int32_t argument; int32_t counter; };
struct HandOff { int32_t module, executeAppType; bool previousWasOpening; };
}
namespace parity { GsFrameLayout openingLayout(int32_t displayIndex); }          // buffers and textures at their GS addresses
```
`openingLayout` is a table read from `facts/opening.md` sections 2 and 4.5 (display pages 0x0000 and 0x08C0, store 0x2300, extra 0x1A40; texture ids as `t<tbp0>-<tbw>-<psm>-<w>x<h>` from Task 1's list), with `depthBits` 24 giving ZBUF format 1; `fromScene` converts the new fields (`perPixelAlpha` to PABE, `alphaCorrection` to FBA, the two blends to ALPHA's selectors) and nothing else.
`OpeningFixture` (tests): `load(path)`, `frame(k)`, typed accessors for each block of Task 4's schema, hex floats read bit for bit (as `scene::hexFloat`).

- [ ] **Step 1: Test first** `FromSceneOpeningTest.cpp`: a synthetic frame with one pass per new field converts to a `GsFrame` whose draw state has PABE 1 / FBA 1 / the expected `ALPHA` selectors for each new `BlendOp` (Cs x Ad + Cd is A=Source, B=Zero, C=DestinationAlpha, D=Destination; Cd - Cs x fixed is A=Zero, B=Source, C=Fixed, D=Destination) and ZBUF format 1; `openingLayout(0)` and `(1)` swap the display page; the clock's layouts and tests are unchanged. RED.
- [ ] **Step 2:** Implement; `ClockParityTest`, `HeadTest`, `RodsTest` and the other clock tests unchanged and green (`ctest -C Debug`).
- [ ] **Step 3: Commit** `Feat(Scene): Add the opening's frame vocabulary and layout`.

### Task 6: Arithmetic and `Vu0`

Approved: controller, 2026-10-04 mandate

**Files:**
- Modify: `src/scene/Arithmetic.hpp`, `src/scene/Arithmetic.cpp`, `tests/scene/ArithmeticTest.cpp`
- Create: `src/scene/opening/Vu0.hpp`, `tests/scene/opening/Vu0Test.cpp`, `tools/scene/opening_vectors.mjs`

**Interfaces — Produces:**
```cpp
struct EeArithmetic { /* existing */ static float sinf(float x);   // opening-libm.mjs sinf (__kernel_sinf, __ieee754_rem_pio2f up to 2^7 pi/2)
                                    static float cosf(float x);   // replaced by opening-libm.mjs cosf when it differs from ee_libm cosf
                                    static float addExact(float, float); static float subExact(float, float);
                                    static float quotientExact(float, float); static float rootExact(float); };
struct NativeArithmetic { static float sinf(float x); static float addExact(float a, float b) { return a + b; } /* ... */ };
namespace scene::opening { template <class A> struct Vu0 {
    static Mat4 rotMatrix(const Vec4& angles);                // sceVu0RotMatrix 0x0027B3D8: Z, then Y, then X (opening-lib.mjs)
    static Mat4 mulMatrix(const Mat4&, const Mat4&); static Mat4 transMatrix(const Mat4&, const Vec4&);
    static Vec4 subVector(const Vec4&, const Vec4&); static Mat4 unitMatrix();
    static Mat4 cameraMatrix(const Vec4& position, const Vec4& direction, const Vec4& up);                     // 0x0027B450
    static Mat4 viewScreenMatrix(float distance, float ax, float ay, float cx, float cy, float zmin, float zmax, float near, float far);  // 0x0027B628
    static Mat4 normalLightMatrix(/* the three light vectors and their colours as sceVu0NormalLightMatrix takes them */);
    static bool clipAll(/* clip box */, const Mat4& m, std::span<const Vec4> vertices);                          // verify_opening_lights.mjs clipAll
}; }
```
`Vu0<A>` uses the exact helpers (`opening-lib.mjs` `add`, `sub`, `quotient`, `root`) wherever the `_v2` verifiers do (`facts/opening.md` 4.6: a single-precision sum is not always the double result cut toward zero when one addend is below the double's precision).

- [ ] **Step 1: Test vectors:** `tools/scene/opening_vectors.mjs` writes `tests/scene/opening/vu0_vectors.json` (small, committed: no Sony data, values from `opening-lib.mjs` and `opening-libm.mjs` on fixed inputs): `sinf`/`cosf` at 0, pi/2 - 1e-7, pi, 2.6, 100 pi/2 .. 2^7 pi/2 boundaries, an angle within 0.006 of pi/2 (the case where plain and exact helpers differed on ROM cubes); `rotMatrix` of three angle sets; `mulMatrix`, `transMatrix`, `cameraMatrix`, `viewScreenMatrix(1024, 1, ay, 2048, 2048, 1, zmax, 1, 65536)`; `add` cases with `x + 1e-22`; division by zero giving the largest single. Each value as a hex pattern.
- [ ] **Step 2: Test first** `Vu0Test.cpp` and extend `ArithmeticTest.cpp`: every vector bit for bit; and, when `opening.json` of `hddosd-110U-opening2-inputs` exists, the probed library results (`sinf`, `cosf`: 1 744 + 988 + 486 + 30; `sceVu0RotMatrix`/`MulMatrix` of the 994 cubes: 63 616 values; the camera, view-screen and product of 258 frames) equal. RED.
- [ ] **Step 3:** Implement `Arithmetic` additions and `Vu0.hpp`. `EeArithmetic::cosf` stays as the clock uses it unless the opening's `cosf` differs for an argument both reach; if it differs, the opening calls a separate `cosfOpening` and the clock's tests stay green.
- [ ] **Step 4:** GREEN; the clock's `ArithmeticTest` cases unchanged. **Commit** `Feat(Scene): Port the opening's sine, cosine and libvu0 routines`.

### Task 7: Timeline

Approved: controller, 2026-10-04 mandate

**Files:**
- Create: `src/scene/opening/Timeline.hpp`, `src/scene/opening/Timeline.cpp`, `tests/scene/opening/TimelineTest.cpp`
- Modify: `CMakeLists.txt` (its own block)

**Interfaces — Produces:**
```cpp
namespace scene::opening {
struct TimelineState { int32_t counter, stage; Block block; Vec4 camera; float roll; Vec4 up; bool go; int32_t pending; /* fields as verify_opening_camera.mjs read(probe) */ };
struct Matrices { Mat4 camera, viewScreen, worldToScreen, normalLight; };
struct TimelineStep { int32_t result;                 // 0 running, 2 = scene + 1 (the intro ends)
                      Matrices matrices; int32_t fadeAlpha, blurLevel, logoAlpha; int32_t logoValue;
                      std::vector<SoundEvent> sounds; };
template <class A> class Timeline {
public:
    Timeline(const BootOptions&, const TimelineState& initial);        // initial: the scene set-up (camera (0,0,16), roll -0.12, B step z 0.04, roll velocity 0.001)
    TimelineStep step(uint32_t discState);                              // OpeningProcessInner 0x0021EF00 once, then the matrices
    const TimelineState& state() const;
};
}
```
Source: `facts/opening.md` section 3 (stage selection, stages 1 and 2, the integrator in its order, the matrices, `ay` NTSC `D_0036F984`), `verify_opening_camera_v2.mjs` and `verify_opening_stages.mjs` (handlers), `verify_opening3_stages.mjs` (stage 1 hold set and stage 2 sound branches), logo walk `func_0021DB50`, fade `func_00221B00`, blur level `func_00221A50` (`verify_opening_overlays.mjs`, `verify_opening_flat.mjs`).

- [ ] **Step 1: Test first** `TimelineTest.cpp`: (a) isolated: for every module call of `opening-full` and `opening3-intro` (247), from the probed state at entry, `step` leaves the probed state and result (block, camera, roll, up, matrices bit for bit); (b) carried: from the scene set-up state, 247 calls fed the capture's disc schedule equal the probes' states; (c) lengths: 246 drawn frames with 0x65 then 0x64; the dive starts at counter 121 (z 20.9); blur level 1 at counter 214 (z 68.6), 2 at 226, 3 at 236; fade starts at 218 and is 131 capped to 0x80 at 246; logo walks +4 to 0xF0 then -4 from counter 51 for 120 draws with alpha `min(0x70, value)`; (d) the hold set: schedules of `-disc65-b`, `-disc69`, `-disc71` hold stage 1 until z > 56 and reach the end after the verified 135, 173, 166 frames from the release; (e) sound events: `0x6140,1` from stage 2's first frame with no disc. RED.
- [ ] **Step 2:** Implement templated on `A`; every single-precision operation through `A`'s exact helpers in the integrator's order; `NativeArithmetic` builds the same code.
- [ ] **Step 3:** GREEN with `EeArithmetic` and, separately, `NativeArithmetic` builds and runs the schedule (no equality, 246 frames reached). **Commit** `Feat(Scene): Port the opening's timeline and camera`.

### Task 8: Fog and lights

Approved: controller, 2026-10-04 mandate

**Files:**
- Create: `src/scene/opening/Fog.hpp`, `src/scene/opening/Fog.cpp`, `src/scene/opening/Lights.hpp`, `src/scene/opening/Lights.cpp`, `tests/scene/opening/FogTest.cpp`, `tests/scene/opening/LightsTest.cpp`
- Modify: `CMakeLists.txt` (its own block)

**Interfaces — Produces:**
```cpp
namespace scene::opening {
template <class A> class Fog {                                            // facts/opening.md 4.3, 4.6; verify_opening_fog_v2.mjs expected
public:
    Fog();                                                                // builds the 17 x 17 mesh and brightness at counter 0 (func_0021DEA8)
    void draw(const Matrices&, std::vector<Pass>& out);                   // six passes (one per layer), advances the six offsets
    const std::array<float, 6>& offsets() const;
};
template <class A> class Lights {                                         // facts/opening.md 4.2; verify_opening_lights_v2.mjs expected
public:
    explicit Lights(uint32_t phase);
    void draw(int32_t counter, const Matrices&, std::vector<Pass>& out);  // four pairs of sprites per light, four trails; ring of 128 carried
};
}
```
Each fog layer is one `Pass` (`PRIM 0x5C` quads as triangle pairs, texture `D_003653E8[n]` = 5, 3, 2, 5, 3, 2, `FixedAdd` 20, depth write off, z lowered by 5 n); quads whose four corners fail the clip test are left out; lights: textured triangle strips as lists, `Add`, alpha `24` or `12` x (age + 1) / 5, trails `Lines` (a vertex off the picture, or whose predecessor was, breaks the strip, as `XYZF3`).

- [ ] **Step 1: Test first:** `FogTest`: from `opening.json` of `opening-full`: mesh and brightness (1 156 + 1 156 values) bit for bit; offsets carried equal in 1 470 of 1 470; 246 frames x 6 layers; per frame the number of quads drawn summed over the run equals 127 136 and the quads left out 250 720. `LightsTest`: with the capture's phase, per frame centres and the four matrices of each light equal the probed ring state of the next frame; the trail ring (head, tail, entries) equal; 218 frames drawn (counter 7..224 index), 3 488 sprite packets and 872 trail packets, 244 quads left out and 137 trail vertices sent without drawing (both branches of the clip test exercised). RED.
- [ ] **Step 2:** Implement. **Step 3:** GREEN (`EeArithmetic`). **Commit** `Feat(Scene): Port the opening's fog and light orbs`.

### Task 9: Cubes

Approved: controller, 2026-10-04 mandate

**Files:**
- Create: `src/scene/opening/Cubes.hpp`, `src/scene/opening/Cubes.cpp`, `tests/scene/opening/CubesTest.cpp`
- Modify: `CMakeLists.txt` (its own block)

**Interfaces — Produces:**
```cpp
namespace scene::opening {
struct CubeWork { /* corners (world, eye, screen, q, 12.4 ints), faces (object/world/view normals, facing, colour, edge terms) as verify_opening_cubes.mjs compares */ };
template <class A> class Cubes {                                          // facts/opening.md 4.1; verify_opening_cubes_v2.mjs
public:
    Cubes();                                                              // InitLightsCubes 0x002209E0: places, angles, rates (no random number)
    void draw(const Matrices&, const Vec4& cameraPosition, std::vector<Pass>& out, std::vector<CubeWork>* probe = nullptr);   // five cubes, ten passes each
};
}
```
Per cube: angles += rates wrapped to -pi..pi, `Vu0::rotMatrix`, world, eye direction, screen, `q`, the clip test (`D_002B0C20`/`D_002B0C30`), per face normal and facing and colour and edge term, then the ten passes of the table in `facts/opening.md` 4.1 in order (0..4 away faces into `Extra`, 5..9 toward faces into `Display`), refraction `S = u / 1024`, `T = (v - half) / 256` as projective `Q` 1, mirror, fixed and graded-colour generators, `AA1` per the rule (antialiased when turned away or facing > `D_0036FB80`), blends `AlphaOver`, `Add`-over-`FixedAdd`, `AddDestinationAlpha`, and textures 10, 11, 12.

- [ ] **Step 1: Test first:** from `opening.json` of `opening-full`: for each of the 994 cubes drawn the angles (994 of 994), the two matrices (1 988 of 1 988), the centre (994 of 994), the 75 544 work-record values bit for bit; 95 left out by the clip test; the pass descriptors (blend, `PRIM` flags, texture) equal the probed `ALPHA` of 9 940 passes. Carried: five cubes over 246 frames, angles never drift. RED. (Vertex packets are gated against the dump in Task 15.)
- [ ] **Step 2:** Implement following `verify_opening_cubes_v2.mjs`'s `transform` and `pass` call order. **Step 3:** GREEN. **Commit** `Feat(Scene): Port the opening's glass cubes`.

### Task 10: Flat draws and overlays

Approved: controller, 2026-10-04 mandate

**Files:**
- Create: `src/scene/opening/Flat.hpp`, `src/scene/opening/Flat.cpp`, `tests/scene/opening/FlatTest.cpp`
- Modify: `CMakeLists.txt` (its own block)

**Interfaces — Produces:**
```cpp
namespace scene::opening {
struct FlatInputs { int32_t counter; int32_t displayIndex; int32_t field; int32_t logoAlpha, fadeAlpha, blurLevel; };
template <class A> struct Flat {                                          // facts/opening.md 4.4, 4.5; verify_opening_flat.mjs, verify_opening_ghost.mjs, verify_opening_overlays_v2.mjs
    static void scissor(std::vector<Pass>&);                                        // OpeningProcess, 1 packet
    static void ghost(const FlatInputs&, std::vector<Pass>&);                       // func_0021D140(1, 2, 0x50, 0xFFFFFF, 0x80): store over the frame, FixedOver 0x50
    static void copyToStore(const FlatInputs&, std::vector<Pass>&);                 // func_0021CF38: frame at half width into Store; CLAMP_1 0 (HDD OSD)
    static void blur(const FlatInputs&, std::vector<Pass>&);                        // func_0021D3D0: n round trips through Extra, 9 + 6 n packets
    static void fade(const FlatInputs&, std::vector<Pass>&);                        // func_0021D848: black rectangle, alpha capped 0x80
    static void logo(const FlatInputs&, std::vector<Pass>&);                        // func_0021D990: texture 0, rows 1..30 and 33..62, NTSC rectangles 0 and 1
    static void bars(const FlatInputs&, std::vector<Pass>&);                        // func_0021D6C0: picture height 164, bars of 30 rows, FixedOver 0x80
};
}
```
The clear sprites of `Framebuffer` land outside the scissor (measured, `extract_opening_clear.mjs`): they are not drawn and not in the scene. The ghost is black over black when nothing is drawn before it (no towers).

- [ ] **Step 1: Test first:** from `opening.json` of `hddosd-110U-opening2-flat` (557 helper calls: copy 247, bars 247, fade 30, blur 33; 5 313 packets): the arguments each helper received (counter parity, `W x H / 64` or 0 page, blur rectangle sizes `(7W/8 - 1 - i(n - 1)) x (7H/8 - 1 - i(n - 1))`, bar heights 164/30, fade alpha) equal the scene's; pass counts per helper equal (blur 15, 21, 27 packets for level 1, 2, 3). Logo: 120 draws, alpha equal the 120 probed alphas. RED.
- [ ] **Step 2:** Implement (vertex values are gated against the dump in Task 15; here the arguments and the pass shapes). **Step 3:** GREEN. **Commit** `Feat(Scene): Port the opening's ghost, copies, blur, fade, logo and bars`.

### Task 11: Towers and `TowerVu1`

Approved: controller, 2026-10-04 mandate

**Files:**
- Create: `src/scene/opening/Towers.hpp`, `src/scene/opening/Towers.cpp`, `src/scene/opening/TowerVu1.hpp`, `src/scene/opening/TowerVu1.cpp`, `tests/scene/opening/TowersTest.cpp`, `tests/scene/opening/TowerVu1Test.cpp`
- Modify: `CMakeLists.txt` (its own block)

**Interfaces — Produces:**
```cpp
namespace scene::opening {
struct TowerChain { std::array<uint8_t, 0x840> bytes; };                  // the chain as started (facts/opening.md 5.1)
struct TowerVertices { std::array<Vertex, 24> vertices; std::array<bool, 24> kicked; std::array<float, 4> alphaBlend; };
template <class A> class Towers {                                         // facts/opening.md 5.2; verify_opening_towers_ee.mjs setup, brightness, tower
public:
    explicit Towers(const History&);                                      // OpeningInitTowersFog 0x00221D30: 126 cells, sway/tall, place, fade count; brightness 20 x 20
    void draw(int32_t counter, const Matrices&, const Vec4& camera, std::vector<Pass>& out, std::vector<TowerChain>* probe = nullptr);   // func_002214F8: one pass over the grid, no sorting
};
template <class A> struct TowerVu1 {                                      // facts/opening.md 5.1; verify_opening_vu1_v2.mjs run
    static TowerVertices run(const TowerChain&);                          // three parts, 0..25, 33..96, entry 21 = 0
};
}
```
Per tower: six faces of four vertices as strips `PRIM 0x9C` (AA1, textured, Gouraud), expanded to triangles; a vertex outside the bounds is not kicked, nor are the three after it, and keeps the previous colour; the blend packet `ALPHA 0x80_00000044` (`PABE` 0). The tower pass uses texture 6 (size per Task 1's audit) and `depthBits` 24.

- [ ] **Step 1: Test first:** `TowersTest`: from the history probed in `hddosd-110U-opening2-ee-a`, `-ee-b`, `-ee-late`: set-up tables 126 of 126 each, 504 place values, brightness 400 of 400; per frame the 0x840-byte chains equal word for word (1 260, 1 260, 3 780; 6 300 chains in 50 frames); the 50 `sinf` results; cell flags and the "no sorting" order. `TowerVu1Test`: from each probed chain the vertices equal the PATH1 packets' vertices of the capture (`-ee-a` 728 packets / 8 320 writes class, `-ee-late` 26 460 packets with **467 vertices not kicked**); entry 21 non-zero asserts. Empty history: zero towers. RED.
- [ ] **Step 2:** Implement `Towers` from the EE side rules and `TowerVu1` from `verify_opening_vu1_v2.mjs`'s `run`; the opcode-exact subtractions of the bounds (instructions 77 and 142) are modelled with the three-vertex delay. **Step 3:** GREEN. **Commit** `Feat(Scene): Port the opening's towers and their VU1 math`.

### Task 12: Rule extensions — 24-bit depth, 16-bit textures, per-pixel alpha, alpha correction

Approved: controller, 2026-10-04 mandate

**Files:**
- Modify: `src/parity/GsParityRenderer.cpp`, `src/parity/GsParityRenderer.hpp`, `shaders/GsParity.frag`, `shaders/GsParity.vert`, `src/parity/Fixture.cpp`, `tools/parity/make_fixture.mjs`, `tools/parity/oracle.mjs` (only if the oracle loses these formats), `tests/RendererTest.cpp`, `tests/FixtureTest.cpp`, `CMakeLists.txt`
- Create: `tools/ParityTool/budgets/hddosd-110U-opening-full-c100.json` (first gated frame; empty file means no differences)

**Interfaces:**
- Consumes: Task 1's report (which draws carry each feature), `facts/opening.md` section 4 for the draws' states.
- Produces: `describeState` no longer lists `depth format 0x31`, `texture format 0x02`, `per-pixel alpha blending`, `alpha correction` as skip reasons when the renderer implements them; the fixture carries the depth format, the texture's 16-bit pixels (`PSM` 2 with `TEXA`), and the two flags per draw.

Rules (GS, as PCSX2's software renderer applies them): **Z24** (`ZBUF.PSM` 1): the depth value compared and stored is `z & 0xFFFFFF`; the buffer's upper byte is untouched. **PSMCT16 texture**: expand as Task 3. **PABE**: with `ABE` on, a pixel is blended only when the source alpha's bit 7 is set; else the source colour replaces. **FBA**: the written alpha has bit 7 forced on (`FBA` 1 ORs 0x80 into the stored alpha). Each is a flag in the pipeline key, not a new shader.

- [ ] **Step 1: Baseline first:** record, before any change, the result of `ctest -C Debug -R "Parity|Fixture|Renderer"` and the per-pixel output of the 249 unskipped opening passes (a fixture of the unskipped passes of `opening-full` frame 100 through `make_fixture.mjs` into the scratch folder, `ParityTool` observed.json); commit nothing.
- [ ] **Step 2: Tests first, one per feature**, each a synthetic frame in `RendererTest.cpp` compared with a hand-computed expectation and, where the oracle can run, with PCSX2: Z24 (a draw with z 0x1FFFFFF compares as 0xFFFFFF; two draws z 0x00FFFFF0 and 0x01000010 order as the masked values); CT16 (a textured sprite over a 16-bit texture of the four patterns of Task 3 gives `(248,248,248,TA1)` etc.); PABE (a source alpha 0x7F replaces, 0x80 blends); FBA (stored alpha bit 7 set). RED.
- [ ] **Step 3:** Implement renderer, fixture and `describeState` changes. After each feature, re-run the baseline: the 249 passes' output and every clock gate (`ParityClock`, `ParityConfig`, `ParityToClock`, `ParityClockScene`) unchanged, every clock budget file unchanged.
- [ ] **Step 4: First opening pixel gate.** `make_fixture.mjs <opening-full.gs> <fixtures>/hddosd-110U-opening-full-c100/f0 106` (counter 100 = frame index 106; five cubes, logo up, fog, lights). Run `bin/ParityTool.exe <f0> build/parity-report/opening-full-c100 bin/shaders` and take every observed entry through the budget rule (the draw, target, pixel, delta; whether the draw is textured and bilinear; the sample's distance to the texture rectangle's edge); an entry is budgeted only when the draw is bilinear and within one texel of the rectangle's edge. Any other entry is a renderer bug: stop and report it. Write the entries to the budget file; add `add_parity_test(ParityOpening100 opening-full-c100)`.
- [ ] **Step 5: Both ways once, reverted after:** delete one budget entry -> FAIL `unexpected difference`; add `{"delta":1,"scope":"chained","target":"fb0000","x":0,"y":0}` -> FAIL `stale budget entry`.
- [ ] **Step 6 (conditional, Task 1 step 4b):** if the towers' texture is mip-mapped (`TEX1.MMIN` > 1 or `MXL` > 0) add mip-mapped sampling to the rule (level from `K`/`L`, levels from `MIPTBP1/2`) with a synthetic test and a tower fixture from `hddosd-110U-opening2-ee-a.gs`; if it is not, record "no mip" and skip.
- [ ] **Step 7: Commit** (several): `GS(GS): Draw 24-bit depth in the parity rule`, `GS(GS): Draw 16-bit textures in the parity rule`, `GS(GS): Draw per-pixel alpha and alpha correction in the parity rule`, `Feat(Tools): Gate the opening's frame at counter 100`.

### Task 13: Native renderer for the opening

Approved: controller, 2026-10-04 mandate

**Files:**
- Modify: `src/render/NativeRenderer.hpp`, `src/render/NativeRenderer.cpp`, `shaders/Native.frag`, `shaders/Native.vert`, `tests/render/NativeRendererTest.cpp`, `tools/ParityTool/main.cpp` (a `--native-opening` distance report), `CMakeLists.txt`

**Interfaces — Produces:**
```cpp
void NativeRenderer::loadOpeningTextures(const std::filesystem::path& directory);   // References/textures/opening/ (Task 3), RGBA8
// record()/draw() honour Frame::textureSet, the five TargetName values, perPixelAlpha, the two new BlendOps
```
Targets: `Display` (the frame page), `RefractionSource` and `Work` are named by use in the opening's layout (`Extra` is the 1024 x 256 buffer, `Store` the half-width copy), each a colour attachment at the output scale, sampled after a barrier. `AddDestinationAlpha` is `src x dstAlpha + dst` by `VK_BLEND_FACTOR_DST_ALPHA` (targets store alpha / 128, so the factor is the console's `Ad`); `SubtractFixed` is reverse subtract with a constant factor; `perPixelAlpha` is a shader branch (blend only when alpha >= 1.0, else replace).

- [ ] **Step 1: Test first** (headless, 640 x 448): `AddDestinationAlpha` over a target with alpha 0.5 gives `src x 0.5 + dst`; `SubtractFixed` 0x80 over grey gives `dst - src`; `perPixelAlpha` with source alpha 0.99 replaces and 1.0 blends; a sprite sampling `Store` (320 x 224 of content) maps its right half to black; a CT16-derived texture loads from the PNG; the `Extra` target (1024 x 256 logical) is addressable with S up to 0.625; the debug assertion fires for a pass whose written alpha exceeds the target's bound; 0 validation errors. RED.
- [ ] **Step 2:** Implement (targets array grows from 3 to 5, pipelines keyed on the new blends and the PABE flag). The clock's `NativeRendererTest` cases stay green.
- [ ] **Step 3:** Distance report: `ParityTool --native-opening <opening fixture f0>` renders the scene frame (from Task 15's fixture) at 640 x 224 per target and prints the differing share and largest difference against the oracle image; report only, never fails. Run after Task 15; record in that task's report.
- [ ] **Step 4: Commit** `Feat(Renderer): Draw the opening's targets, blends and textures`.

### Task 14: Hand-off and clock entry

Approved: controller, 2026-10-04 mandate

**Files:**
- Create: `src/scene/opening/Handoff.hpp`, `src/scene/opening/Handoff.cpp`, `tests/scene/opening/HandoffTest.cpp`, `src/app/Screen.hpp`, `src/app/ClockScreen.hpp`, `src/app/ClockScreen.cpp`, `tests/scene/opening/ClockEntryTest.cpp`
- Modify: `CMakeLists.txt` (its own block), `tools/scene/make_start.mjs` only if it lacks `--frame 0` for this capture (it takes `--frame`; do not edit when it works)

**Interfaces — Produces:**
```cpp
namespace scene::opening {
struct HandOffInputs { uint32_t snapshot; bool clockForced, hddReady; int32_t hddExec; int32_t cdda; };
HandOff decide(const HandOffInputs&);                       // opening_transition_to_clock 0x0021AEE0, facts/opening.md 8, jtbl_00364F60
constexpr int32_t kFramesToClock = 34;                      // measured (hddosd-110U-opening2-handoff): last scene frame -> the clock's first
}
namespace app {
struct Screen { virtual ~Screen() = default; virtual void step() = 0; virtual scene::Frame frame() = 0; virtual bool done() const = 0; };
class ClockScreen : public Screen { /* Clock<NativeArithmetic> from the T0 state of whole-boot-opening, previous module 1 */ };
}
```
- [ ] **Step 1: Test first** `HandoffTest`: every branch of `facts/opening.md` section 8's table: forced clock without hard disk gives module 2, previous 1, execute -1 (checked before the snapshot table); 0x6A, 0x6B execute 2; 0x6C, 0x6D execute 1; 0x6E 0; 0x6F 5; 0x70 4; 0x71 module 2; 0x72 module 5 with cdda > 0 else 2; 0x73 3; 0x74 module 4; anything else module 2; hard-disk ready with exec 1 gives module 0 and execute 6; against the probed branch captures (`opening3-illegal`, `-disc6a` .. `-disc73`, `-forced`, `-ready-b`, `-ill74-c`, `-ill72-b`) exact. `ClockEntryTest`: `Clock<EeArithmetic>` from frame 0 of `hddosd-110U-whole-boot-opening`'s `scene.json` (T0: mode 2, weight 0, camera offset -100, rings empty, all seven orbs displaced), carried 49 frames equal to the capture's `expect` (camera, rods, orbs, head, after). If the native clock cannot reproduce a piece (orb mode 2 flying in, the scale zeroed on entry, the overlay weight ramp), the test names the first differing piece and frame: port that piece from `References/model/clock_*.mjs` into `Orbs`/`ClockState`/`FrameHead` (files shared with the menus lane: change only what the failing piece needs and report the diff to the controller). RED.
- [ ] **Step 2:** Implement `decide` and the start for `ClockScreen` (`tools/scene/make_start.mjs --frame 0` of the capture writes `resources/clock/start-boot.json`, no Sony data in it: state numbers only; check before committing). **Step 3:** GREEN. Also assert that the clock reaches its menu-input frame (T0 + 129: mode 0, weight 128) in the carried run extended past 49 frames with the model's steady rules (no capture beyond 49: this part is `report only`).
- [ ] **Step 4: Commit** `Feat(Scene): Port the opening's hand-off decision`, `Feat(Scene): Start the clock as the opening leaves it`.

### Task 15: Assembly, carried run, dump gate, pixel gates

Approved: controller, 2026-10-04 mandate

**Files:**
- Create: `src/scene/opening/Opening.hpp`, `src/scene/opening/Opening.cpp`, `tests/scene/opening/OpeningCarriedTest.cpp`, `tests/scene/opening/OpeningGateTest.cpp`, `tools/ParityTool/budgets/hddosd-110U-opening-full-c<counter>.json` (counters 3, 60, 150, 200, 222, 240; 100 exists from Task 12), `tools/ParityTool/budgets/hddosd-110U-opening4-towers-whole-c150.json`, `-c230.json`
- Modify: `CMakeLists.txt`, `tests/scene/SceneGate.*` or the clock's `ClockParityTest.cpp` helpers only to share (`compareFrame` gains coalescing of adjacent passes of equal state; its clock behaviour does not change)

**Interfaces — Produces:**
```cpp
namespace scene::opening {
template <class A> class Opening {
public:
    explicit Opening(const BootOptions&);
    Frame frame(int32_t displayIndex, int32_t field);        // one module frame in the OSD's order (facts/opening.md 2): scissor, towers, ghost, copy, fog, lights, cubes, blur, fade, logo, bars; then steps the timeline
    bool ended() const;                                      // the 246th frame was the last drawn
    int32_t counter() const;
    const std::vector<SoundEvent>& sounds() const;           // events of the last frame (R9)
    const HandOff* handOff() const;                          // set when ended
};
extern template class Opening<EeArithmetic>; extern template class Opening<NativeArithmetic>;
}
```
Order within a frame and conditions (`facts/opening.md` section 2): lights and cubes only while camera z < 73; cubes fewer as the camera passes them (5 until frame index 186, then 4, 3, 2); logo from counter 51 for 120 draws; blur level from z; fade from counter 218 (alpha `(uint)((z - 72) x 128 x 0.03125)`, the rectangle caps at 0x80) and the first frame's alpha 0x80 while counter < 2; bars every frame.

- [ ] **Step 1: Carried-run test first** `OpeningCarriedTest`: `Opening<EeArithmetic>` from counter 1 with the capture's externals (`opening-full`: phase, schedule 0x65 then 0x64, empty history) over 246 frames; per frame the timeline state, fog offsets, lights ring, cubes' angles equal `opening.json`; `ended()` true after 246. RED.
- [ ] **Step 2: Dump gate first** `OpeningGateTest`: for every frame of `opening-full` (counters 1..246), `fromScene(frame, openingLayout(displayIndex))` against `passes.json`: same pass count and order after coalescing adjacent scene passes of equal state, the same state per pass, every vertex equal (positions, z, colour, texture coordinates, alpha). On a difference print frame, pass name, vertex index and both values. RED.
- [ ] **Step 3:** Implement `Opening` from the parts. Expect first failures at the frame head (the scissor and first-frame fade), pass grouping (R4) and strip expansion; fix in the part that owns the value (re-open that lane's file, not the gate).
- [ ] **Step 4: Towers in the gate.** The same two tests on `hddosd-110U-opening4-towers-whole` (history and phase from its probes): the carried run and the dump gate over all frames, with 209 034 PATH1 packets in `opening2-whole`'s class, 467 hidden-vertex strips in the late frames. Expected: every pass equal.
- [ ] **Step 5: Pixel gates.** For counters 3, 60, 150, 200, 222, 240 of `opening-full` (frame index = counter + 6) and counters 150, 230 of `opening4-towers-whole`: `make_fixture.mjs` into `<fixtures>/hddosd-110U-<capture>-c<counter>/f0`, then Task 12 step 4's budget procedure per frame; `add_parity_test(ParityOpening<counter> opening-full-c<counter>)`. Both ways once per budget, reverted after. If Task 1 step 4d found that mid-dump frames have no previous-frame state, the ghost's draw is gated only at counter 3 and the rest are gated with the oracle replay fallback named there.
- [ ] **Step 6:** Run the native distance report (Task 13 step 3) on counters 100, 222 and record its output.
- [ ] **Step 7: Commit** `Feat(Scene): Assemble the opening frame and gate it through the rule`, `Feat(Tools): Gate the opening's frames with exact budgets`.

### Task 16: `--boot`, soak, screenshots, PNG sequence

Approved: controller, 2026-10-04 mandate

**Files:**
- Create: `src/app/OpeningScreen.hpp`, `src/app/OpeningScreen.cpp`, `tests/app/BootChainTest.cpp`
- Modify: `src/app/Main.cpp`, `src/app/DebugPanel.hpp`, `src/app/DebugPanel.cpp`, `CMakeLists.txt`, `README.md` (the Features row and the Planned table: opening done, slices left)

**Interfaces — Produces:** `CrystalClock --boot [--towers none|demo] [--lights-phase N] [--textures dir] [--opening-textures dir]`; the chain `OpeningScreen` (246 frames, sound events dropped) -> black for `kFramesToClock` -> `ClockScreen`; the panel shows the screen name, module counter, stage, camera z, hand-off result, sound events of the last frame, and gains a `Restart opening` button; `--smoke` and `--soak` run the whole chain.

- [ ] **Step 1: Test first** `BootChainTest`: with a mock clock of ticks, the chain yields 246 opening frames, then 34 black frames, then `ClockScreen` frames; `Handoff::decide` result other than module 2 is reported and still ends in the clock; two chains with equal options give equal frame sequences (hash of the passes). RED.
- [ ] **Step 2:** Implement. The app links `OpeningSceneNative` (the opening's scene sources with no `EeArithmetic` source); the default phase is random, `--lights-phase` fixes it.
- [ ] **Step 3: Soak:** `CrystalClock --boot --soak 40` (the chain takes 246 / 59.94 s, about 4.1 s, then 0.6 s black, then the clock), repeated 3 times with `--towers demo`: 0 validation errors, present intervals within 2 ms of the step. Then interactively: resize and minimize during the intro and during the black gap, toggle MSAA and resolution on the panel across the seam: 0 validation errors.
- [ ] **Step 4: Eye check:** screenshots at counters 3, 60, 100, 150, 200, 222, 246 at 640 x 448 and at x4 with MSAA 4x (panel button, `out/screenshots/`), and the PNG sequence of the whole intro (`--screenshots dir` with a frame step of 1: 246 files) for Jean; paths in the report. Not committed.
- [ ] **Step 5:** `git grep -n EeArithmetic -- src/app` prints nothing; the `CrystalClock` link line has no `EeArithmetic` object. **Commit** `Feat(App): Play the opening and hand off to the clock`.

## Gaps listed, with who closes them

| Gap | Closed by |
|---|---|
| The rule's skip reasons (Z24, CT16, PABE, FBA, mip) | Task 1 audit, Task 12 |
| Towers capture of the whole intro | Task 2 |
| Opening textures extracted | Task 3 |
| The clock's entry after the opening in the native clock | Task 14 |
| What the 29 empty frames wait for | the controller dispatches `re-scout` with the question "what does `opening_transition_to_clock`'s caller wait for in emulator frames 374..402 (HDD OSD `hddosd-110U-opening2-handoff`), and is it load-dependent?"; the constant 34 stands until then |
| `rand()` seed at power-on, non-default disc states end to end, hard-disk words, sound ids, fade 'W', PAL, the illegal scene, ROM | not in this slice; each is a line of `facts/README.md` and a later plan |

## Self-review

- **Spec coverage.** Frame map and order: T15. Timeline and stage machine with the hold set: T7. Fog and lights: T8. Cubes: T9. Ghost, copy, blur, fade, logo, bars, scissor: T10. Towers and VU1 with the hidden-vertex rule: T11, T15 step 4. Hand-off and the clock's entry: T14. Rule extensions for the opening's formats: T12. Native renderer additions: T13. `--boot` chain, `Screen` seam, sound hooks, towers option, lights-phase option: T14, T15, T16. Exporter and fixtures: T1 to T4. Textures: T3, T13. Gaps with measurement tasks first: T1 to T4 and the gap table.
- **Placeholder scan.** No "TBD"; each port names its source (verifier function or facts section, R1). The two places that depend on a measurement (the towers' texture size and mip state, the oracle's mid-dump start) name the audit step that settles them and the fallback.
- **Type consistency.** `BootOptions`, `History`, `Matrices`, `TimelineStep`, `SoundEvent`, `HandOff` (T5, T7) are what T8 to T16 use; `Fog::draw`, `Lights::draw`, `Cubes::draw`, `Flat::*`, `Towers::draw` take `Matrices` and append `Pass` (T15 calls them in the OSD's order); `openingLayout` (T5) feeds T15's `fromScene`; `Screen` (T14) is T16's seam.
- **Review Focus.** Each of the nine has its test in the owning task (T15 x2, T11, T12, T8 and T16, T7, T5, T14 and T16, T16, T13).
