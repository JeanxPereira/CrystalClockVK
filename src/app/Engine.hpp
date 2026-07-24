#pragma once

#include "core/WindowContext.hpp"
#include "core/VulkanContext.hpp"
#include "core/RenderDocWrapper.hpp"
#include "renderer/SwapchainManager.hpp"
#include "renderer/FrameData.hpp"
#include "renderer/ResourceManager.hpp"
#include "renderer/RenderTargets.hpp"
#include "renderer/UIRenderer.hpp"
#include "app/RenderOrchestrator.hpp"
#include <glm/gtc/quaternion.hpp>
#include <chrono>
#include <array>

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
    glm::quat m_cubeRot{1.0f, 0.0f, 0.0f, 0.0f};
    float m_cubeScale{1.0f};
    bool m_cubeAutoRotate{false};

    std::chrono::high_resolution_clock::time_point m_appStartTime;
    std::chrono::high_resolution_clock::time_point m_lastFrameTime;
    float m_fps{0.0f};
    uint32_t m_frameNumber{0};
};
