#include "core/WindowContext.hpp"
#include "core/VulkanContext.hpp"
#include "core/RenderDocWrapper.hpp"
#include "renderer/SwapchainManager.hpp"
#include "renderer/FrameData.hpp"
#include "renderer/DeletionQueue.hpp"
#include "renderer/PassRecorder.hpp"
#include "renderer/ResourceManager.hpp"
#include "renderer/UIRenderer.hpp"
#include "app/RenderOrchestrator.hpp"
#include "app/TimeSync.hpp"
#include "app/CrystalMath.hpp"
#include <chrono>
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

        AllocatedImage depthImage = resources.createImage(
            swapchain.extent(),
            VK_FORMAT_D32_SFLOAT,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
        
        AllocatedImage mainColorImage = resources.createImage(
            swapchain.extent(),
            swapchain.imageFormat(),
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        
        transitionDepthImage(vulkan, frames[0], depthImage);
        
        // Initial transition for color image to speed up first frame
        vkWaitForFences(vulkan.device(), 1, &frames[0].renderFence, VK_TRUE, UINT64_MAX);
        vkResetFences(vulkan.device(), 1, &frames[0].renderFence);
        vkResetCommandBuffer(frames[0].commandBuffer, 0);

        {
            VkCommandBufferBeginInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(frames[0].commandBuffer, &bi);

            VkImageMemoryBarrier2 barrierColor{};
            barrierColor.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrierColor.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
            barrierColor.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrierColor.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
            barrierColor.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrierColor.newLayout = VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR;
            barrierColor.image = mainColorImage.image;
            barrierColor.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &barrierColor;
            vkCmdPipelineBarrier2(frames[0].commandBuffer, &dep);
            vkEndCommandBuffer(frames[0].commandBuffer);

            VkCommandBufferSubmitInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            ci.commandBuffer = frames[0].commandBuffer;
            VkSubmitInfo2 si{};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            si.commandBufferInfoCount = 1;
            si.pCommandBufferInfos = &ci;
            vkQueueSubmit2(vulkan.graphicsQueue(), 1, &si, frames[0].renderFence);
            vkWaitForFences(vulkan.device(), 1, &frames[0].renderFence, VK_TRUE, UINT64_MAX);
        }

        uint32_t frameNumber = 0;
        bool resizeRequested = false;
        auto appStartTime = std::chrono::high_resolution_clock::now();
        auto lastFrameTime = appStartTime;
        float fps = 0.0f;

        while (!window.shouldClose()) {
            window.pollEvents();
            ui.beginFrame();

            auto now = std::chrono::high_resolution_clock::now();
            float dt = std::chrono::duration<float>(now - lastFrameTime).count();
            lastFrameTime = now;
            fps = fps * 0.95f + (1.0f / std::max(dt, 0.0001f)) * 0.05f;

            int w, h;
            SDL_GetWindowSize(window.getHandle(), &w, &h);
            if (w == 0 || h == 0) continue;

            if (resizeRequested) {
                vkDeviceWaitIdle(vulkan.device());
                swapchain.recreate(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
                swapSync.destroy(vulkan.device());
                swapSync = SwapchainSync::create(vulkan.device(), swapchain.imageCount());

                resources.destroyImage(depthImage);
                resources.destroyImage(mainColorImage);
                
                depthImage = resources.createImage(
                    swapchain.extent(),
                    VK_FORMAT_D32_SFLOAT,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
                
                mainColorImage = resources.createImage(
                    swapchain.extent(),
                    swapchain.imageFormat(),
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
                    
                transitionDepthImage(vulkan, frames[0], depthImage);
                
                // Transition the newly created color image correctly
                vkResetFences(vulkan.device(), 1, &frames[0].renderFence);
                vkResetCommandBuffer(frames[0].commandBuffer, 0);
                {
                    VkCommandBufferBeginInfo bi{};
                    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                    vkBeginCommandBuffer(frames[0].commandBuffer, &bi);
                    
                    VkImageMemoryBarrier2 barrierColor{};
                    barrierColor.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                    barrierColor.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
                    barrierColor.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    barrierColor.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
                    barrierColor.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    barrierColor.newLayout = VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR;
                    barrierColor.image = mainColorImage.image;
                    barrierColor.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

                    VkDependencyInfo depColor{};
                    depColor.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                    depColor.imageMemoryBarrierCount = 1;
                    depColor.pImageMemoryBarriers = &barrierColor;
                    vkCmdPipelineBarrier2(frames[0].commandBuffer, &depColor);

                    vkEndCommandBuffer(frames[0].commandBuffer);

                    VkCommandBufferSubmitInfo ci{};
                    ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
                    ci.commandBuffer = frames[0].commandBuffer;
                    VkSubmitInfo2 si{};
                    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
                    si.commandBufferInfoCount = 1;
                    si.pCommandBufferInfos = &ci;
                    vkQueueSubmit2(vulkan.graphicsQueue(), 1, &si, frames[0].renderFence);
                    vkWaitForFences(vulkan.device(), 1, &frames[0].renderFence, VK_TRUE, UINT64_MAX);
                }

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

            vkResetFences(vulkan.device(), 1, &frame.renderFence);
            vkResetCommandBuffer(frame.commandBuffer, 0);

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(frame.commandBuffer, &beginInfo);

            PassRecorder recorder(frame.commandBuffer);

            TimeInfo timeInfo = TimeSync::getCurrentTime();

            // Always ensure the color target is back to Local Read layout just in case
            recorder.transitionImage(mainColorImage.image,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR);

            VkClearValue clearColor{};
            clearColor.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

            recorder.beginDebugLabel("Crystal Clock", 0.2f, 0.4f, 1.0f);
            recorder.beginRendering(mainColorImage.imageView, depthImage.imageView,
                                    swapchain.extent(), &clearColor, VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR);
            recorder.setViewportScissor(swapchain.extent());

            FrameParams params{};
            params.time = timeInfo;
            params.extent = swapchain.extent();
            params.aspect = static_cast<float>(params.extent.width) / static_cast<float>(params.extent.height);
            params.totalTime = std::chrono::duration<float>(now - appStartTime).count();
            params.device = vulkan.device();
            params.currentImageView = mainColorImage.imageView;
            params.frameIndex = frameNumber % FrameOverlap;

            orchestrator.recordFrame(recorder, params);

            recorder.endRendering();
            recorder.endDebugLabel();

            // ImGui overlay
            int hlRod = CrystalMath::getHighlightedRod(timeInfo.hour);
            int hourCounter = static_cast<int>(timeInfo.minute * 60 + timeInfo.secondsInMinute);
            float fillAmt = CrystalMath::computeRodScale(hlRod, 1.0f, true, true, hourCounter);

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
            ImGui::Text("Tunnel + Glass(11) + Spec(11) + Shimmer(11) + P4/P5(2)");
            ImGui::Text("Draw Calls: %d", 1 + 11 + 11 + 11 + 2);
            ImGui::End();

            ui.render(frame.commandBuffer, mainColorImage.imageView, swapchain.extent());

            // Transition main image to transfer src
            recorder.transitionImage(mainColorImage.image,
                VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            // Transition swapchain to transfer dst
            recorder.transitionImage(swapchain.currentImage(),
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            // Blit / Copy
            VkImageCopy copyRegion{};
            copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.extent = {swapchain.extent().width, swapchain.extent().height, 1};
            vkCmdCopyImage(frame.commandBuffer, 
                           mainColorImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           swapchain.currentImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copyRegion);

            // Ready swapchain for presenting
            recorder.transitionImage(swapchain.currentImage(),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

            vkEndCommandBuffer(frame.commandBuffer);

            VkCommandBufferSubmitInfo cmdSubmitInfo{};
            cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            cmdSubmitInfo.commandBuffer = frame.commandBuffer;

            VkSemaphoreSubmitInfo waitSemInfo{};
            waitSemInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            waitSemInfo.semaphore = acquireSem;
            waitSemInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

            VkSemaphoreSubmitInfo signalSemInfo{};
            signalSemInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signalSemInfo.semaphore = renderSem;
            signalSemInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;

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

        vkDeviceWaitIdle(vulkan.device());

        orchestrator.destroy(vulkan.device(), resources);
        resources.destroyImage(depthImage);
        resources.destroyImage(mainColorImage);

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
