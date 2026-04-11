#pragma once

#include "core/VulkanContext.hpp"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <functional>

// GPU-allocated buffer with its VMA allocation handle
struct AllocatedBuffer {
    VkBuffer buffer{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    VmaAllocationInfo allocationInfo{};
    VkDeviceSize size{0};
};

// GPU-allocated image with its VMA allocation handle
struct AllocatedImage {
    VkImage image{VK_NULL_HANDLE};
    VkImageView imageView{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    VkExtent2D extent{};
    VkFormat format{VK_FORMAT_UNDEFINED};
};

// Manages GPU resource allocation via VMA, including staging uploads.
class ResourceManager {
public:
    explicit ResourceManager(const VulkanContext& ctx);
    ~ResourceManager();

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    // Buffer creation
    AllocatedBuffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memUsage);

    // Image creation (with view)
    AllocatedImage createImage(VkExtent2D extent, VkFormat format,
                               VkImageUsageFlags usage, VmaMemoryUsage memUsage = VMA_MEMORY_USAGE_GPU_ONLY);

    // Upload data to a GPU buffer via staging
    void uploadToBuffer(const AllocatedBuffer& dst, const void* data, VkDeviceSize size);

    // Upload pixel data to a GPU image via staging + layout transition
    void uploadToImage(const AllocatedImage& dst, const void* data, VkExtent2D extent, VkFormat format);

    // Resource destruction
    void destroyBuffer(AllocatedBuffer& buffer);
    void destroyImage(AllocatedImage& image);

private:
    // One-shot command buffer for staging uploads
    void immediateSubmit(std::function<void(VkCommandBuffer)>&& fn);

    const VulkanContext& m_ctx;
    VkCommandPool m_uploadPool{VK_NULL_HANDLE};
    VkFence m_uploadFence{VK_NULL_HANDLE};
};
