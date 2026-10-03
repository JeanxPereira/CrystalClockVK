#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <VkBootstrap.h>
#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <optional>
#include <vector>

namespace render {

struct DeviceOptions {
    bool validation;
};

class Device {
public:
    Device(SDL_Window* window, const DeviceOptions& options);
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    VkInstance instance() const { return m_instance.instance; }
    VkDevice device() const { return m_device.device; }
    VkPhysicalDevice physicalDevice() const { return m_device.physical_device.physical_device; }
    VmaAllocator allocator() const { return m_allocator; }
    VkQueue queue() const { return m_queue; }
    uint32_t queueFamily() const { return m_queueFamily; }
    VkFormat swapchainFormat() const { return m_swapchain.image_format; }
    VkExtent2D swapchainExtent() const { return m_swapchain.extent; }
    uint32_t swapchainImageCount() const { return static_cast<uint32_t>(m_images.size()); }
    bool wideLines() const { return m_wideLines; }
    float maxLineWidth() const { return m_maxLineWidth; }
    VkSampleCountFlags sampleCounts() const { return m_sampleCounts; }

    struct FrameContext {
        VkCommandBuffer cmd;
        VkImage image;
        VkImageView view;
        uint32_t index;
    };
    std::optional<FrameContext> beginFrame();
    void endFrame(const FrameContext& frame);
    uint32_t validationErrors() const { return m_errors.load(); }

private:
    static constexpr uint32_t kFramesInFlight = 2;
    struct Frame {
        VkCommandPool pool{VK_NULL_HANDLE};
        VkCommandBuffer cmd{VK_NULL_HANDLE};
        VkSemaphore acquired{VK_NULL_HANDLE};
        VkFence fence{VK_NULL_HANDLE};
    };

    bool createSwapchain();
    void destroySwapchainViews();
    void transition(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

    SDL_Window* m_window;
    vkb::Instance m_instance;
    vkb::Device m_device;
    VkSurfaceKHR m_surface{VK_NULL_HANDLE};
    vkb::Swapchain m_swapchain{};
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_views;
    std::vector<VkSemaphore> m_renderDone;
    VmaAllocator m_allocator{VK_NULL_HANDLE};
    VkQueue m_queue{VK_NULL_HANDLE};
    uint32_t m_queueFamily{0};
    std::array<Frame, kFramesInFlight> m_frames{};
    uint32_t m_frameSlot{0};
    bool m_hasSwapchain{false};
    bool m_wideLines{false};
    float m_maxLineWidth{1.0f};
    VkSampleCountFlags m_sampleCounts{VK_SAMPLE_COUNT_1_BIT};
    std::atomic<uint32_t> m_errors{0};
};

}  // namespace render
