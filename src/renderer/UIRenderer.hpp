#pragma once

#include "core/VulkanContext.hpp"
#include "renderer/SwapchainManager.hpp"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

class UIRenderer {
public:
    UIRenderer(const VulkanContext& ctx, SDL_Window* window, VkFormat swapchainFormat);
    ~UIRenderer();

    UIRenderer(const UIRenderer&) = delete;
    UIRenderer& operator=(const UIRenderer&) = delete;

    void beginFrame();
    void render(VkCommandBuffer cmd, VkImageView targetView, VkExtent2D extent);

private:
    const VulkanContext& m_ctx;
    VkDescriptorPool m_imguiPool{VK_NULL_HANDLE};
    VkFormat m_format;
};
