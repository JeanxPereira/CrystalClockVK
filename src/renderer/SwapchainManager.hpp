#pragma once

#include "core/VulkanContext.hpp"
#include <SDL3/SDL.h>
#include <vector>

enum class FrameStatus { Ready, SkipFrame, Recreated };

struct SwapchainSync {
    std::vector<VkSemaphore> acquireSems;
    std::vector<VkSemaphore> renderSems;
    uint32_t acquireIndex = 0;

    static SwapchainSync create(VkDevice device, uint32_t imageCount);
    void destroy(VkDevice device);
    VkSemaphore nextAcquireSemaphore();
    VkSemaphore renderSemaphoreForImage(uint32_t imageIndex) const;
};

class SwapchainManager {
public:
    SwapchainManager(const VulkanContext& ctx, uint32_t width, uint32_t height);
    ~SwapchainManager();

    SwapchainManager(const SwapchainManager&) = delete;
    SwapchainManager& operator=(const SwapchainManager&) = delete;

    void recreate(uint32_t width, uint32_t height);

    FrameStatus beginFrame(SDL_Window* window);
    void endFrame(bool& outNeedsRecreate);

    VkResult acquireNextImage(VkSemaphore signalSemaphore);
    VkResult present(VkSemaphore waitSemaphore);

    VkSemaphore acquireSemaphore() const { return m_currentAcquireSem; }
    VkSemaphore renderSemaphore() const { return m_sync.renderSemaphoreForImage(m_imageIndex); }

    VkSwapchainKHR swapchain() const { return m_swapchain; }
    VkFormat imageFormat() const { return m_imageFormat; }
    VkExtent2D extent() const { return m_extent; }
    uint32_t imageIndex() const { return m_imageIndex; }

    VkImage currentImage() const { return m_images[m_imageIndex]; }
    VkImageView currentImageView() const { return m_imageViews[m_imageIndex]; }

    uint32_t imageCount() const { return static_cast<uint32_t>(m_images.size()); }

private:
    void create(uint32_t width, uint32_t height);
    void destroy();

    const VulkanContext& m_ctx;
    VkSwapchainKHR m_swapchain{VK_NULL_HANDLE};
    VkFormat m_imageFormat{VK_FORMAT_UNDEFINED};
    VkExtent2D m_extent{};
    uint32_t m_imageIndex{0};

    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;

    SwapchainSync m_sync;
    bool m_pendingRecreate{false};
    VkSemaphore m_currentAcquireSem{VK_NULL_HANDLE};
};
