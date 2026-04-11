#pragma once

#include <vulkan/vulkan.h>

struct FrameData {
    VkCommandPool commandPool{VK_NULL_HANDLE};
    VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
    VkFence renderFence{VK_NULL_HANDLE};

    static FrameData create(VkDevice device, uint32_t queueFamily) {
        FrameData frame{};

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamily;
        vkCreateCommandPool(device, &poolInfo, nullptr, &frame.commandPool);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = frame.commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(device, &allocInfo, &frame.commandBuffer);

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(device, &fenceInfo, nullptr, &frame.renderFence);

        return frame;
    }

    void destroy(VkDevice device) {
        vkDestroyFence(device, renderFence, nullptr);
        vkDestroyCommandPool(device, commandPool, nullptr);
    }
};

constexpr uint32_t FrameOverlap = 2;
