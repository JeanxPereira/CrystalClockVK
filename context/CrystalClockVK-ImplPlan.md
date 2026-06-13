# Native Vulkan Clean-Room Implementation Plan (Final Architecture)

This plan details the complete architectural pivot to a **Native Vulkan C++** architecture, strictly following the engineering constraints debated for the CrystalClock port. We prioritize a lean, highly performant target (Windows/RDNA2 baseline) over bloated multi-platform abstractions (like RHI layers), keeping the GS logic completely isolated from the Vulkan Backend.

---

## 4-Tier Architectural Layers

### 1. `core/` — System Bootstrap
No logic, just system handles.
- **`WindowContext`**: SDL3 window lifecycle.
- **`VulkanContext`**: Owns `VkDevice`, `VkQueue`, and `VkSurfaceKHR` setup via `vk-bootstrap`. Everything else takes this as a `const &`.
- **`RenderDocWrapper`**: Hooks into the RenderDoc in-app API for automated frame capture and shader debugging.

### 2. `renderer/` — The Lean Vulkan Wrapper
Abstracts raw Vulkan into modern engine patterns. **No RHI interfaces**, direct Vulkan 1.3 usage.
- **`ResourceManager`**: Wraps VMA. Allocates textures/buffers and handles staging uploads.
- **`PipelineBuilder`**: Builder pattern for `VkGraphicsPipelineCreateInfo` to eliminate boilerplate.
- **`PassRecorder`**: The single source of truth for `vkCmd*` calls. Wraps `vkCmdBeginRendering` (Dynamic Rendering) and handles Pipeline Barriers for Layout Transitions via Sync2.
- **`SwapchainManager` & `FrameData`**: Handles the frames-in-flight (Fence/Semaphore syncing).
- **`DeletionQueue`**: RAII queue for resource destruction, cleanly executed after GPU idle.
- **`UIRenderer`**: Wraps the Dear ImGui Vulkan/SDL3 backends, handles a dedicated ImGui descriptor pool, and allows simple draw commands to inject real-time debugging UI into the render loop.

### 3. `gs/` — Pure PS2 Logic (No Vulkan Allowed)
Can be unit-tested without a GPU context.
- **`SwizzleEngine` & `TextureDecoder`**: Reads raw `.bin` and applies CSM1/PSMCT maths. Returns `std::vector<uint8_t>`, which the `ResourceManager` will then copy to VRAM.
- **`VramBuffer`**: Manages the virtual addressing of the 4MB workspace.
- **`GsRegisterState`**: Pure structs mapping the hardware states.

### 4. `app/` — CrystalClock App
The orchestration layer.
- **`RenderOrchestrator`**: Translates decoded GS packet state into Vulkan draws using `PassRecorder`, and queues the ImGui draw data over the final Swapchain image.
- **Pass routing**: implicit in pipeline binding (the GS state per primitive selects the pipeline), not a push-constant pass index.
- **Push Constants & UBOs**: Per-object matrices move to a UBO. Push Constants hold only small per-draw scalars to fit hardware limits.

---

## Final Project Structure

```text
CrystalClockVK/
├── CMakeLists.txt
├── 3rdparty/                         # vkb, VMA, glm, SDL3, stb, imgui, renderdoc
├── assets/                           # bin / spv
├── src/
│   ├── core/
│   │   ├── WindowContext.hpp/.cpp
│   │   ├── VulkanContext.hpp/.cpp
│   │   └── RenderDocWrapper.hpp/.cpp
│   ├── renderer/
│   │   ├── ResourceManager.hpp/.cpp
│   │   ├── SwapchainManager.hpp/.cpp
│   │   ├── DescriptorAllocator.hpp/.cpp
│   │   ├── PipelineBuilder.hpp/.cpp
│   │   ├── ShaderLoader.hpp/.cpp
│   │   ├── PassRecorder.hpp/.cpp
│   │   ├── FrameData.hpp/.cpp
│   │   ├── UIRenderer.hpp/.cpp
│   │   └── DeletionQueue.hpp
│   ├── gs/
│   │   ├── VramBuffer.hpp/.cpp
│   │   ├── SwizzleEngine.hpp/.cpp
│   │   ├── GsRegisterState.hpp
│   │   └── TextureDecoder.hpp/.cpp
│   └── app/
│       ├── CrystalClock.hpp/.cpp     # Handles ImGui Windows & Main Loop
│       ├── RenderOrchestrator.hpp/.cpp
│       ├── CrystalMath.hpp
│       └── TimeSync.hpp/.cpp
└── shaders/
    ├── GsSprite.vert / .frag         # Passes 1-3
    ├── GsCrystal.frag                # Vulkan local_read Refraction 
    └── Tunnel.vert / .frag           
```
