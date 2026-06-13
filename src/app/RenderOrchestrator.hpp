#pragma once

#include "core/VulkanContext.hpp"
#include "renderer/SwapchainManager.hpp"
#include "renderer/ResourceManager.hpp"
#include "renderer/PassRecorder.hpp"
#include "app/TimeSync.hpp"

struct FrameParams {
    TimeInfo time;
    VkExtent2D extent;
    float aspect;
    float totalTime;
    VkDevice device;
    VkImageView currentImageView;
    uint32_t frameIndex;
};

class RenderOrchestrator {
public:
    void init(const VulkanContext& ctx, const SwapchainManager& swapchain, ResourceManager& resources);
    void recordFrame(PassRecorder& recorder, const FrameParams& params);
    void updateUBO(const FrameParams& params);
    void destroy(VkDevice device, ResourceManager& resources);
};
