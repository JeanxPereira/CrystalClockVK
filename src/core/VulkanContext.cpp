#include "VulkanContext.hpp"
#include <SDL3/SDL_vulkan.h>
#include <stdexcept>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

VulkanContext::VulkanContext(const WindowContext& window) {
    vkb::InstanceBuilder builder;

    auto instanceRet = builder.set_app_name("CrystalClockVK")
        .request_validation_layers(true)
        .use_default_debug_messenger()
        .require_api_version(1, 3, 0)
        .build();

    if (!instanceRet) {
        throw std::runtime_error(instanceRet.error().message());
    }
    m_instance = instanceRet.value();

    if (!SDL_Vulkan_CreateSurface(window.getHandle(), m_instance.instance, nullptr, &m_surface)) {
        throw std::runtime_error(SDL_GetError());
    }

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    features13.shaderDemoteToHelperInvocation = VK_TRUE;  // fragment shader `discard`

    VkPhysicalDeviceDynamicRenderingLocalReadFeaturesKHR localReadFeatures{};
    localReadFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_LOCAL_READ_FEATURES_KHR;
    localReadFeatures.dynamicRenderingLocalRead = VK_TRUE;

    // dualSrcBlend: required by the gsvk blend recipes (SRC1 = C/128 in-shader).
    VkPhysicalDeviceFeatures features10{};
    features10.dualSrcBlend = VK_TRUE;

    vkb::PhysicalDeviceSelector selector{m_instance};
    auto physRet = selector.set_surface(m_surface)
        .set_minimum_version(1, 3)
        .set_required_features(features10)
        .set_required_features_13(features13)
        .add_required_extension(VK_KHR_DYNAMIC_RENDERING_LOCAL_READ_EXTENSION_NAME)
        .add_required_extension_features(localReadFeatures)
        .require_present()
        .select();

    if (!physRet) {
        throw std::runtime_error(physRet.error().message());
    }

    vkb::DeviceBuilder deviceBuilder{physRet.value()};
    auto devRet = deviceBuilder.build();

    if (!devRet) {
        throw std::runtime_error(devRet.error().message());
    }
    m_device = devRet.value();

    auto graphicsQueueRet = m_device.get_queue(vkb::QueueType::graphics);
    if (!graphicsQueueRet) {
        throw std::runtime_error(graphicsQueueRet.error().message());
    }
    m_graphicsQueue = graphicsQueueRet.value();
    m_graphicsQueueFamily = m_device.get_queue_index(vkb::QueueType::graphics).value();

    auto presentQueueRet = m_device.get_queue(vkb::QueueType::present);
    if (!presentQueueRet) {
        throw std::runtime_error(presentQueueRet.error().message());
    }
    m_presentQueue = presentQueueRet.value();
    m_presentQueueFamily = m_device.get_queue_index(vkb::QueueType::present).value();

    VmaAllocatorCreateInfo allocInfo{};
    allocInfo.physicalDevice = physicalDevice();
    allocInfo.device = device();
    allocInfo.instance = instance();
    allocInfo.flags = 0;
    allocInfo.vulkanApiVersion = VK_API_VERSION_1_3;

    if (vmaCreateAllocator(&allocInfo, &m_allocator) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VMA allocator");
    }
}

VulkanContext::~VulkanContext() {
    if (m_allocator) {
        vmaDestroyAllocator(m_allocator);
    }
    vkb::destroy_device(m_device);
    if (m_surface) {
        vkb::destroy_surface(m_instance, m_surface);
    }
    vkb::destroy_instance(m_instance);
}

void VulkanContext::setDebugName(VkObjectType type, uint64_t handle, const char* name) const {
    auto func = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(m_device.device, "vkSetDebugUtilsObjectNameEXT");
    if (func != nullptr) {
        VkDebugUtilsObjectNameInfoEXT nameInfo{};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = type;
        nameInfo.objectHandle = handle;
        nameInfo.pObjectName = name;
        func(m_device.device, &nameInfo);
    }
}
