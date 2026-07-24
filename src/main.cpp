#include "core/WindowContext.hpp"
#include "core/VulkanContext.hpp"
#include "core/RenderDocWrapper.hpp"
#include "renderer/SwapchainManager.hpp"
#include "renderer/FrameData.hpp"
#include "renderer/DeletionQueue.hpp"
#include "renderer/PassRecorder.hpp"
#include "renderer/ResourceManager.hpp"
#include "renderer/RenderTargets.hpp"
#include "renderer/UIRenderer.hpp"
#include "app/RenderOrchestrator.hpp"
#include "app/TimeSync.hpp"
#include "app/CrystalMath.hpp"
#include "gs/GsConstants.hpp"
#include <chrono>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <iostream>
#include <array>
#include <vector>

struct SwapchainSync {
    std::vector<VkSemaphore> acquireSems;
    std::vector<VkSemaphore> renderSems;
    uint32_t acquireIndex = 0;

    static SwapchainSync create(VkDevice device, uint32_t imageCount) {
        SwapchainSync sync;
        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        sync.acquireSems.resize(imageCount + 1);
        for (auto& sem : sync.acquireSems)
            vkCreateSemaphore(device, &semInfo, nullptr, &sem);
        sync.renderSems.resize(imageCount);
        for (auto& sem : sync.renderSems)
            vkCreateSemaphore(device, &semInfo, nullptr, &sem);
        sync.acquireIndex = 0;
        return sync;
    }
    void destroy(VkDevice device) {
        for (auto sem : acquireSems) vkDestroySemaphore(device, sem, nullptr);
        for (auto sem : renderSems) vkDestroySemaphore(device, sem, nullptr);
        acquireSems.clear();
        renderSems.clear();
    }
    VkSemaphore nextAcquireSemaphore() {
        VkSemaphore sem = acquireSems[acquireIndex];
        acquireIndex = (acquireIndex + 1) % static_cast<uint32_t>(acquireSems.size());
        return sem;
    }
    VkSemaphore renderSemaphoreForImage(uint32_t imageIndex) const {
        return renderSems[imageIndex];
    }
};

static void transitionDepthImage(const VulkanContext& vulkan, FrameData& frame, AllocatedImage& img) {
    vkWaitForFences(vulkan.device(), 1, &frame.renderFence, VK_TRUE, UINT64_MAX);
    vkResetFences(vulkan.device(), 1, &frame.renderFence);
    vkResetCommandBuffer(frame.commandBuffer, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(frame.commandBuffer, &bi);

    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    barrier.image = img.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(frame.commandBuffer, &dep);

    vkEndCommandBuffer(frame.commandBuffer);

    VkCommandBufferSubmitInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    ci.commandBuffer = frame.commandBuffer;
    VkSubmitInfo2 si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &ci;
    vkQueueSubmit2(vulkan.graphicsQueue(), 1, &si, frame.renderFence);
    vkWaitForFences(vulkan.device(), 1, &frame.renderFence, VK_TRUE, UINT64_MAX);
}

int main(int argc, char* argv[]) {
    try {
        WindowContext window;
        VulkanContext vulkan(window);
        SwapchainManager swapchain(vulkan, 1280, 720);
        ResourceManager resources(vulkan);
        UIRenderer ui(vulkan, window.getHandle(), swapchain.imageFormat());
        RenderOrchestrator orchestrator;
        RenderDocWrapper rdoc;
        rdoc.init();

        orchestrator.init(vulkan, swapchain, resources);

        std::array<FrameData, FrameOverlap> frames;
        for (auto& frame : frames) {
            frame = FrameData::create(vulkan.device(), vulkan.graphicsQueueFamily());
        }
        auto swapSync = SwapchainSync::create(vulkan.device(), swapchain.imageCount());

        RenderTargets targets;
        targets.create(resources, swapchain.extent(), swapchain.imageFormat());

        transitionDepthImage(vulkan, frames[0], targets.depth);

        uint32_t frameNumber = 0;
        bool resizeRequested = false;
        TestSceneParams testParams{};
        glm::quat cubeRot{1.0f, 0.0f, 0.0f, 0.0f};
        float cubeScale = 1.0f;
        bool cubeAutoRotate = false;
        auto appStartTime = std::chrono::high_resolution_clock::now();
        auto lastFrameTime = appStartTime;
        float fps = 0.0f;

        try {
        while (!window.shouldClose()) {
            window.pollEvents();

            auto now = std::chrono::high_resolution_clock::now();
            float dt = std::chrono::duration<float>(now - lastFrameTime).count();
            lastFrameTime = now;
            fps = fps * 0.95f + (1.0f / std::max(dt, 0.0001f)) * 0.05f;

            int w, h;
            SDL_GetWindowSize(window.getHandle(), &w, &h);
            if (w == 0 || h == 0 ||
                (SDL_GetWindowFlags(window.getHandle()) & SDL_WINDOW_MINIMIZED)) {
                SDL_Delay(50);
                continue;
            }

            if (resizeRequested) {
                VkSurfaceCapabilitiesKHR surfCaps{};
                vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                    vulkan.physicalDevice(), vulkan.surface(), &surfCaps);
                if (surfCaps.currentExtent.width == 0 || surfCaps.currentExtent.height == 0 ||
                    surfCaps.maxImageExtent.width == 0 || surfCaps.maxImageExtent.height == 0) {
                    SDL_Delay(50);
                    continue;
                }
                vkDeviceWaitIdle(vulkan.device());
                swapchain.recreate(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
                swapSync.destroy(vulkan.device());
                swapSync = SwapchainSync::create(vulkan.device(), swapchain.imageCount());

                targets.destroy(resources);
                targets.create(resources, swapchain.extent(), swapchain.imageFormat());

                transitionDepthImage(vulkan, frames[0], targets.depth);
                resizeRequested = false;
            }

            auto& frame = frames[frameNumber % FrameOverlap];
            vkWaitForFences(vulkan.device(), 1, &frame.renderFence, VK_TRUE, UINT64_MAX);

            VkSemaphore acquireSem = swapSync.nextAcquireSemaphore();
            VkResult acquireResult = swapchain.acquireNextImage(acquireSem);
            if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
                resizeRequested = true;
                continue;
            }

            uint32_t imageIdx = swapchain.imageIndex();
            VkSemaphore renderSem = swapSync.renderSemaphoreForImage(imageIdx);

            ui.beginFrame();

            vkResetFences(vulkan.device(), 1, &frame.renderFence);
            vkResetCommandBuffer(frame.commandBuffer, 0);

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(frame.commandBuffer, &beginInfo);

            PassRecorder recorder(frame.commandBuffer);

            TimeInfo timeInfo = TimeSync::getCurrentTime();

            FrameParams params{};
            params.time = timeInfo;
            params.extent = swapchain.extent();
            params.aspect = static_cast<float>(params.extent.width) / static_cast<float>(params.extent.height);
            params.totalTime = std::chrono::duration<float>(now - appStartTime).count();
            params.device = vulkan.device();
            params.currentImageView = targets.mainColor.imageView;
            params.tunnelImageView = targets.tunnel.imageView;
            params.frameIndex = frameNumber % 2;

            if (testParams.enabled) {
                ImGuiIO& io = ImGui::GetIO();
                if (!io.WantCaptureMouse) {
                    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
                        float sens = 0.008f;
                        glm::quat yaw = glm::angleAxis(io.MouseDelta.x * sens, glm::vec3(0, 1, 0));
                        glm::quat pitch = glm::angleAxis(io.MouseDelta.y * sens, glm::vec3(1, 0, 0));
                        cubeRot = glm::normalize(pitch * yaw * cubeRot);
                    }
                    if (io.MouseWheel != 0.0f) {
                        cubeScale = std::clamp(cubeScale * std::exp(io.MouseWheel * 0.1f), 0.1f, 5.0f);
                    }
                }
                if (cubeAutoRotate) {
                    cubeRot = glm::normalize(glm::angleAxis(dt * 0.5f, glm::vec3(0, 1, 0)) * cubeRot);
                }
                testParams.cubeModel = glm::mat4_cast(cubeRot) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(cubeScale));
            }

            // Update UBO with viewProj, viewPos, prismColor
            orchestrator.updateUBO(params, &testParams);

            // ═══════════════════════════════════════════════════════════════
            // PASS A: Render tunnel to targets.tunnel
            // ═══════════════════════════════════════════════════════════════
            recorder.transitionImage(targets.tunnel.image,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

            VkClearValue tunnelClear{};
            tunnelClear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

            recorder.beginDebugLabel(testParams.enabled ? "Test Background" : "Tunnel Background", 0.2f, 0.2f, 0.6f);
            recorder.beginRendering(targets.tunnel.imageView, targets.depth.imageView,
                                    swapchain.extent(), &tunnelClear);
            recorder.setViewportScissor(swapchain.extent());

            if (testParams.enabled)
                orchestrator.recordTestBackgroundPass(recorder, params, testParams);
            else
                orchestrator.recordTunnelPass(recorder, params);

            recorder.endRendering();
            recorder.endDebugLabel();

            // Transition tunnel to SHADER_READ_ONLY for crystal refraction sampling
            recorder.transitionImage(targets.tunnel.image,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            // ═══════════════════════════════════════════════════════════════
            // PASS B: Render crystals to targets.mainColor, sampling targets.tunnel
            // ═══════════════════════════════════════════════════════════════

            // Copy tunnel content to main color image as base (so crystals blend ON TOP of tunnel)
            recorder.transitionImage(targets.mainColor.image,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            recorder.transitionImage(targets.tunnel.image,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            VkImageCopy tunnelCopy{};
            tunnelCopy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            tunnelCopy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            tunnelCopy.extent = {swapchain.extent().width, swapchain.extent().height, 1};
            vkCmdCopyImage(frame.commandBuffer,
                           targets.tunnel.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           targets.mainColor.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &tunnelCopy);

            // Transition for the crystal rendering pass
            recorder.transitionImage(targets.tunnel.image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            recorder.transitionImage(targets.mainColor.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

            VkClearValue crystalClear{};
            // Don't clear — we just copied the tunnel content

            recorder.beginDebugLabel(testParams.enabled ? "Test Cube" : "Crystal Clock (Pass 1)", 0.2f, 0.4f, 1.0f);
            recorder.beginRendering(targets.mainColor.imageView, targets.depth.imageView,
                                    swapchain.extent(), nullptr);
            recorder.setViewportScissor(swapchain.extent());

            if (testParams.enabled)
                orchestrator.recordTestCubePass(recorder, params, testParams);
            else
                orchestrator.recordCrystalPasses(recorder, params);

            recorder.endRendering();
            recorder.endDebugLabel();

            // ═══════════════════════════════════════════════════════════════
            // Inter-rod refraction: copy mainColor → targets.tunnel,
            // then re-render rods refracting now-updated bg (rods + tunnel).
            // ═══════════════════════════════════════════════════════════════
            if (!testParams.enabled) {
            recorder.transitionImage(targets.mainColor.image,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            recorder.transitionImage(targets.tunnel.image,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkImageCopy interRodCopy{};
            interRodCopy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            interRodCopy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            interRodCopy.extent = {swapchain.extent().width, swapchain.extent().height, 1};
            vkCmdCopyImage(frame.commandBuffer,
                           targets.mainColor.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           targets.tunnel.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &interRodCopy);

            recorder.transitionImage(targets.tunnel.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            recorder.transitionImage(targets.mainColor.image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

            recorder.beginDebugLabel("Crystal Clock (Pass 2: Inter-Rod)", 0.4f, 0.6f, 1.0f);
            recorder.beginRendering(targets.mainColor.imageView, targets.depth.imageView,
                                    swapchain.extent(), nullptr);
            recorder.setViewportScissor(swapchain.extent());

            orchestrator.recordCrystalPasses(recorder, params);

            recorder.endRendering();
            recorder.endDebugLabel();
            }

            // ImGui overlay
            int hlRod = CrystalMath::getHighlightedRod(timeInfo.hour);
            int hourCounter = static_cast<int>(timeInfo.minute * 60 + timeInfo.secondsInMinute);
            bool isWide = params.aspect > 1.5f;
            int screenRatio = isWide ? GsConstants::SCREEN_RATIO_16_9 : GsConstants::SCREEN_RATIO_4_3;
            float fillAmt = CrystalMath::computeRodScale(hlRod, 1.0f, isWide, screenRatio, true, hourCounter);

            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(280, 240), ImGuiCond_FirstUseEver);
            ImGui::Begin("CrystalClock Debug");
            ImGui::Text("FPS: %.1f", fps);
            ImGui::Separator();

            if (rdoc.isLoaded()) {
                if (ImGui::Button("Trigger RenderDoc Capture")) {
                    rdoc.triggerCapture();
                }
                ImGui::Separator();
            }

            ImGui::Text("Time: %02d:%02d:%02d.%03d", timeInfo.hour, timeInfo.minute, timeInfo.second, timeInfo.millisecond);
            ImGui::Text("Highlighted Rod: %d", hlRod);
            ImGui::Text("Hour Scale Slide: %.3f", fillAmt);
            ImGui::Text("Sec in Min: %.2f", timeInfo.secondsInMinute);
            ImGui::Separator();
            ImGui::Text("Tunnel(1) + Glass(12) + Spec(12) + Fill(1)");
            ImGui::Text("Draw Calls: %d", 1 + 12 + 12 + 1);
            ImGui::End();

            ImGui::SetNextWindowPos(ImVec2(10, 260), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(340, 560), ImGuiCond_FirstUseEver);
            ImGui::Begin("Refraction Test Scene");
            ImGui::Checkbox("Enable Test Scene", &testParams.enabled);
            if (testParams.enabled) {
                ImGui::TextDisabled("LMB drag: rotate cube | Wheel: scale");

                if (ImGui::CollapsingHeader("Background", ImGuiTreeNodeFlags_DefaultOpen)) {
                    const char* bgModes[] = {"Stripes (Vertical)", "Stripes (Horizontal)", "Checkerboard", "Grid"};
                    ImGui::Combo("Pattern", &testParams.bgMode, bgModes, 4);
                    ImGui::SliderFloat("Scale", &testParams.bgScale, 1.0f, 64.0f);
                    ImGui::SliderFloat("Scroll Speed", &testParams.bgScrollSpeed, 0.0f, 2.0f);
                    ImGui::ColorEdit3("Color 1", &testParams.bgColor1.x);
                    ImGui::ColorEdit3("Color 2", &testParams.bgColor2.x);
                }

                if (ImGui::CollapsingHeader("Refraction", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::SliderFloat("Eta (IOR ratio)", &testParams.eta, 0.0f, 1.5f);
                    ImGui::SliderFloat("Offset Scale", &testParams.refractScale, 0.0f, 10.0f);
                    ImGui::SliderFloat("Emissive Boost", &testParams.refractBoost, 0.0f, 8.0f);
                    ImGui::SliderFloat("Rim Strength", &testParams.rimStrength, 0.0f, 4.0f);
                    ImGui::SliderFloat("Emissive Base", &testParams.emissiveBase, 0.0f, 1.0f);
                }

                if (ImGui::CollapsingHeader("Composition", ImGuiTreeNodeFlags_DefaultOpen)) {
                    const char* compModes[] = {"Front (opaque)", "Back (scene mix)"};
                    ImGui::Combo("Mode", &testParams.composition, compModes, 2);
                    ImGui::SliderFloat("Diffuse Mix", &testParams.diffuseMix, 0.0f, 1.0f);
                    ImGui::SliderFloat("Reflect Strength", &testParams.reflectStrength, 0.0f, 2.0f);
                    ImGui::SliderFloat("Fade Alpha", &testParams.fadeAlpha, 0.0f, 1.0f);
                }

                if (ImGui::CollapsingHeader("Tint", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("Animate", &testParams.animateTint);
                    if (testParams.animateTint)
                        ImGui::SliderFloat("Period (s)", &testParams.colorPeriod, 1.0f, 60.0f);
                    else
                        ImGui::SliderFloat("Lerp", &testParams.tintLerp, 0.0f, 1.0f);
                    ImGui::ColorEdit3("Tint 1", &testParams.tint1.x);
                    ImGui::ColorEdit3("Tint 2", &testParams.tint2.x);
                }

                if (ImGui::CollapsingHeader("Cube", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("Auto Rotate", &cubeAutoRotate);
                    ImGui::SliderFloat("Size", &cubeScale, 0.1f, 5.0f);
                    if (ImGui::Button("Reset Rotation")) {
                        cubeRot = glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
                        cubeScale = 1.0f;
                    }
                }
            }
            ImGui::End();

            // Render ImGui to targets.mainColor (already in COLOR_ATTACHMENT_OPTIMAL)
            ui.render(frame.commandBuffer, targets.mainColor.imageView, swapchain.extent());

            // Blit targets.mainColor to swapchain
            recorder.transitionImage(targets.mainColor.image,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            recorder.transitionImage(swapchain.currentImage(),
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkImageCopy copyRegion{};
            copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.extent = {swapchain.extent().width, swapchain.extent().height, 1};
            vkCmdCopyImage(frame.commandBuffer,
                           targets.mainColor.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           swapchain.currentImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copyRegion);

            recorder.transitionImage(swapchain.currentImage(),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

            vkEndCommandBuffer(frame.commandBuffer);

            VkCommandBufferSubmitInfo cmdSubmitInfo{};
            cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            cmdSubmitInfo.commandBuffer = frame.commandBuffer;

            VkSemaphoreSubmitInfo waitSemInfo{};
            waitSemInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            waitSemInfo.semaphore = acquireSem;
            waitSemInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

            VkSemaphoreSubmitInfo signalSemInfo{};
            signalSemInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signalSemInfo.semaphore = renderSem;
            signalSemInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

            VkSubmitInfo2 submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submitInfo.commandBufferInfoCount = 1;
            submitInfo.pCommandBufferInfos = &cmdSubmitInfo;
            submitInfo.waitSemaphoreInfoCount = 1;
            submitInfo.pWaitSemaphoreInfos = &waitSemInfo;
            submitInfo.signalSemaphoreInfoCount = 1;
            submitInfo.pSignalSemaphoreInfos = &signalSemInfo;

            vkQueueSubmit2(vulkan.graphicsQueue(), 1, &submitInfo, frame.renderFence);

            VkResult presentResult = swapchain.present(renderSem);
            if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
                resizeRequested = true;
            }

            frameNumber++;
        }
        } catch (const std::exception& e) {
            std::cerr << "Render loop error: " << e.what() << std::endl;
        }

        vkDeviceWaitIdle(vulkan.device());

        orchestrator.destroy(vulkan.device(), resources);
        resources.destroyImage(targets.depth);
        resources.destroyImage(targets.tunnel);
        resources.destroyImage(targets.mainColor);

        swapSync.destroy(vulkan.device());
        for (auto& frame : frames) {
            frame.destroy(vulkan.device());
        }

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
