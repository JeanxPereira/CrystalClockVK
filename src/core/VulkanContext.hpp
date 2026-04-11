#pragma once

#include "WindowContext.hpp"
#include <vulkan/vulkan.h>
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>

class VulkanContext {
public:
    explicit VulkanContext(const WindowContext& window);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VkDevice device() const { return m_device.device; }
    VkPhysicalDevice physicalDevice() const { return m_device.physical_device.physical_device; }
    VkInstance instance() const { return m_instance.instance; }
    VkSurfaceKHR surface() const { return m_surface; }
    VmaAllocator allocator() const { return m_allocator; }

    VkQueue graphicsQueue() const { return m_graphicsQueue; }
    uint32_t graphicsQueueFamily() const { return m_graphicsQueueFamily; }

    VkQueue presentQueue() const { return m_presentQueue; }
    uint32_t presentQueueFamily() const { return m_presentQueueFamily; }

    const vkb::Device& vkbDevice() const { return m_device; }
    const vkb::PhysicalDevice& vkbPhysicalDevice() const { return m_device.physical_device; }

private:
    vkb::Instance m_instance;
    vkb::Device m_device;
    VkSurfaceKHR m_surface{VK_NULL_HANDLE};
    VmaAllocator m_allocator{VK_NULL_HANDLE};

    VkQueue m_graphicsQueue{VK_NULL_HANDLE};
    uint32_t m_graphicsQueueFamily{0};
    VkQueue m_presentQueue{VK_NULL_HANDLE};
    uint32_t m_presentQueueFamily{0};
    
public:
    void setDebugName(VkObjectType type, uint64_t handle, const char* name) const;
};
