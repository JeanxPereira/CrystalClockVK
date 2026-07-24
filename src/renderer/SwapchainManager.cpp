#include "SwapchainManager.hpp"
#include <VkBootstrap.h>
#include <stdexcept>

SwapchainSync SwapchainSync::create(VkDevice device, uint32_t imageCount) {
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

void SwapchainSync::destroy(VkDevice device) {
    for (auto sem : acquireSems) vkDestroySemaphore(device, sem, nullptr);
    for (auto sem : renderSems) vkDestroySemaphore(device, sem, nullptr);
    acquireSems.clear();
    renderSems.clear();
}

VkSemaphore SwapchainSync::nextAcquireSemaphore() {
    VkSemaphore sem = acquireSems[acquireIndex];
    acquireIndex = (acquireIndex + 1) % static_cast<uint32_t>(acquireSems.size());
    return sem;
}

VkSemaphore SwapchainSync::renderSemaphoreForImage(uint32_t imageIndex) const {
    return renderSems[imageIndex];
}

SwapchainManager::SwapchainManager(const VulkanContext& ctx, uint32_t width, uint32_t height)
    : m_ctx(ctx) {
    create(width, height);
    m_sync = SwapchainSync::create(m_ctx.device(), imageCount());
}

SwapchainManager::~SwapchainManager() {
    m_sync.destroy(m_ctx.device());
    destroy();
}

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

void SwapchainManager::create(uint32_t width, uint32_t height) {
    vkb::SwapchainBuilder builder{m_ctx.vkbDevice()};

    auto swapRet = builder
        .set_desired_format({VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(width, height)
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build();

    if (!swapRet) {
        throw std::runtime_error(swapRet.error().message());
    }

    auto vkbSwapchain = swapRet.value();
    m_swapchain = vkbSwapchain.swapchain;
    m_imageFormat = vkbSwapchain.image_format;
    m_extent = vkbSwapchain.extent;
    m_images = vkbSwapchain.get_images().value();
    m_imageViews = vkbSwapchain.get_image_views().value();
}

void SwapchainManager::destroy() {
    if (m_swapchain == VK_NULL_HANDLE) return;

    for (auto view : m_imageViews) {
        vkDestroyImageView(m_ctx.device(), view, nullptr);
    }
    m_imageViews.clear();
    m_images.clear();

    vkDestroySwapchainKHR(m_ctx.device(), m_swapchain, nullptr);
    m_swapchain = VK_NULL_HANDLE;
}

void SwapchainManager::recreate(uint32_t width, uint32_t height) {
    vkDeviceWaitIdle(m_ctx.device());
    destroy();
    create(width, height);
}

VkResult SwapchainManager::acquireNextImage(VkSemaphore signalSemaphore) {
    return vkAcquireNextImageKHR(
        m_ctx.device(), m_swapchain, UINT64_MAX,
        signalSemaphore, VK_NULL_HANDLE, &m_imageIndex
    );
}

VkResult SwapchainManager::present(VkSemaphore waitSemaphore) {
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &waitSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &m_imageIndex;

    return vkQueuePresentKHR(m_ctx.presentQueue(), &presentInfo);
}
