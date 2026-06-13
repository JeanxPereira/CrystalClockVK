# CrystalClockVK — Canonical Master Implementation Plan

> Status: APPROVED (lead-architect final, 2026-06-13, rev 2 — adversarial-critique incorporated).
> Supersedes the four competing design drafts AND the rev-1 synthesis.
> Every contested point is resolved against the ground-truth repo docs (`docs/ghidra_analysis/*`,
> `MEMORY.md` GS-dump note), not against any single draft.
> Verify by EVIDENCE (decomp + dump + numeric pixel-diff), never by photo.

---

## 0. How this plan was built (and what rev-1 got wrong)

Four designs were judged (lenses faithful / testable / clean / incremental). They converged on one
spine and the judges flagged the same defects. Rev-1 chose the `[lens=testable]` spine, grafted the
best ideas, and claimed to hard-fix six flaws. An adversarial completeness review then found that
**rev-1 introduced one self-inflicted contradiction, overstated the projection as solved, and left
four evidenced subsystems unscheduled.** This rev-2 fixes all of those against the cited docs.

**Spine retained:** the `[lens=testable]` design — `clock/` pure-logic + `clockvk/` bridge split,
per-phase CTest+pixel-diff gating, math-first ordering.

**Grafted ideas (judge-endorsed):**
- The `constexpr` ordered **pass-list table** keyed by `enum class PassId`, with
  `inputAttachmentRead`/`enabled` flags as the single pass-ordering seam. Style shaders become a
  1-line table edit. (`name` is debug-label only — dispatch keys off `PassId`, never the string. Fixes
  rev-1's stringly-typed footgun.)
- **Draw-lists as plain structs** between scene and renderer — matches the PS2's per-frame GIF-packet
  rebuild and keeps `clock/` and `clockvk/` from leaking into each other.
- **Day-0 runtime/static-constant capture** before any phase, so no week benchmarks against a wrong
  blend/projection/animation state.

**Corrections this rev makes to rev-1 (each cited):**

- **C-A1 [self-inflicted, CRITICAL] — the "+0x00 is scratch" claim is FALSE.** `runtime-trace.md`
  lines 62-64 decode `+0x00 = -13.039 (world X)`, `+0x04 = 14.666 (world Y)`, `+0x08 = 50.271 (world
  Z/radius)`; the projected screen XY lands at `+0x20/+0x24`. Rev-1 called +0x00..+0x08 "animated
  projection scratch," then in §4.2 read the world position FROM those exact bytes — a direct
  self-contradiction. Meanwhile `rod-pipeline.md` §6 line 151 says `draw_crystal_rod` WRITES the
  per-frame `angle` to `+0x04`. **Both are evidenced and they collide at +0x04.** Resolution: `+0x04`
  has two lifetimes — it holds the static ring world-Y AND is overwritten with the per-frame angle
  during `draw_crystal_rod`. This is **W0-Q1**, resolved by a PCSX2 write-watchpoint on `0x375254`
  (= `0x375250+0x04`) stepped through one `draw_crystal_rod`. Until resolved, the `Rod` model stores
  the static world XYZ and the per-frame angle as **separate fields**, and the struct comment states
  the unresolved lifetime — it does NOT assert "scratch."

- **C-A2 + C-B1 — projection is a FITTED HYPOTHESIS, not "ported/FIXED."** `vu0-math-pipeline.md`
  lines 450-453 and 522-526 explicitly WARN that the column order and `tz`/`qz` sign are unconfirmed,
  and the doc's own two pseudocodes (lines 419-447) disagree on `qz`/`tz` form. Rev-1 silently picked a
  textbook Vulkan matrix and labeled it evidence-grade. **Fix:** §4.2 is labeled "hypothesis matrix,
  structure fitted to the oracle"; the oracle does FITTING, not validation, and the plan states the
  free-parameter count so reviewers can see it is not overdetermined. The near-colinear 5-rod problem
  (all at screen Y≈2118, four clustered in X) is fixed by capturing **spread rods at distinct screen Y
  and Z from both groups** (W0-Q2).

- **C-A3 — group-B steady angle step is undocumented.** `rod-pipeline.md` §5 lines 182-191 list
  `832c`/`8330` as group **A** steady only; there is NO group-B steady entry (only `831c/8320/8324/8328`
  transition). **Fix:** W0 resolves the group-B steady step explicitly or documents "B reuses A's step"
  with evidence. Not left implicit.

- **C-A4 — orb blend modes 6/7 are hypotheses, not facts.** `orbs-particles.md` §5 marks halo (mode 7)
  "hypothesis subtractive or src-over" and core (mode 6) "hypothesis additive," and blocker #6 says the
  arg→blend mapping is undecoded. **Fix:** W0 decodes `FUN_00230fe8`'s arg→blend mapping; the pass
  table marks those two entries `HYPOTHESIS` until confirmed; the W4 gate includes a per-stage blend
  check.

- **C-C1 — the transition/opening animation has no phase.** `rod-pipeline.md` §1 shows the rod
  renderer forks on `transition != 0` into a transition variant with its OWN four angle-step globals
  (`831c/8320/8324/8328`), `FUN_002335e8` ×2 group-transform init, and `FUN_00236a80` Y-translate by
  `state[0x1b]*26*t`. Rev-1 reduced this to one `transitionT` float with no render path. **Fix:**
  explicit transition coverage in W3b with `RodTransitionTest`.

- **C-C2 — the clock-face time digits have no render path.** `orbs-particles.md` line 32/59:
  `module_clock_231E48 @ 0x0022e0b8` renders "animated digit labels (date/time)" — the on-screen clock
  readout, a core VISUAL element, distinct from menu text. Rev-1 folded all glyphs into W8 behind the
  menu with "ImGui HUD shows time as fallback." **Fix:** clock-face digits are scheduled as a scene
  element (W8a), separate from menu UI text (W8b). ImGui time is a dev aid, never the 1:1 element.

- **C-C3 — orb count baked to `std::array<Orb,2>` against an explicit "count unknown" blocker.**
  `orbs-particles.md` §7 + blocker #2: the count comes from an un-decompiled fn-table iteration; two
  texture handles ≠ two orbs. **Fix:** orb storage is an init-sized container driven by the W0 fn-table
  decode, not a compile-time `std::array<,2>`.

- **C-C4 — the controller camera-orbit term is missing from the MVP.** `update_camera_angles_input
  @ 0x00231478` → `fGpffff8bc0` rotates the whole scene. If non-zero at the trace instant, the
  projection oracle's MVP is missing a term — a silent correctness hole. **Fix:** W0-Q3 reads
  `fGpffff8bc0` at the capture instant; if non-zero it enters the `Basis`/MVP chain BEFORE the W1
  oracle is fit.

- **C-D2 — 8-bit feedback target will fail late gates for non-correctness reasons.** Subtractive
  framebuffer-feedback refraction + additive glow accumulation through `R8G8B8A8_UNORM` bands and
  clips. **Fix:** main render target is `R16G16B16A16_SFLOAT`; convert to 8-bit only for present and
  for the diff-gate readback (which downsamples to 640×224 8-bit anyway — HDR is parity-free).

- **C-D3 — instancing decided now, not as a W10 afterthought.** ~3936 rod draws × 5 passes with
  per-draw push-constant churn is a data-model choice, not a perf patch. **Fix:** per-rod data lives in
  an SSBO indexed by `gl_InstanceIndex` from W2; `RodPushConst` carries only frame-global data.

- **C-D4 — `DepthMode::Reset` was undefined.** `rod-pipeline.md` §7.2 maps `(0,2)` to depthWrite=true.
  **Fix:** the third enum value is dropped; the pass-3 entry uses `WriteOn`.

- **C-B2 — MAE gates need a measured noise floor and a stated unit.** Gates are per-channel mean
  absolute error on 0-255 RGB, expressed as **"floor + ε,"** where `floor` is measured in W0 by
  diffing two independent correct references. PARTIAL DISCOUNT on the dither half of the critique — see
  §"Discounted critique points" D-1.

- **C-B3 / C-B4 — menu/positional gates must be numeric, not eyeballed.** W0 captures resolved menu
  item screen positions (live memory read after `MenuLayout`) for a `MenuLayoutTest` oracle; W2's
  positional check is the numeric `RodProjectionTest` run headless, with RenderDoc as a debug aid only,
  never a gate.

- **C-D1, C-E1, C-E2 — abstraction trims.** `PassId` enum key (not string); resolution-independence
  demoted from a day-one principle to "keep the code float-clean, realize the payoff in W10"; init-only
  transition scratch (`field140`, `groupTransform`) kept out of the per-frame runtime model unless the
  port needs it.

- **F6 retained — `clock/` Vulkan-free enforced at the CMake link wall**, not by comment.

### Discounted critique points (stated, with one-line reason)

- **D-1 — "ordered GS dither will defeat sub-2 MAE."** DISCOUNTED for the clock: the GS-dump ground
  truth (`MEMORY.md` gs-dump note) shows **DTHE=0 (dither OFF) uniformly across all clock draws**, so
  there is no dither pattern to match. (The general PS2 style uses dither; the clock does not. The
  noise-floor measurement C-B2 still stands — PCSX2 SW rounding/quantization remains.) Decision
  F2-OD is therefore "diff against the dither-off clock reference; do NOT add dither."
- **D-2 — "the F1 fix must call +0x04 only the angle."** DISCOUNTED as stated: the critique is right
  that rev-1's "scratch" label is wrong, but the cleanest model is NOT "angle-only" either — both the
  static world-Y and the per-frame angle are evidenced at +0x04. We model them as two fields and flag
  the lifetime (C-A1), rather than collapsing to one interpretation before the watchpoint resolves it.

---

## 1. Overview

### Goal
A modern, resolution-independent **Vulkan 1.3 / C++23** reimplementation of the PS2 OSDSYS crystal-clock
VISUAL layer, **perceptually 1:1** with the original. Built **procedurally** from the Ghidra decomp
(pure math, the way the PS2 does it). NOT a GS emulator, NOT a GS-dump replay.

### Guiding principles
1. **Logic first, style last.** Rod geometry → projection → animation → orbs → menu → clock digits,
   all flat-rendered and numerically verified, THEN refraction / glow / blur shaders.
2. **Evidence only.** Every ported number cites a Ghidra `name@addr` or a live trace. Unknowns are
   resolved by static disassembly or a numeric fit against a captured oracle — never guessed, never
   eyeballed from a photo. **Where a value is a fit, the plan says so and states the free-parameter
   count.**
3. **Pure logic is Vulkan-free and unit-testable.** The hardest piece (projection) is exercised by a
   CTest oracle before a single draw call exists.
4. **One ordering seam.** All pass ordering, blend, depth, and feedback flags live in one `constexpr`
   table keyed by `PassId`. Adding a style pass is a table edit.
5. **Resolution independence is kept cheap, realized late.** Projection uses float scalars and a
   dynamic viewport from the start (free), but it is NOT advertised as solved before the matrix's
   column order is confirmed (C-A2/C-E1). The payoff is a W10 deliverable, not an early design driver.

### Keep / Drop

**KEEP (modern foundation, already in repo):**
- `src/core/` — `VulkanContext`, `WindowContext` (SDL3), `RenderDocWrapper`.
- `src/renderer/` — `ResourceManager`(+VMA, `downloadImage` readback), `PassRecorder`(Sync2, dynamic
  rendering, `local_read` helpers), `PipelineBuilder`(`setBlendState`, `setFlags`),
  `DescriptorAllocator`/`Writer`, `ShaderLoader`, `SwapchainManager`, `FrameData`(`FrameOverlap=2`),
  `DeletionQueue`, `UIRenderer`(ImGui).
- `src/app/TimeSync` (+`TimeInfo`) — real-time clock query.
- CMake (C++23, FetchContent: SDL3/VMA/vk-bootstrap/GLM; ImGui submodule; glslc glob-compile).
- `tools/pixeldiff/extract_ref.mjs` + `pdiff.mjs` — the numeric gate. App `--dump-rgba` (640×224 FBP0).
- `gs/` swizzle/TEXA unit tests (`gs_swizzle*`, `gs_texa*`) — kept as a **style oracle/reference only**,
  not linked into the render path. Used in W8 to deswizzle the glyph/prism textures.

**DROP (the GS-replay / emulation path — do not carry into the rebuild):**
- `src/gs/SwizzleEngine`, `TextureDecoder`, `VramBuffer`, `GsRegisterState`, `GsCommandStream`,
  `GsConstants`, `GsCrystalMath` (production code; the swizzle/TEXA tests stay as oracle).
- `src/gsvk/GsBlendTranslator`, `GsDrawRecipe` (the blend constants re-encode in
  `clockvk/BlendStates.hpp`; the translator is unnecessary).
- `src/app/GsScene`, `src/app/GsRenderer`.
- `tools/gsdump/`, `tools/vramdump/` (kept on disk as a reference oracle, not built into the app).
- CTest entries `gsvk_blend`, `gsvk_recipe` (mapping is re-tested by the pixel-diff gate).

---

## 2. Target Architecture

### Module / layer tree

```
src/
├── core/                              [KEEP AS-IS]
│   ├── VulkanContext.{hpp,cpp}        device, VMA, queues, debug names, feature queries
│   ├── WindowContext.{hpp,cpp}        SDL3 window + event pump
│   └── RenderDocWrapper.{hpp,cpp}     optional capture hook
│
├── renderer/                          [KEEP AS-IS]
│   ├── SwapchainManager.{hpp,cpp}
│   ├── ResourceManager.{hpp,cpp}      VMA alloc + downloadImage (pixel-diff gate entry)
│   ├── PassRecorder.{hpp,cpp}         Sync2, dynamic rendering, insertLocalReadBarrier
│   ├── PipelineBuilder.{hpp,cpp}      setBlendState, setFlags (feedback-loop bit)
│   ├── DescriptorAllocator.{hpp,cpp}
│   ├── ShaderLoader.{hpp,cpp}
│   ├── FrameData.{hpp,cpp}
│   ├── DeletionQueue.hpp
│   └── UIRenderer.{hpp,cpp}           ImGui overlay + debug HUD
│
├── clock/                             [NEW — pure PS2 logic, ZERO Vulkan, unit-testable]
│   ├── Types.hpp                      POD: Rod, RodGroup, Orb, TrailEntry, TrailRing,
│   │                                  ClockState, MenuStateData, DisplayTime, VisualConfig,
│   │                                  AnimState, Scene, RodInstance, OrbDraw, MenuDraw,
│   │                                  GlyphDraw, DrawLists
│   ├── math/
│   │   ├── FixedPoint.hpp             constexpr ftoi4/vitof4/ftoi12 (oracle scratch only)
│   │   ├── Basis.{hpp,cpp}            rotation_build: 2× cross-product orthonormal basis
│   │   └── Projection.{hpp,cpp}       projection_build (HYPOTHESIS matrix, GS-native→NDC)
│   ├── rod/
│   │   ├── RodField.{hpp,cpp}         ring layout, 2 groups, stride 0x160, face partition
│   │   ├── RodAnimation.{hpp,cpp}     per-pass angle step (steady) + transition variant
│   │   ├── GlowAlpha.{hpp,cpp}        per-rod alpha decay (FUN_00242ac8)
│   │   └── RodProjection.{hpp,cpp}    Basis × Projection → screen + gs 12.4 (oracle)
│   ├── orb/
│   │   ├── OrbField.{hpp,cpp}         init-sized slot dispatch (fn-table 0x239440/0x238d60)
│   │   ├── OrbOrbit.{hpp,cpp}         angular velocity / radius / tilt per slot
│   │   └── TrailBuffer.{hpp,cpp}      50×32B ring + color attenuation (FUN_002261a0)
│   ├── menu/
│   │   ├── MenuState.{hpp,cpp}        4-state dispatcher, frame-count fade, scroll angle
│   │   └── MenuLayout.{hpp,cpp}       DAT_00274c00 record decode → per-item pixel XY
│   ├── time/
│   │   ├── TimeDisplay.{hpp,cpp}      BCD decode, timezone + DST + 12/24h, date format
│   │   └── ClockDigits.{hpp,cpp}      scene-space digit layout (module_clock_231E48) — C-C2
│   ├── camera/
│   │   └── CameraOrbit.{hpp,cpp}      fGpffff8bc0 controller-driven scene orbit — C-C4
│   ├── Config.{hpp,cpp}               two-word bit-field decode → VisualConfig (visual fields only)
│   └── SceneUpdater.{hpp,cpp}         tick: TimeInfo→ClockState→camera→rods→orbs→digits→menu
│
├── clockvk/                           [NEW — the ONLY place clock logic meets Vulkan]
│   ├── BlendStates.hpp                constexpr VkPipelineColorBlendAttachmentState ×3
│   ├── PassList.hpp                   PassId enum + RenderPass struct + ordered constexpr array
│   ├── Pipelines.{hpp,cpp}            one VkPipeline per (blend×depth×inputAttach) combo
│   ├── RodRenderer.{hpp,cpp}          rod passes: instanced draw, SSBO per-rod data, face/group filter
│   ├── OrbRenderer.{hpp,cpp}          4 orb stages: trail / halo / core / trail-2
│   ├── DigitRenderer.{hpp,cpp}        clock-face date/time glyph quads (C-C2)
│   ├── MenuRenderer.{hpp,cpp}         UI overlay, fade alpha, menu glyph quads
│   └── ClockRenderer.{hpp,cpp}        top-level: owns Pipelines + HDR target, iterates PassList
│
└── app/
    ├── main.cpp                       frame loop (structure kept; GsRenderer→ClockRenderer)
    ├── TimeSync.{hpp,cpp}             [KEEP] system clock → TimeInfo
    └── FrameParams.hpp               [EXTEND] add const Scene* / DrawLists* / FrameInput

shaders/
├── rod_flat.vert / rod_flat.frag                W2: instanced MVP, white × glowAlpha
├── rod_prism.vert / rod_prism.frag              W6: additive prism surface (flat→textured)
├── orb_trail.vert / orb_trail.frag              W4: billboard, per-entry attenuated alpha
├── orb_halo.vert / orb_halo.frag                W4: head halo + core rects
├── digit.vert / digit.frag                      W8a: clock-face date/time glyphs
├── menu_overlay.vert / menu_overlay.frag        W5: UI fade + menu glyph atlas
├── rod_refract.frag                             W7: subpassLoad feedback + bump offset
└── blur_h.frag / blur_v.frag                    W9: optional separable Gaussian

tests/
├── math/   FixedPointTest, BasisTest, ProjectionOracleTest
├── rod/    RodFieldTest, RodAnimationTest, RodTransitionTest, GlowAlphaTest, RodProjectionTest
├── orb/    TrailBufferTest, OrbOrbitTest, OrbCountTest
├── menu/   MenuFadeTest, MenuLayoutTest
├── time/   TimeDisplayTest, ClockDigitsTest
├── camera/ CameraOrbitTest
└── [KEEP]  SwizzleRoundTripTest, SwizzleAddress32Test, TexaExpandTest, DeswizzleTexa24Test

tools/pixeldiff/                       [KEEP unchanged] extract_ref.mjs, pdiff.mjs
```

### Strict-layer boundaries (enforced at the CMake link wall)

| Layer | Depends on | Forbidden |
|---|---|---|
| `core/` | vk-bootstrap, SDL3, VMA | app/clock logic |
| `renderer/` | `core/`, Vulkan | PS2/app logic |
| `clock/` | `glm::glm` ONLY | **any Vulkan symbol** (no `#include <vulkan/vulkan.h>`) |
| `clockvk/` | `clock/`, `renderer/`, Vulkan | `app/` |
| `app/` | `clockvk/`, `renderer/`, `core/`, `clock/` | — |

`target_link_libraries(clock PRIVATE glm::glm)` — deliberately **no** `Vulkan::Vulkan`. A stray Vulkan
include in `clock/` fails to compile. This is the F6 fix: the boundary is a build error, not a comment.

### How it sits on the kept foundation
- `ClockRenderer::record(PassRecorder&, const DrawLists&, AllocatedImage& hdrTarget)` replaces the
  `if (gsRenderer.ready())` branch in `main.cpp`. The rest of the loop (FrameData, swapchain
  acquire/present, Sync2 submit, `--dump-rgba` exit, ImGui overlay) is structurally unchanged.
- **Render target is `R16G16B16A16_SFLOAT`** (C-D2), usage
  `COLOR_ATTACHMENT | TRANSFER_SRC | INPUT_ATTACHMENT | SAMPLED`. A final tonemap/convert pass writes
  the 8-bit swapchain image and the `--dump-rgba` readback. The diff gate already downsamples to
  640×224 8-bit, so HDR adds zero parity cost and prevents banding/clipping in the feedback + additive
  passes.
- `ClockRenderer::readbackDisplay()` calls `ResourceManager::downloadImage(...)` on the converted 8-bit
  image — same signature as the dropped `GsRenderer::readbackDisplay`, so the `--dump-rgba` arg block
  is reused verbatim.

---

## 3. Data Model

All POD, all in `clock/Types.hpp`. `std::array` for fixed counts; orb storage is init-sized (C-C3).

```cpp
// --- C-A1 FIX: static ring world position and per-frame angle are SEPARATE fields. ---
// The lifetime collision at rod+0x04 (static world-Y vs per-frame angle write) is W0-Q1,
// resolved by a PCSX2 write-watchpoint. We do NOT assert "scratch".
struct Rod {
    glm::vec3 worldPos;     // +0x00..+0x08 static ring input: X,Y,Z per runtime-trace.md L62-64
    float     angle;        // +0x04 per-frame ring angle; draw_crystal_rod WRITES *(rod+4)=in_f0
                            //        (rod-pipeline.md §6 L151) — shares the word with worldPos.y;
                            //        lifetime order = W0-Q1 (watchpoint on 0x375254).
    glm::vec3 scale;        // +0x10..+0x18 (live trace: 1.0,1.0,0 — sceVu0MulVector target)
    glm::vec4 screen;       // +0x20..+0x2c projected X,Y,Z,W (computed each frame; oracle target)
    glm::uvec3 gs;          // +0x30..+0x38 12.4 fixed (oracle/debug only, never uploaded)
    float     perspW;       // +0x40 1/W
    float     glowAlpha;    // +0x60 [0,128]; divided by 128 before GPU
    bool      isFront;      // +0x150 == 0
};

// C-C1/C-E2: group-level fields (+0x130 group_transform, +0x140) are TRANSITION-INIT scratch.
// They are NOT in the per-frame runtime model; the transition path (W3b) carries them locally.
struct RodGroup {
    std::array<Rod, 32> rods;      // upper bound; live count in `count`
    int  count;
    int  skip;                     // group A skips i>7; group B skips i-8>1 (both carried)
};

struct TrailEntry { glm::vec3 gsPos; glm::vec3 colorBase; };   // 32B logical; color 0..255 raw
struct TrailRing  { std::array<TrailEntry,50> e; int head; int count; bool wrapped; };

struct Orb {
    glm::vec3 worldPos;
    float     orbitAngle;          // wraps at 1048576.0 (fGpffff8b88 path)
    float     brightness;          // unaff_f22 (halo ×30, core ×4.5)
    TrailRing trail;
    uint32_t  texCore, texHalo;    // GS TEX0/TEX1 handles (1×1 white until W8)
};

struct MenuStateData {
    int   state;                   // 0=none 1=CD 2=settings 3=confirm  (menu_state @ 0x2c8ce0)
    int   itemIndex, animFrame;
    float scrollAngle;
    float fadeAlpha;               // [0,1] precomputed; ramp = counter*0x7F/total
};

struct DisplayTime {               // BCD-decoded; see TimeDisplay
    int seconds, minutes, hours, day, month, year;
    bool is12h, isDst; int dateFormat;   // 0=YMD 1=MDY 2=DMY
};

struct VisualConfig {              // widescreen selects projection halfWidth (NOT aspect)
    bool widescreen; int timezoneMin; bool dst; bool timeFormat12h; int dateFormat; int language;
};

struct AnimState {
    float transitionT;             // 0→1 opening transition (drives the W3b transition path)
    float orbitAngle;              // fGpffff8b88, wraps 1048576
    float cameraAngle;             // fGpffff8bc0, controller scene orbit (C-C4)
    float phase;                   // wraps at 1000.0 → FUN_00231e60 event
    float dt;                      // clamped smoothed delta (calc_animation_delta_time)
};

struct ClockState {                // the int* clockState passed to ui_render_3d_objects
    int   rodCountRaw;             // [0]  × transition → base angle
    int   rodCount;                // [1]  steady loop bound
    float intensity;               // [0x1b] scale/intensity (also *26*t transition translate)
    glm::mat4 transformBlock;      // [0x28] = (float*)(clockState+0xa0)  (cast, not member)
    float angleOffsetA, angleOffsetB; // [0x2c]/[0x2d] pass-3 group offsets
};

struct Scene {
    std::array<RodGroup,2>  groups;     // A + B
    std::vector<Orb>        orbs;       // C-C3: init-sized from W0 fn-table decode, NOT array<,2>
    MenuStateData menu; DisplayTime time; VisualConfig config; AnimState anim; ClockState cs;
};

// --- plain-struct boundary clock/ → clockvk/ ---
enum class BlendMode { Disabled, SrcOver, Additive, Subtractive };
enum class DepthMode { WriteOn, WriteOff };          // C-D4: "Reset" dropped; (0,2)=WriteOn

struct RodInstance { glm::mat4 mvp; float glowAlpha; float bumpScale; };  // SSBO entry (C-D3)
struct OrbDraw   { glm::vec3 pos; float size; glm::vec4 color; };
struct GlyphDraw { glm::vec4 rect; glm::vec4 color; uint32_t glyph; };    // clock digits + menu text
struct MenuDraw  { glm::vec4 rect; glm::vec4 color; uint32_t glyph; };
struct DrawLists {
    std::array<std::vector<RodInstance>,5> rodPasses;   // 1,1b,1c,2,3 (instanced)
    std::array<std::vector<OrbDraw>,4>     orbStages;   // trail, halo, core, trail-2
    std::vector<GlyphDraw> digits;                       // clock-face date/time (C-C2)
    std::vector<MenuDraw>  menu;
};
```

### Per-frame data flow

```
TimeSync → TimeInfo
      │
SceneUpdater::tick(TimeInfo, FrameInput) → Scene
   1. TimeDisplay::update      → DisplayTime (tz + DST + format)
   2. AnimState::tick(dt)      → orbitAngle (wrap 1048576), phase (wrap 1000)
   3. CameraOrbit::step        → cameraAngle from FrameInput (C-C4)
   4. RodAnimation::step       → per-pass angle (steady) OR transition variant if transitionT>0
   5. RodProjection::project   → Camera × Basis × Projection × world → Rod.screen + Rod.gs (per rod)
   6. GlowAlpha::compute       → Rod.glowAlpha (per rod, front/back/active divisors)
   7. OrbField::step           → orbit pos + TrailBuffer push + attenuation (over init-sized slots)
   8. ClockDigits::layout      → digit glyph quads in scene space (C-C2)
   9. MenuState::step          → fadeAlpha, scrollAngle
      │
SceneUpdater::buildDrawLists(Scene) → DrawLists   (pure; no Vulkan)
      │  (boundary: plain structs only)
ClockRenderer::record(PassRecorder, DrawLists, hdrTarget)
      → for each entry in kPassList (if enabled):
           bind Pipelines.get(blend,depth,inputAttach); bind per-rod SSBO; draw the stage's list
      → tonemap/convert HDR → 8-bit swapchain image
      │
present → [optional --dump-rgba → ResourceManager::downloadImage(8-bit) → pdiff.mjs]
```

Everything up to `ClockRenderer::record` is pure C++/GLM. `Scene` and `DrawLists` are the only types
crossing the boundary. No persistent GPU state lives in `clock/`.

---

## 4. Math & RE Mapping

### 4.1 rotation_build — `clock/math/Basis.cpp`
Port of `rotation_build @ 0x002732d8` (`vu0-math-pipeline.md`; two VOPMSUB cross products @ `0x273318`
+ `0x273370`). NOT Euler, NOT Rodrigues.

```cpp
glm::mat4 Basis::build(glm::vec3 forward, glm::vec3 up) {
    glm::vec3 fwd   = glm::normalize(forward);
    glm::vec3 right = glm::normalize(glm::cross(fwd, up));   // VOPMSUB @ 0x273318
    glm::vec3 upO   = glm::cross(right, fwd);                // VOPMSUB @ 0x273370
    return glm::mat4(glm::vec4(right,0), glm::vec4(upO,0), glm::vec4(fwd,0), glm::vec4(0,0,0,1));
}
// Overflow guard: NaN/denormal input → mat4(0) (mirrors bc1t→memclr @ 0x00273300).
```
**Inputs** `forward`/`up` are built caller-side from `sin/cos` of two angles (`angleA` from `s3` and
`rod.angle` from `s2+0x04`) written into the trig temp buffer `0x29BCF0` before the `jal`. The buffer
layout is **W0-Q4** (`draw_crystal_rod` prologue `s5`-relative writes, ~10-20 instrs before
`jal 0x002732d8`; `vu0-math-pipeline.md` blocker #2).

**Unit test (`BasisTest`):** `right·fwd≈0`, `upO·right≈0`, `cross(right,upO)≈fwd` (right-handed);
parallel inputs → mat4(0).

### 4.2 projection_build — `clock/math/Projection.cpp`  **(HYPOTHESIS matrix — C-A2)**
Port of `projection_build @ 0x002730a8` (NOT `sceVu0ViewScreenMatrix`). A **custom GS-native**
projection that embeds the viewport transform.

> **This matrix is a HYPOTHESIS whose STRUCTURE is fitted to the oracle, not a verified port.**
> `vu0-math-pipeline.md` lines 450-453 and 522-526 explicitly warn that the column order and the
> `tz`/`qz` sign are unconfirmed from static decode (92 `.xy`-paired SIMD instructions), and the doc's
> own two pseudocodes (lines 419-447) disagree on the `qz`/`tz` form. The oracle below performs
> **fitting**, not validation.

Evidence-grade call-site args (`vu0-math-pipeline.md` §"Confirmed constants"):
`far=2048.0`, `aspect=1.0` (hardcoded — widescreen via `halfWidth`, NOT aspect), `scale=65536.0`,
widescreen branch on `iGpffff8d18` @ `0x00232e68` selecting `halfWidth = gp[-0x7b80]` (4:3) or
`gp[-0x7b7c]` (16:9). Unknown scalars: `fov = gp[-0x73d8]`, `near = gp[-0x7b78]`.

**Free parameters the oracle fits (stated so reviewers see it is not overdetermined — C-B1):**
`fov`, `halfWidth(4:3)`, `near`, plus **a discrete column-order choice** from a small enumerated set
(the `tz`/`qz` placement). `far`, `aspect`, `scale` are fixed by evidence. That is **3 continuous + 1
discrete** unknowns — which is exactly why the oracle needs spread rods (below), not one ring.

**Vulkan adaptation (explicit GS-space vs NDC split):**
`Projection::build` returns a **Vulkan NDC** matrix (`GLM_FORCE_DEPTH_ZERO_TO_ONE`, depth [0,1]). The
`×65536` scale and the GS `+1024` centre offset are GS-integer-encode artifacts and are **dropped**.
The oracle converts NDC→GS only for comparison; the matrix itself never emits GS space.

```cpp
// HYPOTHESIS form; column order is one of an enumerated set, picked by the W1 oracle fit (C-A2/B1).
// scale/×65536 and GS +1024 offset are NOT in this matrix.
glm::mat4 Projection::build(float fovRadians, float halfWidth, float near) {
    constexpr float far = 2048.0f;
    float f  = 1.0f / std::tan(fovRadians * 0.5f);
    float sx = f * halfWidth;       // aspect == 1.0 → sy == sx
    float qz = far / (far - near);  // depth-zero-to-one form (candidate; sign per fit)
    float tz = -(far * near) / (far - near);
    return glm::mat4(
        glm::vec4(sx, 0,  0, 0),
        glm::vec4(0,  sx, 0, 0),
        glm::vec4(0,  0,  qz, 1),
        glm::vec4(0,  0,  tz, 0));
}
// Oracle conversion (test only):  gs_xy = (ndc_xy * 0.5 + 0.5) * 2048.0  ... centre 1024.
```

**Resolving FOV/near/halfWidth (live REGISTER read is DEAD — `runtime-trace.md` L5-8 gp=0):**
1. `near`, `halfWidth(4:3 & 16:9)` come from the **static constant loads** in `draw_crystal_rod
   @ 0x00232e38` (`lwc1 f18,-0x7b78(gp)` near @ `0x00232ea4`; the 4:3/16:9 branch @ `0x00232e68`).
   Disassemble these (ghidra-mcp `decompile_function`, `program="OSDSYS.elf"`), follow each `gp[-off]`
   to the static initializer that seeds the global.
2. `fov` (`gp[-0x73d8]`): same — read the static initializer. If it is runtime-computed, fall back to a
   **save-state MEMORY read** (memory reads ARE valid; only register reads are zeroed) at the absolute
   address derived from a save-state `gp`.
3. Only after both are known by static/memory evidence does the oracle *fit-and-validate* them.

**Oracle spread requirement (C-B1 — the near-colinear problem):** the five live rods all sit at screen
Y≈2118 with four clustered in X — five near-colinear points do NOT independently constrain a 4×4 with
3+1 unknowns; a transposed-but-coincidentally-fitting matrix could pass. **W0-Q2** captures additional
rods from **both** groups (`0x375250` group A, `0x377e50` group B) at genuinely **distinct screen Y and
distinct Z** via PCSX2 memory reads, so the fit is constrained in all three axes. If sufficient spread
cannot be captured, the oracle is declared a **regression test, not a correctness proof**, and the plan
says so in the W1 gate.

**Projection oracle (`ProjectionOracleTest`) — the W1 hard gate:**
```
for each captured rod (≥5, spanning distinct screen-X, screen-Y, and Z, both groups):
    mvp = Projection::build(fov, halfWidth, near) * Basis::build(fwd_i, up_i) * Camera(cameraAngle)
    p   = mvp * vec4(world_i, 1)
    gs  = ndcToGs(p.xy / p.w)
    REQUIRE(length(gs - liveScreen_i) < 1.0)            // < 1 GS unit = sub-pixel
REQUIRE depth ordering + ratio of the distinct-Z rods matches live screen.z (rod 0 +0x28 = -255.0)
REQUIRE the chosen column order is the UNIQUE one passing all rods (transpose-guard; else fail + flag)
```
`cameraAngle` is included only if W0-Q3 finds `fGpffff8bc0 != 0` at the capture instant; otherwise it
is identity and the term is documented as "confirmed zero at capture."

### 4.3 sceVu0MulMatrix — combined = projection × rotation
Port of `sceVu0MulMatrix @ 0x002638a0` (alias `0x002738a0`). Column-major, identical to GLM
`operator*`. `combined = Projection::build(...) * Basis::build(...)`. With camera:
`combined = Projection * Basis * Camera`.

### 4.4 12.4 fixed-point — `clock/math/FixedPoint.hpp` (oracle scratch ONLY)
```cpp
constexpr int32_t ftoi4 (float v) { return int32_t(v * 16.0f); }    // VFTOI4 trunc(×16)
constexpr float   vitof4(int32_t v){ return float(v) / 16.0f; }     // VITOF4 ×(1/16)
constexpr int32_t ftoi12(float v) { return int32_t(v * 4096.0f); }  // VFTOI12 (intermediate)
```
**Not in the GPU path.** The vertex shader receives float world coords; the projection matrix emits
float NDC. `Rod.gs` is populated solely so `RodProjectionTest` can assert `gs.x == 0x77b3 ± 1` and
`gs.y == 0x8463 ± 1` (live trace). `FixedPointTest`: `ftoi4(vitof4(0x77b3)) == 0x77b3`,
`vitof4(0x77b3) ≈ 1916.19` (note: live `+0x20` float screen X = 1915.20; the 12.4 value at `+0x30` is
`0x77b3/16 = 1916.19` — the ~1px delta is the float-vs-fixed rounding the trace itself records, and the
oracle tolerance accounts for it).

### 4.5 Rod ring layout / angle step — `clock/rod/RodField.cpp` + `RodAnimation.cpp`
- Ring positions derived from the captured X-values; group A skips first 8 (`i>7`), group B skips first
  2 (`i-8>1`) — **both distinct skip counts carried**.
- **Steady-state** per-rod angle: pass 2 `angle = fVar18 + i*fGpffff832c`; pass 3 `angle += i*fGpffff8330`
  shifted by `clockState[0x2c]/[0x2d]`. `rod-pipeline.md` §5 lists `832c`/`8330` as group **A** steady
  only.
- **C-A3 — group-B steady angle step is undocumented.** W0 resolves it: either decompile the group-B
  steady pass-2/pass-3 loop to find its step global, OR confirm "group B reuses A's `832c/8330`" with
  evidence. The value is committed as a named `constexpr` with its source; it is NOT left implicit.

### 4.5b Transition (opening) variant — `clock/rod/RodAnimation.cpp` (transition path) — **C-C1**
`rod-pipeline.md` §1: when `transition != 0`, the renderer takes a DIFFERENT branch:
- `FUN_002335e8` ×2 per-group transform init (groups A @ `0x375250`, B @ `0x377e50`).
- `FUN_00236a80(0, state[0x1b]*26.0*t, 0)` — a Y-translate that scales with `t` (matrix-stack push).
- Its OWN four angle-step globals: `831c` (A pass 2), `8320` (B pass 2), `8324` (A pass 3),
  `8328` (B pass 3) — distinct from the steady `832c`/`8330`.
This is a whole render mode. It is scheduled in **W3b** with `RodTransitionTest` asserting the four
transition steps, the `*26*t` Y-translate, and the group-transform init. The init-only group fields
(`+0x130`, `+0x140`) are handled locally in this path, not in the per-frame `RodGroup` (C-E2).

### 4.6 Glow alpha decay — `clock/rod/GlowAlpha.cpp`
Port of `FUN_00242ac8` (real target of thunk `FUN_00232da0`). Front rods:
`factor = intensity*(target−pos)`, `alpha = factor/13.0` (inner ring `/6.0`); back rods `/10.0`
(inner `/5.0`); active-rod override `alpha *= (target − iGpffff8b4c)/40.0`. Written to `Rod.glowAlpha`.
**`GlowAlphaTest`:** assert the exact divisors 13/6/10/5 for front-outer/front-inner/back-outer/
back-inner at known position indices.

### 4.7 Orbit motion — `clock/orb/OrbOrbit.cpp` + orb count — `clock/orb/OrbField.cpp`
fn-table entries `0x00239440` (slot 0) and `0x00238d60` (slot 1) — confirmed live pointers
(`runtime-trace.md` orbit fn-table @ `0x0029b3c0`). **W0** statically decompiles both (ghidra-mcp,
`program="OSDSYS.elf"`) → `{angular_velocity, radius, tilt, phase}` per slot; if LUT-driven, embed the
LUT entries as `constexpr float[]` from ELF memory. Integration:
`orbitAngle = fmod(orbitAngle + ω·dt, 1048576.0)`.
**Orb count (C-C3):** the count is an explicit blocker (`orbs-particles.md` §7, blocker #2) — it comes
from the fn-table iteration, NOT from "two texture handles." W0 decodes the iteration count; `Scene.orbs`
is `std::vector` sized at init. `OrbCountTest` asserts the decoded count; `OrbOrbitTest` asserts
period-return AFTER the period is committed from the decode (no circular fit).

### 4.8 Trail color attenuation — `clock/orb/TrailBuffer.cpp`
Port of `FUN_002261a0`: `alpha = 128 − floor(i·50/(count−1))·3`; Blue linear, Red/Green quadratic
(`×alpha²`), GS pack with ADC bit `0x3f8000000000000`. GS encode `gs = (xy+2048)·16`, `z·16`.
`TrailBufferTest`: wrap-at-50, `i=0→α128`, `i=49→α≈25`.

### 4.9 Clock-face digits — `clock/time/ClockDigits.cpp` — **C-C2**
Port of `module_clock_231E48 @ 0x0022e0b8` ("animated digit labels (date/time)" via
`browser_str_related` + `FUN_00247438`). This is the on-screen clock readout, a SCENE element distinct
from menu text. `ClockDigits::layout(DisplayTime) → std::vector<GlyphDraw>` in scene space.
`ClockDigitsTest`: digit glyph positions for a fixed `DisplayTime` match the decoded layout constants.

### 4.10 Camera orbit — `clock/camera/CameraOrbit.cpp` — **C-C4**
Port of `update_camera_angles_input @ 0x00231478`: controller L/R → `fGpffff8bc0` (`+= fGpffff8478`
or `fGpffff8474`, ~8-9/frame). `CameraOrbit::step(FrameInput) → cameraAngle`; feeds the MVP (§4.2/4.3).
`CameraOrbitTest`: L/R input over N frames produces the decoded per-frame increment.

---

## 5. Render Strategy

### 5.1 Flat-first
W2–W5 draw every rod/orb/menu quad flat-shaded: the vertex shader applies the per-instance MVP;
the fragment outputs white × `glowAlpha/128` (rods) or attenuated vertex color (orbs). This proves
projection, blend accumulation, and pass ordering **before** any style shader exists. The refraction
pass renders with the subtractive blend but a flat color (wrong look, correct blend + ordering).

```glsl
// rod_flat.frag — matches live vertex color RGB=255, A=128
layout(location=0) in float vGlowAlpha;     // from per-instance SSBO, already /128
layout(location=0) out vec4 o;
void main() { o = vec4(1.0, 1.0, 1.0, vGlowAlpha); }
```

### 5.2 The single ordering seam — `clockvk/PassList.hpp`
```cpp
enum class PassId {                 // C-D1: dispatch keys off this enum, NEVER a string
    RodGlowFront, RodGlowBack0, RodGlowBackFF, RodPrism, RodRefract,
    OrbTrail, OrbHalo, OrbCore, OrbTrail2, Digits, MenuOverlay, TemporalBlend, PostBlur
};

struct RenderPass {
    PassId           id;              // dispatch key
    std::string_view name;           // DEBUG / RenderDoc label ONLY (string literal, static lifetime)
    BlendMode        blend;
    DepthMode        depth;
    bool             inputAttachmentRead;   // refraction subpassLoad
    bool             enabled;               // optional passes default off
};

// Compile-time. The ONLY place pass ordering, blend, depth, feedback live.
// C-A4: orb halo/core blends are HYPOTHESIS until W0 decodes FUN_00230fe8's arg→blend map.
// F2: 4 orb stages (trail, halo, core, trail-2). C-C2: Digits is a scene pass before menu.
inline constexpr std::array<RenderPass, 13> kPassList = {{
  { PassId::RodGlowFront,  "rod_glow_front",  BlendMode::SrcOver,     DepthMode::WriteOn,  false, true  },
  { PassId::RodGlowBack0,  "rod_glow_back0",  BlendMode::SrcOver,     DepthMode::WriteOff, false, true  },
  { PassId::RodGlowBackFF, "rod_glow_backFF", BlendMode::SrcOver,     DepthMode::WriteOff, false, true  },
  { PassId::RodPrism,      "rod_prism",       BlendMode::Additive,    DepthMode::WriteOn,  false, true  },
  { PassId::RodRefract,    "rod_refract",     BlendMode::Subtractive, DepthMode::WriteOn,  false, true  }, // inputAttach→true W7
  { PassId::OrbTrail,      "orb_trail",       BlendMode::Additive,    DepthMode::WriteOff, false, true  },
  { PassId::OrbHalo,       "orb_halo",        BlendMode::Subtractive, DepthMode::WriteOff, false, true  }, // HYPOTHESIS mode 7 — confirm W0
  { PassId::OrbCore,       "orb_core",        BlendMode::Additive,    DepthMode::WriteOff, false, true  }, // HYPOTHESIS mode 6 — confirm W0
  { PassId::OrbTrail2,     "orb_trail2",      BlendMode::Additive,    DepthMode::WriteOff, false, true  },
  { PassId::Digits,        "digits",          BlendMode::SrcOver,     DepthMode::WriteOff, false, true  }, // C-C2 scene readout
  { PassId::MenuOverlay,   "menu_overlay",    BlendMode::SrcOver,     DepthMode::WriteOff, false, true  },
  { PassId::TemporalBlend, "temporal_blend",  BlendMode::SrcOver,     DepthMode::WriteOff, true,  false }, // W9
  { PassId::PostBlur,      "post_blur",       BlendMode::Disabled,    DepthMode::WriteOff, false, false }, // W9
}};
```
Enabling refraction in W7 = flip `inputAttachmentRead` on `RodRefract` + swap its shader. Enabling blur
in W9 = flip `enabled`. No structural change anywhere else. Patent ordering (FIG 16-17): rods
(refraction boundary) → orb spots (after-image boundary) → digits → blur (post) → menu.

### 5.3 Blend / material / depth abstraction — `clockvk/BlendStates.hpp` + `Pipelines.cpp`
GS equation `(A−B)·C/128 + D`, alpha range **0–128** (128 = opaque; CPU pre-divides by 128). The
three confirmed GS clock blends (`MEMORY.md` gs-dump note, evidenced from the dump):
```cpp
constexpr auto kBlendSrcOver     = makeBlend(SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ADD);          // (Cs-Cd)As/128+Cd
constexpr auto kBlendAdditive    = makeBlend(SRC_ALPHA, ONE,                 ADD);          // (Cs-0)As/128+Cd
constexpr auto kBlendSubtractive = makeBlend(SRC_ALPHA, ONE,          REVERSE_SUBTRACT);    // (0-Cs)As/128+Cd
```
The subtractive form `(0-Cs)*As/128+Cd` is confirmed by the GS dump (1176 subtractive draws); W0 also
decodes the `0x2973c0` ALPHA qword as an independent check. `Pipelines` builds one `VkPipeline` per
`(BlendMode × DepthMode × inputAttach)` combo at init via `PipelineBuilder::setBlendState(...)` +
`setDepthTest(...)`. Flat-first: ~4 combos. Shaders swap (flat→prism→refract) without changing
blend/depth state. **COLCLAMP=1** (dump) → clamp at the tonemap; **DTHE=0** (dump) → no dither (D-1).

Per-rod data is an **SSBO indexed by `gl_InstanceIndex`** (C-D3):
```cpp
struct RodInstanceGPU { glm::mat4 mvp; float glowAlpha; float bumpScale; float pad[2]; };
```
Push constant carries only frame-global data (e.g. `bumpScale` default, viewport). One
`vkCmdDraw(4, instanceCount, ...)` per pass replaces ~3936 individual draws.

### 5.4 Render-to-texture / feedback (HDR, future-proofed, zero-cost until used)
- Main target `R16G16B16A16_SFLOAT` (C-D2) allocated with `INPUT_ATTACHMENT` from day one — bound as
  both color out and input in one dynamic-rendering scope in W7 via
  `PassRecorder::insertLocalReadBarrier()` + `setLocalReadInputIndices()`. Pipeline variant gets
  `setFlags(VK_PIPELINE_CREATE_COLOR_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT)`.
- A final tonemap/convert pass writes the 8-bit swapchain image and the `--dump-rgba` readback.
- History ping-pong pair (temporal after-image) and a half-res blur target are allocated **lazily** —
  only when their `enabled` flag is set.

### 5.5 Vertex strategy & resolution independence
- Per-rod data in a per-frame SSBO (VMA `HOST_VISIBLE|HOST_COHERENT`, double-buffered with
  `FrameOverlap`); instanced draw (C-D3). Rod geometry = TRI_STRIP (4 verts, GS ground truth); orbs =
  billboard quads.
- Projection is float scalars; `setViewportScissor` uses the live `VkExtent2D`. `--width/--height`
  change nothing in logic or shaders. **The diff-gate dump path always renders at 640×224** (the Vulkan
  output MUST match `extract_ref.mjs`'s 640×224 or the diff fails regardless of correctness).
  Resolution independence is kept cheap but its payoff is a W10 deliverable (C-E1), not an early gate.

---

## 6. Phased Roadmap

W0 prerequisite (before W1 code), then W1–W10. Every phase ends with running, committable software and
an explicit gate. Commit convention `Type(Scope): imperative`, no Co-Authored-By.

### W0 — Day-0 runtime/static constant + oracle capture
Pull EVERY blend/projection/animation constant into named `constexpr` BEFORE any test, and resolve the
open questions the critique surfaced. Register reads are dead — **static disassembly + save-state memory
reads only**.

W0 deliverables (each a named constant or a documented resolution):
- **W0-Q1 (C-A1):** PCSX2 write-watchpoint on `0x375254` (rod+0x04), step one `draw_crystal_rod` →
  document the world-Y vs per-frame-angle lifetime order. Update the `Rod` model comment.
- **W0-Q2 (C-B1):** PCSX2 memory reads of `0x375250` (group A) + `0x377e50` (group B) → capture rods at
  **distinct screen Y and Z** for the projection oracle (defeat the near-colinear blind spot).
- **W0-Q3 (C-C4):** read `fGpffff8bc0` at the capture instant → is the camera orbit ≈0? Decide whether
  the MVP needs the camera term.
- Static-decompile the constant loads in `draw_crystal_rod @ 0x00232e38` / `projection_build
  @ 0x002730a8` → `fov`, `near`, `halfWidth`(4:3 & 16:9); confirm `far=2048`, `scale=65536`,
  `aspect=1.0`.
- **W0-Q4:** static-decompile the `0x29BCF0` trig-buffer layout (`draw_crystal_rod` prologue) — the
  rotation_build forward/up input mapping.
- Resolve the rod angle steps: group A steady `832c/8330`, and **group-B steady (C-A3)** — decompile or
  document reuse with evidence. Transition steps `831c/8320/8324/8328` (C-C1).
- Decode `FUN_00230fe8`'s arg→blend mapping (C-A4, `orbs-particles.md` blocker #6) → confirm orb
  halo(7)/core(6) blends; confirm rod subtractive via the `0x2973c0` ALPHA qword.
- Static-decompile orbit fn-table `0x00239440` / `0x00238d60` AND the **iteration count** (C-C3,
  `orbs-particles.md` blocker #2).
- **Noise floor (C-B2):** render the reference frame twice (or PCSX2 SW with two independent runs) and
  measure `pdiff.mjs` MAE between two correct frames → record `floor`. All later MAE gates are
  expressed as `floor + ε`.
- **Menu/digit oracle (C-B3):** capture resolved menu-item and clock-digit screen positions (live
  memory read after `MenuLayout` / `module_clock_231E48`) for `MenuLayoutTest` / `ClockDigitsTest`.

**Gate:** all constants committed with `name@addr` citations; W0-Q1..Q4 documented; noise floor
recorded; menu/digit position oracles captured. Any value that remains a runtime-computed fit is flagged
explicitly (not silently treated as evidence).

### W1 — Pure math library + projection oracle
`clock/math/` (FixedPoint, Basis, Projection), `clock/camera/CameraOrbit`, `clock/rod/RodField` +
`RodProjection`. CMake `clock` static lib (glm only).
**Gate (HARD):** `ctest -R math` green; **`ProjectionOracleTest`** — all captured spread rods (W0-Q2)
within 1 GS unit, depth-validation rods' Z ordering+ratio correct, AND the column-order choice is
unique. If spread was insufficient, the gate is explicitly downgraded to "regression test" in the
commit message and the residual risk is logged. Nothing proceeds until this passes.

### W2 — Rod geometry + first pixels (flat ring, instanced)
`RodField::init` (ring from captured X-values, both group skips), `SceneUpdater` (rods only, steady
angle), `clockvk/` minimal (`BlendStates`, `Pipelines` src-over, instanced `RodRenderer`,
`ClockRenderer` with HDR target + tonemap), `rod_flat` shaders, per-rod SSBO. Replace
`GsScene`/`GsRenderer` in `main.cpp`.
**Gate:** `RodFieldTest` (rod 0 within 0.01 of live trace; rods 1–4 X-values matched) + headless
`RodProjectionTest` (`gs == 0x77b3/0x8463 ± 1`) green. App renders a ring of white rods. RenderDoc is a
debug aid only — the positional gate is the numeric test, not an eyeball (C-B4). No pixel-diff target
yet (flat white ≠ textured reference).

### W3 — Steady animation + all 5 rod passes
`RodAnimation` (steady per-pass angle from W0 constants), `GlowAlpha` (13/6/10/5 divisors), all 5 pass
pipelines (src-over/additive/subtractive), per-pass angle offsets.
**Gate:** `RodAnimationTest` + `GlowAlphaTest` green. Pixel-diff rod-only frame (orbs/menu off) vs PCSX2
SW reference; record the baseline MAE (expected large — flat shading). Gate is "pipeline live, pass
ordering + blend accumulation correct," not final fidelity. Geometry sub-gate: rod screen positions
within 2 px.

### W3b — Transition (opening) animation — **C-C1**
The transition branch: `FUN_002335e8` ×2 group-transform init, `FUN_00236a80` Y-translate
`state[0x1b]*26*t`, the four transition angle steps `831c/8320/8324/8328`. Driven by `AnimState.transitionT`.
**Gate:** `RodTransitionTest` — the four transition steps, the `*26*t` Y-translate at sampled `t`, and
the group-transform init match the decode. The opening plays start→steady without geometry breakage.

### W4 — Orb system (4 stages, init-sized count)
`TrailBuffer` (50-ring + attenuation), `OrbField`/`OrbOrbit` (W0 fn-table constants + decoded count),
`OrbRenderer` (trail / halo ×30 / core ×4.5 / trail-2), orb shaders, 4 orb pass pipelines with the
**W0-confirmed** halo/core blends (C-A4).
**Gate:** `TrailBufferTest` + `OrbOrbitTest` + `OrbCountTest` (decoded count, not hardcoded 2) green.
Pixel-diff composite (rods+orbs); orbs present & moving; **per-stage blend check** (each orb stage's
blend matches the W0 decode) + orb-region MAE < `floor + 10`.

### W5 — Menu / UI overlay + config + time
`MenuState` (4-state, fade `counter*0x7F/total`: 4:3=15f / 16:9=12f, scroll), `MenuLayout`
(`DAT_00274c00` record decode → per-item XY — follow the 6 data pointers), `MenuRenderer`,
`menu_overlay` shaders (colored quads now; glyphs W8b), `Config` (two-word bit-field → `VisualConfig`,
widescreen → projection halfWidth), `TimeDisplay` (BCD + tz/DST/format). ImGui HUD shows time as a DEV
aid only (the 1:1 readout is W8a, C-C2).
**Gate:** `MenuFadeTest` (alpha at frame 0/7/15 for 4:3, 0/.../12 for 16:9) + `MenuLayoutTest` (per-item
XY matches the W0-captured numeric oracle, C-B3) + `TimeDisplayTest` green. Menu fades at the correct
frame count; time/date numerically correct.

### W6 — Prism rod surface (pass 2 geometry)
Replace the flat rod quad with the triangulated prism cross-section swept along the rotation Z axis;
`rod_prism` shaders (STQ + vertex color, additive); pass-2 per-rod angle. Prism texture deferred to W8
(1×1 white sampler now).
**Gate:** Pixel-diff composite (all 5 rod passes, no orbs) vs reference. MAE < `floor + 6` (additive
prisms dominate; correct geometry is the gate).

### W7 — Refraction pass (subtractive + HDR framebuffer feedback)
Confirm subtractive ALPHA from W0; `Pipelines` adds the input-read variant with the feedback-loop flag;
`insertLocalReadBarrier()` between pass 2 and 3; `rod_refract.frag` `subpassLoad(...)` (flat sample,
`bumpScale=0`); flip `inputAttachmentRead=true` on `RodRefract`. Reads the **HDR** target (C-D2 — 8-bit
feedback would band the subtractive result). RDNA2 supports `dynamic_rendering_local_read`; runtime-gate
behind a capability flag with a two-subpass fallback.
**Gate:** Pixel-diff full rod+orb composite. MAE < `floor + 4`. A passing diff confirms framebuffer
feedback is correct.

### W8a — Clock-face date/time digits — **C-C2**
`ClockDigits` + `DigitRenderer` + `digit` shaders: the on-screen clock readout in scene space
(`module_clock_231E48`). Glyph atlas: deswizzle the digit/font texture from a PCSX2 VRAM dump using the
kept `gs/` swizzle oracle + `extract_ref.mjs`; upload `R8_UNORM`.
**Gate:** `ClockDigitsTest` (digit positions match the W0 oracle) + composite pixel-diff with digits.
The on-screen time is the real rendered element, not the ImGui fallback.

### W8b — Menu glyphs + bump normal
Menu text via the same atlas, sampled at decoded `DAT_00274c00` positions; wire TEX0/TEX1 GS page
offsets (±0x1e NTSC / ±0x19 16:9). Procedural bump normal perturbs the refraction `subpassLoad` UV
(`bumpScale` push const / SSBO field).
**Gate:** Full composite pixel-diff incl. bump + menu text. MAE < `floor + 3` on rod area,
< `floor + 5` overall. Menu text legible.

### W9 — Temporal after-image + optional blur
History ping-pong pair in `ResourceManager` (lazy); `TemporalBlend` pass enabled before orb stages;
`blur_h`/`blur_v` half-res separable Gaussian enabled after menu. Retail blur state unknown → default
off until confirmed.
**Gate:** Full composite pixel-diff with temporal blend active. MAE < `floor + 2` — the final parity
gate.

### W10 — Polish + config integration + resolution test
Widescreen halfWidth switch wired through projection + menu layout; tz/DST verified against live time;
config index→option mapping (static decode or save-state read); scroll LUT base (`FUN_00247ae8`);
keyboard/controller → menu navigation AND camera orbit (C-C4) wired to input; perf pass (verify
instanced draw cost; persistent buffers if frame > 2 ms).
**Gate:** Every `VisualConfig` field produces the correct visual change; camera orbit responds to input;
MAE stays < `floor + 2`; render at 1920×1080 with no geometry breakage (resolution-independence payoff).

---

## 7. Verification Strategy

| Phase | Gate type | Pass condition |
|---|---|---|
| W0 | static evidence + capture | all constants cited; W0-Q1..Q4 documented; noise `floor` recorded; menu/digit oracles captured |
| W1 | CTest unit (oracle) | spread-rod ProjectionOracle < 1 GS unit + depth ordering + unique column order |
| W2 | CTest (headless) | RodFieldTest + RodProjectionTest green; rods at correct screen positions |
| W3 | CTest + pixel-diff baseline | RodAnimation+GlowAlpha green; pipeline live; geom < 2 px |
| W3b | CTest | RodTransitionTest green; opening plays to steady |
| W4 | CTest + pixel-diff | TrailBuffer+OrbOrbit+OrbCount green; per-stage blend correct; orb MAE < floor+10 |
| W5 | CTest + pixel-diff | MenuFade+MenuLayout+TimeDisplay green; menu XY matches numeric oracle |
| W6 | pixel-diff | all-rod composite MAE < floor+6 |
| W7 | pixel-diff | rod+orb composite MAE < floor+4 (HDR feedback correct) |
| W8a | CTest + pixel-diff | ClockDigitsTest green; digits rendered in scene |
| W8b | pixel-diff | composite MAE < floor+3 rod / < floor+5 overall; text legible |
| W9 | pixel-diff | full composite MAE < floor+2 (FINAL 1:1 gate) |
| W10 | functional + pixel-diff | config + camera correct; MAE < floor+2; 1080p no breakage |

**Oracles (numeric, falsifiable, no photos):**
- Projection oracle: spread rod world→screen pairs from BOTH groups + depth rods (`runtime-trace.md` +
  W0-Q2 capture); column-order uniqueness asserted.
- Geometry oracle: `Rod.gs == 0x77b3/0x8463 ± 1` after `RodProjection`.
- Formula oracles: glow divisors 13/6/10/5; trail attenuation `128−floor(i·50/(count−1))·3`.
- Position oracles: menu-item + clock-digit screen XY captured live (W0).
- Pixel-diff: `pdiff.mjs` per-channel mean/max (MAE, 0-255 RGB) + %pixels>16 + heatmap, always at
  640×224, every numeric gate expressed as **`floor + ε`** (floor measured in W0, C-B2).
- `clock/` runs entirely without a GPU — every math/formula/position gate is a plain CTest binary.

**MAE unit & noise floor (C-B2):** MAE = mean absolute per-channel difference on 0-255 RGB at 640×224.
The W0 noise floor (two correct references diffed) sets the unreachable-target guard: the clock disables
GS dither (DTHE=0, D-1), so the floor is driven by PCSX2 SW rounding/quantization, not a dither pattern.
No MAE gate is an absolute; all are `floor + ε`.

---

## 8. Open Decisions (each with a recommendation)

| # | Decision | Recommendation |
|---|---|---|
| D1 | Generate vs hardcode rod ring positions | **Generate** from a fitted radius+angular step (fit to the captured X-values within 0.1), verified against the traced subset. Hardcoding can't scale to the full ring. |
| D2 | Time source mapping | **Wall clock via `TimeSync`** + `VisualConfig` tz/DST math (`utc + tz_min·60 + dst·3600`). Animation phase uses `calc_animation_delta_time` smoothing (clamp jitter > 3000 ticks) so motion is frame-rate-independent. Menu fades stay frame-counted (PS2 semantics). |
| D3 | Widescreen / resolution | **Code resolution-independent from day one (cheap), payoff realized W10.** Widescreen = `halfWidth` switch (4:3 vs 16:9 global), NOT aspect (aspect is hardcoded 1.0). Diff gate always renders 640×224. Do NOT advertise it solved before the projection column order is confirmed (C-E1). |
| D4 | GLM vs hand-rolled math | **GLM.** Column-major matches `sceVu0MulMatrix` exactly; `cross`/`normalize` map 1:1 to VOPMSUB. `GLM_FORCE_DEPTH_ZERO_TO_ONE` set. |
| D5 | ECS vs plain structs | **Plain POD + `std::array` (rods) / `std::vector` (orbs, init-sized).** No entity churn; trivial to unit-test and value-copy across the boundary. |
| D6 | Shader language | **GLSL → glslc** (kept build globs `*.vert/.frag/.comp` to `vulkan1.3` SPIR-V). `subpassLoad`/`local_read` are first-class in GLSL. |
| D7 | FOV/near acquisition | **Static disassembly first** (register reads dead); save-state memory read fallback; oracle FIT to choose column order + validate, never the sole source for near. |
| D8 | `RenderOrchestrator` keep? | **Delete / thin dispatcher.** `main.cpp` → `ClockRenderer::record` directly. Keep only `FrameParams` plumbing. |
| D9 | Subtractive blend exact form | **`REVERSE_SUBTRACT`**, confirmed by the GS dump (`(0-Cs)As/128+Cd`, 1176 draws) AND the W0 `0x2973c0` ALPHA decode; W3 pixel-diff surfaces any sign error numerically. |
| D10 | Orb count | **Data-driven, decoded in W0** (C-C3) — NOT hardcoded 2. `Scene.orbs` is `std::vector` sized at init from the fn-table iteration count. |
| **D11** | **rod+0x04 lifetime (C-A1)** | **Resolve by W0-Q1 watchpoint.** Model static world-Y and per-frame angle as separate fields until the write order is known; do not assert "scratch." |
| **D12** | **Projection column order / tz·qz (C-A2)** | **Enumerate the small candidate set, fit each to the spread-rod + depth oracle, pick the unique survivor.** If >1 survives, capture more spread rods before proceeding past W1. |
| **D13** | **Ordered dither (F2-OD)** | **Do NOT add dither.** The clock runs DTHE=0 (GS dump, D-1); diff against the dither-off reference. (Other OSDSYS scenes may need it; the clock does not.) |
| **D14** | **Camera orbit in the MVP (C-C4)** | **Include conditionally.** W0-Q3 reads `fGpffff8bc0` at capture; if non-zero, the camera term enters the MVP before the W1 oracle fits; if zero, document "confirmed zero at capture." |
| **D15** | **Transition vs steady (C-C1)** | **Build steady first (W2–W3), transition in W3b.** Both are evidenced render paths; neither is skipped. |
| **D16** | **HDR vs UNORM8 target (C-D2)** | **`R16G16B16A16_SFLOAT`** main target; convert to 8-bit only for present + diff readback. Prevents banding/clipping in feedback + additive; zero parity cost (gate downsamples anyway). |
| **D17** | **Instanced vs per-rod draws (C-D3)** | **Instanced from W2.** Per-rod data in an SSBO indexed by `gl_InstanceIndex`; one `vkCmdDraw` per pass. Decided as a data-model choice, not a W10 perf patch. |
| **D18** | **Clock-face digits (C-C2)** | **Distinct scene pass (W8a)**, separate from menu text (W8b). ImGui time is a dev aid only. |

---

## 9. Risks + Mitigations

| ID | Risk | Sev | Mitigation |
|---|---|---|---|
| R1 | FOV/near unobtainable (live register read returns zeroed snapshot) | HIGH | W0 static disassembly of the constant loads; save-state memory read fallback; oracle validates. The W1 gate depends on this — W0's primary deliverable. |
| R2 | Projection column order / tz·qz ambiguous; matrix is a HYPOTHESIS (C-A2) | HIGH | W0-Q2 spread rods (both groups, distinct Y+Z) + W1 enumerate-and-fit (D12) with a uniqueness assertion. If non-unique, capture more rods before W2. Honest "fitted, not ported" labeling. |
| R3 | rod+0x04 lifetime collision (C-A1) | MED | W0-Q1 watchpoint resolves write order; model carries both fields meanwhile; `ProjectionOracleTest` fails fast if the world-XYZ source is wrong. |
| R4 | `0x29BCF0` trig-buffer layout (angle→forward/up) unknown | MED | W0-Q4 static decompile (~10-20 instrs). If misread, `ProjectionOracleTest` fails immediately. |
| R5 | Orbit fn-table is LUT/dynamic + orb count unknown (C-C3) | MED | W0 static decompile of both fns + the iteration count; embed LUT as `constexpr float[]`; `OrbCountTest` guards. Placeholder ellipse keeps W2–W3 unblocked; W4 gated on real values. |
| R6 | Group-B steady angle step undocumented (C-A3) | MED | W0 decompiles the group-B steady loop or documents reuse with evidence; committed as a cited `constexpr`. |
| R7 | Orb halo/core blend modes are hypotheses (C-A4) | MED | W0 decodes `FUN_00230fe8` arg→blend map; pass table marks them HYPOTHESIS; W4 per-stage blend check. |
| R8 | Transition path unscheduled / its own globals (C-C1) | MED | W3b dedicated phase + `RodTransitionTest` (four transition steps + `*26*t` translate + group init). |
| R9 | Camera orbit missing from MVP → silent oracle error (C-C4) | MED | W0-Q3 reads `fGpffff8bc0` at capture; conditional camera term (D14) before the W1 fit. |
| R10 | MAE gates unreachable / no noise floor (C-B2) | MED | W0 measures the floor (two correct refs); all gates are `floor + ε`. Clock is DTHE=0 (D-1) so no dither pattern inflates the floor. |
| R11 | 8-bit feedback bands/clips the late gates (C-D2) | MED | HDR `R16G16B16A16_SFLOAT` target (D16); 8-bit only at present/readback. |
| R12 | `dynamic_rendering_local_read` absent on driver | LOW | RDNA2 supports it; query at `VulkanContext` init; two-subpass fallback isolated to `Pipelines`/`PassRecorder`. Flat-first (W2–W6) has no dependency. |
| R13 | Subtractive blend ≠ REVERSE_SUBTRACT | LOW | GS dump confirms `(0-Cs)As/128+Cd`; W0 ALPHA decode + W3 diff catch a sign error. |
| R14 | Prism/glyph/digit texture decode multi-step (W6/W8) | LOW | Reuse the kept `gs/` swizzle oracle + `extract_ref.mjs`; data-pipeline task, not architecture. Split W8 into 8a/8b. |
| R15 | Per-frame SSBO/draw-list churn | LOW | Reserve capacity once; instanced draw (D17); retained buffers only if W10 profiling shows cost. |
| R16 | Menu layout pointers (`0x275c90` …) not yet decoded | LOW | W5 is late, off the critical path; decode incrementally; colored rects until glyphs (W8b). |

---

## 10. File-path index (load-bearing)

- Spec/strategy: `docs/OSDSYS-DECOMP-1to1-STRATEGY.md`, `docs/FOUNDATION-STATUS.md`,
  `docs/PHASE0-AUDIT.md`, `MEMORY.md`.
- Ground truth: `docs/ghidra_analysis/CLOCK-SYSTEM-MAP.md`, `rod-pipeline.md`, `vu0-math-pipeline.md`,
  `vu0_decode.md`, `orbs-particles.md`, `menu-ui.md`, `settings-config.md`, `runtime-trace.md`.
- Patent: `docs/clock_patent/US6693606-DIGEST.md` (method/ordering only, never numbers).
- Kept foundation: `src/core/VulkanContext.hpp`, `src/renderer/{PassRecorder,PipelineBuilder,
  ResourceManager}.hpp`, `src/app/TimeSync.hpp`, `tools/pixeldiff/{extract_ref,pdiff}.mjs`.
- This plan: `docs/superpowers/specs/2026-06-13-canonical-master-implementation-plan.md`.
