#include "app/Engine.hpp"
#include "app/TimeSync.hpp"
#include "app/CrystalMath.hpp"
#include "gs/GsConstants.hpp"
#include <algorithm>
#include <imgui.h>
#include <iostream>

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

Engine::Engine()
    : m_window()
    , m_vulkan(m_window)
    , m_swapchain(m_vulkan, 1280, 720)
    , m_resources(m_vulkan)
    , m_ui(m_vulkan, m_window.getHandle(), m_swapchain.imageFormat())
    , m_orchestrator()
    , m_rdoc()
{
    m_rdoc.init();

    m_orchestrator.init(m_vulkan, m_swapchain, m_resources);

    for (auto& frame : m_frames) {
        frame = FrameData::create(m_vulkan.device(), m_vulkan.graphicsQueueFamily());
    }

    m_targets.create(m_resources, m_swapchain.extent(), m_swapchain.imageFormat());

    transitionDepthImage(m_vulkan, m_frames[0], m_targets.depth);

    m_appStartTime = std::chrono::high_resolution_clock::now();
    m_lastFrameTime = m_appStartTime;
}

Engine::~Engine() {
    if (m_vulkan.device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_vulkan.device());

        m_orchestrator.destroy(m_vulkan.device(), m_resources);
        m_targets.destroy(m_resources);

        for (auto& frame : m_frames) {
            frame.destroy(m_vulkan.device());
        }
    }
}

void Engine::run() {
    try {
        while (!m_window.shouldClose()) {
            m_window.pollEvents();
            runFrame();
        }
    } catch (const std::exception& e) {
        std::cerr << "Render loop error: " << e.what() << std::endl;
    }
}

void Engine::runFrame() {
    auto now = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration<float>(now - m_lastFrameTime).count();
    m_lastFrameTime = now;
    m_fps = m_fps * 0.95f + (1.0f / std::max(dt, 0.0001f)) * 0.05f;

    FrameStatus fs = m_swapchain.beginFrame(m_window.getHandle());
    if (fs == FrameStatus::SkipFrame) return;
    if (fs == FrameStatus::Recreated) {
        m_targets.destroy(m_resources);
        m_targets.create(m_resources, m_swapchain.extent(), m_swapchain.imageFormat());
        transitionDepthImage(m_vulkan, m_frames[0], m_targets.depth);
    }

    auto& frame = m_frames[m_frameNumber % FrameOverlap];
    vkWaitForFences(m_vulkan.device(), 1, &frame.renderFence, VK_TRUE, UINT64_MAX);

    VkSemaphore acquireSem = m_swapchain.acquireSemaphore();
    VkSemaphore renderSem = m_swapchain.renderSemaphore();

    m_ui.beginFrame();

    vkResetFences(m_vulkan.device(), 1, &frame.renderFence);
    vkResetCommandBuffer(frame.commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(frame.commandBuffer, &beginInfo);

    PassRecorder recorder(frame.commandBuffer);
    recorder.resetLayoutTracking();

    TimeInfo timeInfo = TimeSync::getCurrentTime();

    FrameParams params{};
    params.time = timeInfo;
    params.extent = m_swapchain.extent();
    params.aspect = static_cast<float>(params.extent.width) / static_cast<float>(params.extent.height);
    params.totalTime = std::chrono::duration<float>(now - m_appStartTime).count();
    params.device = m_vulkan.device();
    params.currentImageView = m_targets.mainColor.imageView;
    params.tunnelImageView = m_targets.tunnel.imageView;
    params.frameIndex = m_frameNumber % 2;

    VkExtent2D ext = m_swapchain.extent();

    m_testScene.params().enabled = m_testSceneActive;

    if (m_testSceneActive) {
        m_testScene.update(params, dt);
        m_orchestrator.updateUBO(params, &m_testScene.params());
        m_testScene.record(recorder, params, m_targets);
    } else {
        m_orchestrator.updateUBO(params, nullptr);

        VkClearValue clear{};
        clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

        recorder.runPass({"Tunnel Background", {0.2f, 0.2f, 0.6f},
                          &m_targets.tunnel, &m_targets.depth, {}, &clear, ext}, [&] {
            m_orchestrator.recordTunnelPass(recorder, params);
        });

        recorder.copyImage(m_targets.tunnel, m_targets.mainColor, ext);

        recorder.runPass({"Crystal Clock (Pass 1)", {0.2f, 0.4f, 1.0f},
                          &m_targets.mainColor, &m_targets.depth,
                          {&m_targets.tunnel}, nullptr, ext}, [&] {
            m_orchestrator.recordCrystalPasses(recorder, params);
        });

        recorder.copyImage(m_targets.mainColor, m_targets.tunnel, ext);
        recorder.runPass({"Crystal Clock (Pass 2: Inter-Rod)", {0.4f, 0.6f, 1.0f},
                          &m_targets.mainColor, &m_targets.depth, {&m_targets.tunnel}, nullptr, ext}, [&] {
            m_orchestrator.recordCrystalPasses(recorder, params);
        });
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
    ImGui::Text("FPS: %.1f", m_fps);
    ImGui::Separator();

    if (m_rdoc.isLoaded()) {
        if (ImGui::Button("Trigger RenderDoc Capture")) {
            m_rdoc.triggerCapture();
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
    ImGui::Separator();
    ImGui::Checkbox("Enable Test Scene", &m_testSceneActive);
    ImGui::End();

    if (m_testSceneActive) m_testScene.drawUI();

    // Render ImGui to targets.mainColor (already in COLOR_ATTACHMENT_OPTIMAL)
    m_ui.render(frame.commandBuffer, m_targets.mainColor.imageView, m_swapchain.extent());

    // Blit targets.mainColor to swapchain
    recorder.transitionImage(m_targets.mainColor.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    recorder.transitionImage(m_swapchain.currentImage(),
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkImageCopy copyRegion{};
    copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.extent = {m_swapchain.extent().width, m_swapchain.extent().height, 1};
    vkCmdCopyImage(frame.commandBuffer,
                   m_targets.mainColor.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   m_swapchain.currentImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &copyRegion);

    recorder.transitionImage(m_swapchain.currentImage(),
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

    vkQueueSubmit2(m_vulkan.graphicsQueue(), 1, &submitInfo, frame.renderFence);

    bool dummy;
    m_swapchain.endFrame(dummy);

    m_frameNumber++;
}
