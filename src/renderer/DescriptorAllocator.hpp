#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

// Pool-based descriptor set allocator with per-frame reset.
// Supports the descriptor types needed by the Crystal Clock pipeline:
//   - UNIFORM_BUFFER (per-object UBOs)
//   - COMBINED_IMAGE_SAMPLER (textures)
//   - INPUT_ATTACHMENT (local_read for refraction)
class DescriptorAllocator {
public:
    // Pool size ratio: how many descriptors of each type per pool
    struct PoolSizeRatio {
        VkDescriptorType type;
        float ratio; // multiplied by maxSets to get count
    };

    void init(VkDevice device, uint32_t initialSets, const std::vector<PoolSizeRatio>& ratios);
    void destroy();

    // Allocate a descriptor set from the current pool (grows automatically)
    VkDescriptorSet allocate(VkDescriptorSetLayout layout);

    // Reset all pools (call once per frame, at frame start)
    void resetPools();

private:
    VkDescriptorPool createPool(uint32_t setCount);
    VkDescriptorPool grabPool();

    VkDevice m_device{VK_NULL_HANDLE};
    std::vector<PoolSizeRatio> m_ratios;
    std::vector<VkDescriptorPool> m_fullPools;
    std::vector<VkDescriptorPool> m_readyPools;
    uint32_t m_setsPerPool{0};
};

// Helper to write descriptor updates
class DescriptorWriter {
public:
    DescriptorWriter& writeBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size,
                                   VkDeviceSize offset, VkDescriptorType type);
    DescriptorWriter& writeImage(uint32_t binding, VkImageView imageView, VkSampler sampler,
                                  VkImageLayout layout, VkDescriptorType type);

    void updateSet(VkDevice device, VkDescriptorSet set);
    void clear();

private:
    std::vector<VkDescriptorBufferInfo> m_bufferInfos;
    std::vector<VkDescriptorImageInfo> m_imageInfos;
    std::vector<VkWriteDescriptorSet> m_writes;
};
