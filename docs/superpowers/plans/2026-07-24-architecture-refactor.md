# Architecture Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restructure the interior of CrystalClockVK per the approved spec (`docs/superpowers/specs/2026-07-24-architecture-refactor-design.md`): Engine with ordered teardown, swapchain-owned policy, declarative passes, scene modules, RAII guards — with zero visual/behavior change.

**Architecture:** Five phases, each independently buildable and verifiable. `main.cpp`'s god-loop migrates into `app/Engine`; policy and boilerplate sink into the layer that owns them (`renderer/`). The 4-layer structure (`core/renderer/gs/app`) is preserved.

**Tech Stack:** C++23, Vulkan 1.4 (dynamic rendering, Sync2), VMA, SDL3, vk-bootstrap, Dear ImGui, CMake 3.30+, MSVC.

## Global Constraints

- Zero visual change: clock scene and test scene must render identically before/after every task.
- Zero validation-layer errors at every checkpoint (grep the run log).
- English only; zero comments except complex GS-derived math (CLAUDE.md).
- PascalCase file/class naming, matching existing style.
- Commit convention `Type(Scope): imperative subject ≤72 chars`; no AI attribution anywhere.
- `gs/` untouched. Shader files and pipeline layouts untouched.
- Verification harness for every task: build Debug, run app 8 s, close, grep log; torture script after Tasks 2+.

## Verification Harness (used by every task)

Build: `cmake --build build --config Debug` from repo root. Expect: no `error` lines.

Smoke run (from repo root):
```bash
cd bin && (./CrystalClockVK.exe > ../run.log 2>&1 &) && sleep 8 && \
powershell -NoProfile -Command "$null=(Get-Process CrystalClockVK).CloseMainWindow()" && sleep 3 && \
grep -cE "ERROR|Assertion|Render loop error" ../run.log
```
Expected: `0`, and PowerShell reports no `CrystalClockVK` process after.

Torture (after Task 2 exists): `powershell -NoProfile -File tools/TortureTest.ps1`
Expected output ends with `SURVIVED` and `CLEAN EXIT`, grep count `0`.

---

### Task 1: Torture-test script as a repo tool

**Files:**
- Create: `tools/TortureTest.ps1`

**Interfaces:**
- Consumes: built `bin/CrystalClockVK.exe`.
- Produces: `tools/TortureTest.ps1` — run via `powershell -NoProfile -File tools/TortureTest.ps1`; prints `SURVIVED`/`DEAD MID-TORTURE`, then `CLEAN EXIT`/`STILL UP`, exits 0 only on success. All later tasks call it.

- [ ] **Step 1: Write the script**

```powershell
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$log = Join-Path $root 'torture.log'
Start-Process -FilePath (Join-Path $root 'bin\CrystalClockVK.exe') -WorkingDirectory (Join-Path $root 'bin') -RedirectStandardOutput $log -RedirectStandardError (Join-Path $root 'torture.err.log')
Start-Sleep 5
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class TT {
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool r);
}
'@
$p = Get-Process CrystalClockVK
1..2 | ForEach-Object {
  [TT]::ShowWindow($p.MainWindowHandle, 6) | Out-Null; Start-Sleep 2
  [TT]::ShowWindow($p.MainWindowHandle, 9) | Out-Null; Start-Sleep 2
}
[TT]::MoveWindow($p.MainWindowHandle, 80, 80, 1500, 850, $true) | Out-Null; Start-Sleep 2
[TT]::MoveWindow($p.MainWindowHandle, 120, 120, 800, 600, $true) | Out-Null; Start-Sleep 2
$alive = Get-Process CrystalClockVK -ErrorAction SilentlyContinue
if ($alive) { 'SURVIVED'; $null = $alive.CloseMainWindow() } else { 'DEAD MID-TORTURE'; exit 1 }
Start-Sleep 4
$bad = @(Select-String -Path $log, (Join-Path $root 'torture.err.log') -Pattern 'ERROR|Assertion|Render loop error' -ErrorAction SilentlyContinue).Count
"BadLines: $bad"
if (Get-Process CrystalClockVK -ErrorAction SilentlyContinue) { 'STILL UP'; exit 1 }
'CLEAN EXIT'
if ($bad -ne 0) { exit 1 }
```

- [ ] **Step 2: Run it against the current build**

Run: `powershell -NoProfile -File tools/TortureTest.ps1`
Expected: `SURVIVED`, `BadLines: 0`, `CLEAN EXIT`, exit code 0.

- [ ] **Step 3: Commit**

```bash
git add tools/TortureTest.ps1
git commit -m "Build(Project): Add window torture-test script"
```

---

### Task 2: RenderTargets struct

**Files:**
- Create: `src/renderer/RenderTargets.hpp`
- Modify: `src/main.cpp` (replace the three `AllocatedImage` locals and both create/destroy sites)

**Interfaces:**
- Consumes: `ResourceManager` (`createImage`, `destroyImage`), `AllocatedImage`.
- Produces:
```cpp
struct RenderTargets {
    AllocatedImage depth;
    AllocatedImage tunnel;
    AllocatedImage mainColor;
    void create(ResourceManager& res, VkExtent2D extent, VkFormat colorFormat);
    void destroy(ResourceManager& res);
};
```
Later tasks (`Engine`, scenes) receive `RenderTargets&`.

- [ ] **Step 1: Write the header**

```cpp
#pragma once

#include "renderer/ResourceManager.hpp"

struct RenderTargets {
    AllocatedImage depth;
    AllocatedImage tunnel;
    AllocatedImage mainColor;

    void create(ResourceManager& res, VkExtent2D extent, VkFormat colorFormat) {
        depth = res.createImage(extent, VK_FORMAT_D32_SFLOAT,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
        tunnel = res.createImage(extent, colorFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        mainColor = res.createImage(extent, colorFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    }

    void destroy(ResourceManager& res) {
        res.destroyImage(depth);
        res.destroyImage(tunnel);
        res.destroyImage(mainColor);
    }
};
```

- [ ] **Step 2: Use it in main.cpp**

Replace the three `AllocatedImage depthImage/tunnelImage/mainColorImage = resources.createImage(...)` declarations with:
```cpp
RenderTargets targets;
targets.create(resources, swapchain.extent(), swapchain.imageFormat());
```
Replace the resize-path destroy/create block with `targets.destroy(resources); targets.create(resources, swapchain.extent(), swapchain.imageFormat());`, the exit-path destroys with `targets.destroy(resources);`, and every `depthImage`/`tunnelImage`/`mainColorImage` reference with `targets.depth`/`targets.tunnel`/`targets.mainColor` (including `transitionDepthImage(vulkan, frames[0], targets.depth)`). Add `#include "renderer/RenderTargets.hpp"` to main.cpp.

- [ ] **Step 3: Build + smoke + torture**

Run the Verification Harness. Expected: build clean, grep `0`, torture `CLEAN EXIT`.

- [ ] **Step 4: Commit**

```bash
git add src/renderer/RenderTargets.hpp src/main.cpp
git commit -m "Refactor(Renderer): Extract render targets into RenderTargets"
```

---

### Task 3: SwapchainManager owns frame policy

**Files:**
- Modify: `src/renderer/SwapchainManager.hpp`, `src/renderer/SwapchainManager.cpp`
- Modify: `src/main.cpp` (delete `SwapchainSync`, resize block, acquire/present handling)

**Interfaces:**
- Consumes: existing `SwapchainManager` internals, `VulkanContext` (`physicalDevice()`, `surface()`, `device()`), the `SwapchainSync` struct currently in `main.cpp` (moves in, verbatim).
- Produces:
```cpp
enum class FrameStatus { Ready, SkipFrame, Recreated };

class SwapchainManager {
public:
    FrameStatus beginFrame(SDL_Window* window);
    VkSemaphore acquireSemaphore() const;
    VkSemaphore renderSemaphore() const;
    void endFrame(bool& outNeedsRecreate);
    ...
};
```
`beginFrame` does: minimized/zero-size guard (returns `SkipFrame`, sleeps 50 ms), surface-caps zero guard, pending-recreate handling (`vkDeviceWaitIdle` + `recreate` + sync recreation, returns `Recreated`), image acquire (OUT_OF_DATE ⇒ mark pending, return `SkipFrame`). `endFrame` runs `present` and folds OUT_OF_DATE/SUBOPTIMAL into the pending-recreate flag.

- [ ] **Step 1: Move SwapchainSync into SwapchainManager**

Cut the `SwapchainSync` struct from `main.cpp` and add it (verbatim) as a private member `m_sync` of `SwapchainManager`, created in the constructor and recreated inside the new recreate path. Expose `acquireSemaphore()` (the semaphore used for the current acquire) and `renderSemaphore()` (`m_sync.renderSemaphoreForImage(imageIndex())`).

- [ ] **Step 2: Implement beginFrame/endFrame**

```cpp
FrameStatus SwapchainManager::beginFrame(SDL_Window* window) {
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    if (w == 0 || h == 0 || (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) {
        SDL_Delay(50);
        return FrameStatus::SkipFrame;
    }

    FrameStatus status = FrameStatus::Ready;
    if (m_pendingRecreate) {
        VkSurfaceCapabilitiesKHR caps{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_ctx.physicalDevice(), m_ctx.surface(), &caps);
        if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0 ||
            caps.maxImageExtent.width == 0 || caps.maxImageExtent.height == 0) {
            SDL_Delay(50);
            return FrameStatus::SkipFrame;
        }
        vkDeviceWaitIdle(m_ctx.device());
        recreate(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        m_sync.destroy(m_ctx.device());
        m_sync = SwapchainSync::create(m_ctx.device(), imageCount());
        m_pendingRecreate = false;
        status = FrameStatus::Recreated;
    }

    m_currentAcquireSem = m_sync.nextAcquireSemaphore();
    VkResult res = acquireNextImage(m_currentAcquireSem);
    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        m_pendingRecreate = true;
        return FrameStatus::SkipFrame;
    }
    return status;
}

void SwapchainManager::endFrame(bool&) {
    VkResult res = present(m_sync.renderSemaphoreForImage(imageIndex()));
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) m_pendingRecreate = true;
}
```
(`SwapchainManager` needs a `const VulkanContext& m_ctx` reference and `bool m_pendingRecreate{false}`, `VkSemaphore m_currentAcquireSem{VK_NULL_HANDLE}` members; adjust the constructor accordingly. Include `<SDL3/SDL.h>`.)

- [ ] **Step 3: Rewrite the main loop to use it**

The loop top becomes:
```cpp
window.pollEvents();
FrameStatus fs = swapchain.beginFrame(window.getHandle());
if (fs == FrameStatus::SkipFrame) continue;
if (fs == FrameStatus::Recreated) {
    targets.destroy(resources);
    targets.create(resources, swapchain.extent(), swapchain.imageFormat());
    transitionDepthImage(vulkan, frames[0], targets.depth);
}
```
Delete `resizeRequested`, the old resize block, and the manual acquire; use `swapchain.acquireSemaphore()` / `swapchain.renderSemaphore()` in the submit info and replace the present call with `bool dummy; swapchain.endFrame(dummy);`. Keep dt/fps computation above the guard unchanged (move it below `pollEvents()` if it sits below the deleted block).

- [ ] **Step 4: Build + smoke + torture**

Run the Verification Harness. Expected: grep `0`, torture `CLEAN EXIT`.

- [ ] **Step 5: Commit**

```bash
git add src/renderer/SwapchainManager.hpp src/renderer/SwapchainManager.cpp src/main.cpp
git commit -m "Refactor(Renderer): Move frame policy into SwapchainManager"
```

---

### Task 4: Engine object with ordered teardown

**Files:**
- Create: `src/app/Engine.hpp`, `src/app/Engine.cpp`
- Modify: `src/main.cpp` (shrinks to ~20 lines)
- Modify: `CMakeLists.txt` only if sources are listed explicitly (they are globbed — verify, otherwise no change).

**Interfaces:**
- Consumes: everything `main.cpp` uses today.
- Produces:
```cpp
class Engine {
public:
    Engine();
    ~Engine();
    void run();
private:
    void runFrame();
    WindowContext m_window;
    VulkanContext m_vulkan;
    SwapchainManager m_swapchain;
    ResourceManager m_resources;
    UIRenderer m_ui;
    RenderOrchestrator m_orchestrator;
    RenderDocWrapper m_rdoc;
    std::array<FrameData, FrameOverlap> m_frames;
    RenderTargets m_targets;
    TestSceneParams m_testParams;
    // arcball state, timing state, frameNumber
};
```
`main.cpp` becomes:
```cpp
#include "app/Engine.hpp"
#include <iostream>

int main(int, char*[]) {
    try {
        Engine engine;
        engine.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return -1;
    }
    return 0;
}
```

- [ ] **Step 1: Create Engine.hpp/Engine.cpp by moving code**

Mechanical move, no logic change: constructor body = everything from today's `main` before the loop (member-initializer order **is** the teardown order — declare members in the construction order above). `run()` = the `while` loop wrapping `runFrame()` in the inner try/catch (`Render loop error` message stays). `runFrame()` = one iteration of today's loop body. `~Engine()` = the post-loop cleanup (`vkDeviceWaitIdle`, `orchestrator.destroy`, `targets.destroy`, `frame.destroy` loop), guarded by `if (m_vulkan.device() != VK_NULL_HANDLE)`. `transitionDepthImage` moves to a static function in `Engine.cpp`.

- [ ] **Step 2: Shrink main.cpp**

Replace the whole file with the ~15-line version from the Interfaces block.

- [ ] **Step 3: Build + smoke + torture**

Run the Verification Harness. Expected: grep `0`, torture `CLEAN EXIT`. Also verify teardown robustness: temporarily `throw std::runtime_error("inject")` at the top of `runFrame()`, build, run — expect `Render loop error: inject` in the log and clean exit with **no VMA assert**; remove the throw and rebuild before committing.

- [ ] **Step 4: Commit**

```bash
git add src/app/Engine.hpp src/app/Engine.cpp src/main.cpp
git commit -m "Refactor(App): Extract Engine with ordered teardown from main"
```

---

### Task 5: Declarative pass recording in PassRecorder

**Files:**
- Modify: `src/renderer/PassRecorder.hpp`, `src/renderer/PassRecorder.cpp`
- Modify: `src/app/Engine.cpp` (frame recording uses the new API)

**Interfaces:**
- Consumes: existing `transitionImage`, `beginRendering`, `endRendering`, debug labels, `AllocatedImage`.
- Produces:
```cpp
struct PassDesc {
    const char* name;
    float color[3];
    AllocatedImage* colorTarget;
    AllocatedImage* depthTarget;          // nullptr = none
    std::vector<AllocatedImage*> reads;   // sampled inputs
    VkClearValue* clear;                  // nullptr = load
    VkExtent2D extent;
};

class PassRecorder {
    void runPass(const PassDesc& desc, const std::function<void()>& body);
    void copyImage(AllocatedImage& src, AllocatedImage& dst, VkExtent2D extent);
    void resetLayoutTracking();
    ...
};
```
Semantics: `PassRecorder` keeps `std::unordered_map<VkImage, VkImageLayout> m_layouts` (missing = `UNDEFINED`). `runPass` transitions each `reads[i]` to `SHADER_READ_ONLY_OPTIMAL` and `colorTarget` to `COLOR_ATTACHMENT_OPTIMAL` (from tracked layout), begins label+rendering+viewport, calls `body`, ends rendering+label, updates the map. `copyImage` transitions src→`TRANSFER_SRC_OPTIMAL`, dst→`TRANSFER_DST_OPTIMAL`, records `vkCmdCopyImage`, updates the map. Depth attaches as-is (transitioned once at startup, never tracked). `resetLayoutTracking()` is called once per frame at command-buffer begin; to preserve tunnel/mainColor content across frames while starting from tracked-unknown, seed entries whose previous layout is known: on reset, every tracked entry becomes `UNDEFINED` **except** none — i.e. the map is cleared, and the first use each frame transitions from `UNDEFINED`, matching today's behavior for tunnel (cleared each frame by its pass) and mainColor (fully overwritten by the tunnel copy).

- [ ] **Step 1: Implement runPass/copyImage/resetLayoutTracking**

```cpp
void PassRecorder::runPass(const PassDesc& desc, const std::function<void()>& body) {
    for (AllocatedImage* r : desc.reads) {
        VkImageLayout cur = m_layouts.count(r->image) ? m_layouts[r->image] : VK_IMAGE_LAYOUT_UNDEFINED;
        if (cur != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            transitionImage(r->image, cur, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            m_layouts[r->image] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
    }
    VkImageLayout cur = m_layouts.count(desc.colorTarget->image) ? m_layouts[desc.colorTarget->image] : VK_IMAGE_LAYOUT_UNDEFINED;
    if (cur != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        transitionImage(desc.colorTarget->image, cur, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        m_layouts[desc.colorTarget->image] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    beginDebugLabel(desc.name, desc.color[0], desc.color[1], desc.color[2]);
    if (desc.depthTarget)
        beginRendering(desc.colorTarget->imageView, desc.depthTarget->imageView, desc.extent, desc.clear);
    else
        beginRendering(desc.colorTarget->imageView, desc.extent, desc.clear);
    setViewportScissor(desc.extent);
    body();
    endRendering();
    endDebugLabel();
}

void PassRecorder::copyImage(AllocatedImage& src, AllocatedImage& dst, VkExtent2D extent) {
    VkImageLayout sc = m_layouts.count(src.image) ? m_layouts[src.image] : VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout dc = m_layouts.count(dst.image) ? m_layouts[dst.image] : VK_IMAGE_LAYOUT_UNDEFINED;
    if (sc != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) transitionImage(src.image, sc, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    if (dc != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) transitionImage(dst.image, dc, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.extent = {extent.width, extent.height, 1};
    vkCmdCopyImage(m_cmd, src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    m_layouts[src.image] = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    m_layouts[dst.image] = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
}

void PassRecorder::resetLayoutTracking() { m_layouts.clear(); }
```
`PassRecorder.hpp` gains `#include "ResourceManager.hpp"`, `<functional>`, `<vector>`, `<unordered_map>`, the `PassDesc` struct, and the three declarations plus `std::unordered_map<VkImage, VkImageLayout> m_layouts;`. Note the final blit to swapchain keeps using raw `transitionImage` (swapchain images are not `AllocatedImage`); after the last `copyImage(mainColor → …)` the mainColor tracked layout is already `TRANSFER_SRC_OPTIMAL`, which is what the blit needs.

- [ ] **Step 2: Convert Engine::runFrame recording**

Replace today's transition/copy/beginRendering blocks with:
```cpp
recorder.resetLayoutTracking();
VkClearValue clear{}; clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
VkExtent2D ext = m_swapchain.extent();

recorder.runPass({m_testParams.enabled ? "Test Background" : "Tunnel Background",
                  {0.2f, 0.2f, 0.6f}, &m_targets.tunnel, &m_targets.depth, {}, &clear, ext}, [&] {
    if (m_testParams.enabled) m_orchestrator.recordTestBackgroundPass(recorder, params, m_testParams);
    else m_orchestrator.recordTunnelPass(recorder, params);
});

recorder.copyImage(m_targets.tunnel, m_targets.mainColor, ext);

recorder.runPass({m_testParams.enabled ? "Test Cube" : "Crystal Clock (Pass 1)",
                  {0.2f, 0.4f, 1.0f}, &m_targets.mainColor, &m_targets.depth,
                  {&m_targets.tunnel}, nullptr, ext}, [&] {
    if (m_testParams.enabled) m_orchestrator.recordTestCubePass(recorder, params, m_testParams);
    else m_orchestrator.recordCrystalPasses(recorder, params);
});

if (!m_testParams.enabled) {
    recorder.copyImage(m_targets.mainColor, m_targets.tunnel, ext);
    recorder.runPass({"Crystal Clock (Pass 2: Inter-Rod)", {0.4f, 0.6f, 1.0f},
                      &m_targets.mainColor, &m_targets.depth, {&m_targets.tunnel}, nullptr, ext}, [&] {
        m_orchestrator.recordCrystalPasses(recorder, params);
    });
}
```
The ImGui overlay + final blit code stays; the blit's first `transitionImage(mainColor, COLOR_ATTACHMENT_OPTIMAL, TRANSFER_SRC_OPTIMAL)` must read the tracked layout instead — mainColor is in `COLOR_ATTACHMENT_OPTIMAL` after `ui.render`, so keep that call unchanged (ui.render requires COLOR_ATTACHMENT — `runPass` leaves it there).

- [ ] **Step 3: Build + smoke + torture + RenderDoc sanity**

Run the Verification Harness. Expected: grep `0` (validation silence proves barrier parity), torture `CLEAN EXIT`. Visually compare clock scene against pre-refactor.

- [ ] **Step 4: Commit**

```bash
git add src/renderer/PassRecorder.hpp src/renderer/PassRecorder.cpp src/app/Engine.cpp
git commit -m "Refactor(Renderer): Add declarative runPass/copyImage to PassRecorder"
```

---

### Task 6: IScene + TestScene module

**Files:**
- Create: `src/app/IScene.hpp`, `src/app/TestScene.hpp`, `src/app/TestScene.cpp`
- Modify: `src/app/Engine.hpp`, `src/app/Engine.cpp`

**Interfaces:**
- Consumes: `RenderOrchestrator` (shared infra: pipelines, descriptors, meshes), `PassRecorder::runPass/copyImage`, `RenderTargets`, `FrameParams`, `TestSceneParams`.
- Produces:
```cpp
class IScene {
public:
    virtual ~IScene() = default;
    virtual void update(const FrameParams& params, float dt) = 0;
    virtual void record(PassRecorder& recorder, const FrameParams& params, RenderTargets& targets) = 0;
    virtual void drawUI() = 0;
};

class TestScene : public IScene {
public:
    explicit TestScene(RenderOrchestrator& orchestrator);
    void update(const FrameParams& params, float dt) override;   // arcball input + cubeModel
    void record(PassRecorder&, const FrameParams&, RenderTargets&) override; // bg pass + copy + cube pass
    void drawUI() override;                                       // the whole Refraction panel
    TestSceneParams& params();                                    // Engine feeds updateUBO
private:
    RenderOrchestrator& m_orchestrator;
    TestSceneParams m_params;
    glm::quat m_cubeRot{1.0f, 0.0f, 0.0f, 0.0f};
    float m_cubeScale{1.0f};
    bool m_autoRotate{false};
};
```

- [ ] **Step 1: Write IScene.hpp and TestScene**

Move from `Engine.cpp` into `TestScene`: the arcball/wheel/auto-rotate block (→ `update`), the test-scene branches of the pass recording (→ `record`, using `runPass`/`copyImage` exactly as written in Task 5), and the whole `"Refraction Test Scene"` ImGui window body minus the enable checkbox (→ `drawUI`). `m_params.enabled` stays but is driven by Engine (Step 2).

- [ ] **Step 2: Engine drives scenes**

Engine gains `TestScene m_testScene{m_orchestrator};` and a `bool m_testSceneActive{false}`. The frame flow becomes: Engine draws a small window with the `Enable Test Scene` checkbox bound to `m_testSceneActive` (kept in the existing debug window), sets `m_testScene.params().enabled = m_testSceneActive`, then:
```cpp
if (m_testSceneActive) {
    m_testScene.update(params, dt);
    m_orchestrator.updateUBO(params, &m_testScene.params());
    m_testScene.record(recorder, params, m_targets);
} else {
    m_orchestrator.updateUBO(params, nullptr);
    // clock passes (moved to ClockScene in Task 7)
}
if (m_testSceneActive) m_testScene.drawUI();
```

- [ ] **Step 3: Build + smoke + torture**

Run the Verification Harness; additionally toggle the checkbox on and interact (drag) for a visual check. Expected: identical behavior to before, grep `0`.

- [ ] **Step 4: Commit**

```bash
git add src/app/IScene.hpp src/app/TestScene.hpp src/app/TestScene.cpp src/app/Engine.hpp src/app/Engine.cpp
git commit -m "Refactor(App): Extract TestScene as IScene module"
```

---

### Task 7: ClockScene module

**Files:**
- Create: `src/app/ClockScene.hpp`, `src/app/ClockScene.cpp`
- Modify: `src/app/Engine.hpp`, `src/app/Engine.cpp`

**Interfaces:**
- Consumes: `IScene`, `RenderOrchestrator` recording methods, Task 5 pass API.
- Produces:
```cpp
class ClockScene : public IScene {
public:
    explicit ClockScene(RenderOrchestrator& orchestrator);
    void update(const FrameParams& params, float dt) override;   // no-op today
    void record(PassRecorder&, const FrameParams&, RenderTargets&) override;
    void drawUI() override;   // the CrystalClock Debug time/rod readouts
private:
    RenderOrchestrator& m_orchestrator;
};
```
Engine holds `IScene* m_activeScene` pointing at `m_clockScene` or `m_testScene`; the frame flow collapses to:
```cpp
m_activeScene->update(params, dt);
m_orchestrator.updateUBO(params, m_testSceneActive ? &m_testScene.params() : nullptr);
m_activeScene->record(recorder, params, m_targets);
m_activeScene->drawUI();
```

- [ ] **Step 1: Write ClockScene**

`record` = the non-test pass sequence from Task 5 Step 2 (tunnel pass, copy, crystal pass 1, copy, inter-rod pass). `drawUI` = the time/highlighted-rod/draw-call `ImGui::Text` block from the debug window (FPS + RenderDoc button stay in Engine).

- [ ] **Step 2: Engine scene switch**

Replace the `if (m_testSceneActive)` frame branches with the `m_activeScene` flow above; the checkbox sets `m_activeScene = m_testSceneActive ? static_cast<IScene*>(&m_testScene) : &m_clockScene;`. No `if (test)` remains in the recording path.

- [ ] **Step 3: Build + smoke + torture + both scenes visual check**

Run the Verification Harness; toggle scenes at runtime. Expected: grep `0`, both scenes render as before.

- [ ] **Step 4: Commit**

```bash
git add src/app/ClockScene.hpp src/app/ClockScene.cpp src/app/Engine.hpp src/app/Engine.cpp
git commit -m "Refactor(App): Extract ClockScene and scene switching"
```

---

### Task 8: RAII guards

**Files:**
- Create: `src/renderer/FrameGuards.hpp`
- Modify: `src/app/Engine.cpp`, `src/renderer/PassRecorder.cpp` (labels via guard internally)

**Interfaces:**
- Produces:
```cpp
class UIFrameGuard {
public:
    explicit UIFrameGuard(UIRenderer& ui) : m_ui(ui) { m_ui.beginFrame(); }
    void render(VkCommandBuffer cmd, VkImageView view, VkExtent2D extent) {
        m_ui.render(cmd, view, extent);
        m_rendered = true;
    }
    ~UIFrameGuard() { if (!m_rendered) ImGui::EndFrame(); }
private:
    UIRenderer& m_ui;
    bool m_rendered{false};
};

class DebugLabelGuard {
public:
    DebugLabelGuard(PassRecorder& r, const char* name, float cr, float cg, float cb)
        : m_r(r) { m_r.beginDebugLabel(name, cr, cg, cb); }
    ~DebugLabelGuard() { m_r.endDebugLabel(); }
private:
    PassRecorder& m_r;
};
```

- [ ] **Step 1: Write FrameGuards.hpp and adopt**

In `Engine::runFrame`, replace the bare `m_ui.beginFrame()` with `UIFrameGuard uiFrame(m_ui);` constructed after acquire, and `m_ui.render(...)` with `uiFrame.render(...)`. Any early `return` from `runFrame` after that point now auto-closes the ImGui frame. In `PassRecorder::runPass`, replace begin/endDebugLabel with a local `DebugLabelGuard`.

- [ ] **Step 2: Build + smoke + torture**

Run the Verification Harness. Expected: grep `0`, torture `CLEAN EXIT`.

- [ ] **Step 3: Commit**

```bash
git add src/renderer/FrameGuards.hpp src/app/Engine.cpp src/renderer/PassRecorder.cpp
git commit -m "Refactor(Renderer): Add RAII guards for UI frame and debug labels"
```

---

### Task 9: Final sweep

**Files:**
- Modify: `CLAUDE.md` (architecture section mentions Engine/scenes), memory update.

- [ ] **Step 1: Grep for leftovers**

Run: `grep -rn "transitionImage\|vkCmdCopyImage" src/app/` — expected: no matches (only `renderer/` touches barriers, except the swapchain blit inside Engine which is allowed if it remained; if it did, note it as accepted debt in the commit body).
Run: `grep -rn "testParams.enabled" src/app/Engine.cpp` — expected: only the checkbox/scene-switch lines.

- [ ] **Step 2: CI check**

Push and confirm GitHub Actions Windows + macOS builds pass.

- [ ] **Step 3: Update docs + commit**

Update the CLAUDE.md layer description (app/: Engine, scenes, orchestration) and project memory phase note.
```bash
git add CLAUDE.md
git commit -m "Docs(Project): Document Engine and scene architecture"
```
