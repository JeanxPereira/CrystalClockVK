#include "DescriptorAllocator.hpp"
#include <stdexcept>

void DescriptorAllocator::init(VkDevice device, uint32_t initialSets, const std::vector<PoolSizeRatio>& ratios) {
    m_device = device;
    m_ratios = ratios;
    m_setsPerPool = initialSets;

    // Pre-create one pool
    VkDescriptorPool pool = createPool(initialSets);
    m_readyPools.push_back(pool);
}

void DescriptorAllocator::destroy() {
    for (auto pool : m_readyPools) {
        vkDestroyDescriptorPool(m_device, pool, nullptr);
    }
    for (auto pool : m_fullPools) {
        vkDestroyDescriptorPool(m_device, pool, nullptr);
    }
    m_readyPools.clear();
    m_fullPools.clear();
}

VkDescriptorSet DescriptorAllocator::allocate(VkDescriptorSetLayout layout) {
    VkDescriptorPool pool = grabPool();

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet set;
    VkResult result = vkAllocateDescriptorSets(m_device, &allocInfo, &set);

    if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
        // Current pool is full — retire it and try with a new one
        m_fullPools.push_back(pool);
        m_readyPools.pop_back();

        pool = grabPool();
        allocInfo.descriptorPool = pool;

        if (vkAllocateDescriptorSets(m_device, &allocInfo, &set) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate descriptor set from fresh pool");
        }
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor set");
    }

    return set;
}

void DescriptorAllocator::resetPools() {
    for (auto pool : m_readyPools) {
        vkResetDescriptorPool(m_device, pool, 0);
    }
    for (auto pool : m_fullPools) {
        vkResetDescriptorPool(m_device, pool, 0);
        m_readyPools.push_back(pool);
    }
    m_fullPools.clear();
}

VkDescriptorPool DescriptorAllocator::createPool(uint32_t setCount) {
    std::vector<VkDescriptorPoolSize> poolSizes;
    for (auto& ratio : m_ratios) {
        poolSizes.push_back({
            ratio.type,
            static_cast<uint32_t>(ratio.ratio * static_cast<float>(setCount))
        });
    }

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = setCount;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    VkDescriptorPool pool;
    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool");
    }
    return pool;
}

VkDescriptorPool DescriptorAllocator::grabPool() {
    if (!m_readyPools.empty()) {
        return m_readyPools.back();
    }
    // Grow: double the set count for next pool
    m_setsPerPool = static_cast<uint32_t>(static_cast<float>(m_setsPerPool) * 1.5f);
    if (m_setsPerPool > 4096) m_setsPerPool = 4096;

    VkDescriptorPool pool = createPool(m_setsPerPool);
    m_readyPools.push_back(pool);
    return pool;
}

// ──────────────────────────────────────────────────────────────────────────────
// DescriptorWriter
// ──────────────────────────────────────────────────────────────────────────────

DescriptorWriter& DescriptorWriter::writeBuffer(uint32_t binding, VkBuffer buffer,
                                                  VkDeviceSize size, VkDeviceSize offset,
                                                  VkDescriptorType type) {
    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = buffer;
    bufInfo.offset = offset;
    bufInfo.range = size;
    m_bufferInfos.push_back(bufInfo);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = type;
    // pBufferInfo will be patched in updateSet()
    m_writes.push_back(write);

    return *this;
}

DescriptorWriter& DescriptorWriter::writeImage(uint32_t binding, VkImageView imageView,
                                                 VkSampler sampler, VkImageLayout layout,
                                                 VkDescriptorType type) {
    VkDescriptorImageInfo imgInfo{};
    imgInfo.sampler = sampler;
    imgInfo.imageView = imageView;
    imgInfo.imageLayout = layout;
    m_imageInfos.push_back(imgInfo);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = type;
    // pImageInfo will be patched in updateSet()
    m_writes.push_back(write);

    return *this;
}

void DescriptorWriter::updateSet(VkDevice device, VkDescriptorSet set) {
    uint32_t bufIdx = 0;
    uint32_t imgIdx = 0;

    for (auto& write : m_writes) {
        write.dstSet = set;
        if (write.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
            write.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
            write.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
            write.pBufferInfo = &m_bufferInfos[bufIdx++];
        } else {
            write.pImageInfo = &m_imageInfos[imgIdx++];
        }
    }

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(m_writes.size()), m_writes.data(), 0, nullptr);
}

void DescriptorWriter::clear() {
    m_bufferInfos.clear();
    m_imageInfos.clear();
    m_writes.clear();
}
