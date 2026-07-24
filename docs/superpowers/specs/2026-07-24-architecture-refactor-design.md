# CrystalClockVK — Architecture Refactor Design

Date: 2026-07-24
Status: proposed

## Motivation

The 2026-07-24 minimize crash exposed structural weaknesses, not bad luck:

- `main.cpp` is a ~570-line god-loop owning frame orchestration, resize
  policy, swapchain sync, input, ImGui panels, pass sequencing, and
  hand-rolled barriers.
- Object lifetimes are implicit stack order inside one `try` block. Any
  exception in the loop unwound `VulkanContext` (and the VMA allocator)
  before cleanup ran, so the real error was masked by a VMA assert.
- Swapchain policy (OUT_OF_DATE, SUBOPTIMAL, minimize, surface-zero) is
  scattered across three places in the loop.
- The tunnel↔mainColor refraction ping-pong is 60+ lines of inline
  transitions/copies duplicated per scene variant.
- `RenderOrchestrator` mixes pipelines, descriptors, meshes, textures,
  rod state, and the test scene.
- `DeletionQueue` exists and is unused.

Goal: same 4-layer structure (`core/`, `renderer/`, `gs/`, `app/`),
cleaner interior. No behavior change, no big-bang rewrite — five phases,
each independently buildable and testable.

## Phase 1 — Engine object and ordered teardown

New `app/Engine.{hpp,cpp}`:

- Owns (in declaration order): `WindowContext`, `VulkanContext`,
  `SwapchainManager`, `ResourceManager`, `UIRenderer`, scenes, frame
  data, sync, render targets.
- `Engine::run()` = loop; `Engine::runFrame()` = one frame.
- Destructor performs ordered teardown via `DeletionQueue`; render-loop
  exceptions are caught inside `runFrame` callers so unwinding never
  crosses `VulkanContext`.
- `main.cpp` shrinks to ~20 lines: construct `Engine`, `run()`, catch,
  report.

Success: behavior identical; exception injected anywhere in the loop
produces a logged message and clean exit (no VMA assert).

## Phase 2 — SwapchainManager owns its policy

- `SwapchainManager::beginFrame(acquireSem)` returns
  `FrameStatus { Ready, SkipFrame, Recreated }`.
- Internally handles: surface-capability zero-extent guard, minimize
  wait, OUT_OF_DATE/SUBOPTIMAL recreate, sync-object recreation.
- Render-target (depth/tunnel/mainColor) recreation moves behind a
  `RenderTargets` struct with a `recreate(extent)` method called on
  `Recreated`.

Success: no swapchain/resize logic remains in `Engine::runFrame`; the
minimize torture test passes.

## Phase 3 — Declarative pass recording

Small frame-graph-lite in `renderer/`:

- `struct PassDesc { name; colorTarget; depthTarget; reads[]; clear?; }`
- `PassRecorder::runPass(desc, fn)` emits the needed image transitions
  before/after and debug labels; `PassRecorder::copy(src, dst)` wraps
  the transition+copy+restore dance.
- The tunnel→main copy and inter-rod ping-pong become 3 declarative
  calls each.

Success: zero raw `transitionImage`/`vkCmdCopyImage` calls in `app/`;
validation layer stays silent.

## Phase 4 — Scenes as modules

- `app/IScene.hpp`: `init(ctx)`, `update(FrameParams&, Input&)`,
  `record(PassRecorder&, RenderTargets&)`, `drawUI()`, `destroy()`.
- `app/ClockScene` — current tunnel + rods path (wraps most of today's
  `RenderOrchestrator` recording).
- `app/TestScene` — refraction test (background + cube + panel +
  arcball input), replacing the `testParams.enabled` branches.
- `RenderOrchestrator` slims into shared infra: `PipelineCache`
  (name → pipeline), descriptor plumbing, `MeshLibrary`, texture
  loading. Scenes reference it.

Success: switching scene is one call; no `if (test)` branches in the
frame loop.

## Phase 5 — RAII guards

- `UIFrameGuard` pairs `beginFrame`/`render` even on early-out paths.
- `DebugLabelGuard` pairs begin/end labels.
- `DeletionQueue` used by `Engine` and scenes for GPU resources.

Success: no manually paired begin/end calls in `app/`.

## Non-goals

- No RHI abstraction, no render-thread split, no material system.
- `gs/` untouched (pure math stays pure).
- Shader/pipeline layouts unchanged.

## Testing

Each phase: build Debug, run, verify visually (clock + test scene),
run the torture script (2× minimize/restore, 2× resize, clean close)
and require zero validation errors and clean exit. CI must stay green
on Windows + macOS.

## Risks

- Phase 3 barrier inference must match today's hand-rolled layouts
  exactly — validate with the validation layer + RenderDoc capture.
- Phase 4 moves per-rod state; keep `CrystalMath`/`GsCrystalMath` calls
  byte-identical to avoid visual drift from the PS2 reference.
