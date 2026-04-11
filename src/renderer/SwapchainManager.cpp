#include "SwapchainManager.hpp"
#include <VkBootstrap.h>
#include <stdexcept>

SwapchainManager::SwapchainManager(const VulkanContext& ctx, uint32_t width, uint32_t height)
    : m_ctx(ctx) {
    create(width, height);
}

SwapchainManager::~SwapchainManager() {
    destroy();
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
