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
#include "app/IScene.hpp"
#include "app/TestScene.hpp"
#include "app/ClockScene.hpp"
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

    TestScene m_testScene{m_orchestrator};
    ClockScene m_clockScene{m_orchestrator};
    IScene* m_activeScene{&m_clockScene};
    bool m_testSceneActive{false};

    std::chrono::high_resolution_clock::time_point m_appStartTime;
    std::chrono::high_resolution_clock::time_point m_lastFrameTime;
    float m_fps{0.0f};
    uint32_t m_frameNumber{0};
};
