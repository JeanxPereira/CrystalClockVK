#include "ResourceManager.hpp"
#include <stdexcept>
#include <cstring>

ResourceManager::ResourceManager(const VulkanContext& ctx) : m_ctx(ctx) {
    // Dedicated command pool for staging uploads
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_ctx.graphicsQueueFamily();
    vkCreateCommandPool(m_ctx.device(), &poolInfo, nullptr, &m_uploadPool);

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(m_ctx.device(), &fenceInfo, nullptr, &m_uploadFence);
}

ResourceManager::~ResourceManager() {
    vkDestroyFence(m_ctx.device(), m_uploadFence, nullptr);
    vkDestroyCommandPool(m_ctx.device(), m_uploadPool, nullptr);
}

AllocatedBuffer ResourceManager::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memUsage) {
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = size;
    bufInfo.usage = usage;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memUsage;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    if (memUsage == VMA_MEMORY_USAGE_AUTO) {
        allocInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }

    AllocatedBuffer result{};
    result.size = size;

    if (vmaCreateBuffer(m_ctx.allocator(), &bufInfo, &allocInfo,
                        &result.buffer, &result.allocation, &result.allocationInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VMA buffer");
    }

    return result;
}

AllocatedImage ResourceManager::createImage(VkExtent2D extent, VkFormat format,
                                             VkImageUsageFlags usage, VmaMemoryUsage memUsage,
                                             VkSampleCountFlagBits samples) {
    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = format;
    imgInfo.extent = {extent.width, extent.height, 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = samples;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = usage;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memUsage;

    AllocatedImage result{};
    result.extent = extent;
    result.format = format;

    if (vmaCreateImage(m_ctx.allocator(), &imgInfo, &allocInfo,
                       &result.image, &result.allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VMA image");
    }

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = result.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;

    // Auto-detect aspect mask based on format
    bool isDepth = (format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D16_UNORM ||
                    format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT);
    viewInfo.subresourceRange.aspectMask = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_ctx.device(), &viewInfo, nullptr, &result.imageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image view");
    }

    return result;
}

void ResourceManager::uploadToBuffer(const AllocatedBuffer& dst, const void* data, VkDeviceSize size) {
    // Create staging buffer
    AllocatedBuffer staging = createBuffer(size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_ONLY);

    std::memcpy(staging.allocationInfo.pMappedData, data, size);

    immediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy copy{};
        copy.size = size;
        vkCmdCopyBuffer(cmd, staging.buffer, dst.buffer, 1, &copy);
    });

    vmaDestroyBuffer(m_ctx.allocator(), staging.buffer, staging.allocation);
}

void ResourceManager::uploadToImage(const AllocatedImage& dst, const void* data,
                                     VkExtent2D extent, VkFormat format) {
    // Calculate pixel size
    VkDeviceSize pixelSize = 4; // RGBA8
    if (format == VK_FORMAT_R32G32B32A32_SFLOAT) pixelSize = 16;
    VkDeviceSize imageSize = extent.width * extent.height * pixelSize;

    AllocatedBuffer staging = createBuffer(imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_ONLY);

    std::memcpy(staging.allocationInfo.pMappedData, data, imageSize);

    immediateSubmit([&](VkCommandBuffer cmd) {
        // Transition to TRANSFER_DST
        VkImageMemoryBarrier2 toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        toTransfer.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        toTransfer.srcAccessMask = VK_ACCESS_2_NONE;
        toTransfer.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        toTransfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.image = dst.image;
        toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &toTransfer;
        vkCmdPipelineBarrier2(cmd, &dep);

        // Copy buffer to image
        VkBufferImageCopy copyRegion{};
        copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.imageExtent = {extent.width, extent.height, 1};
        vkCmdCopyBufferToImage(cmd, staging.buffer, dst.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        // Transition to SHADER_READ_ONLY
        VkImageMemoryBarrier2 toShader{};
        toShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        toShader.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        toShader.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toShader.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        toShader.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toShader.image = dst.image;
        toShader.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkDependencyInfo dep2{};
        dep2.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep2.imageMemoryBarrierCount = 1;
        dep2.pImageMemoryBarriers = &toShader;
        vkCmdPipelineBarrier2(cmd, &dep2);
    });

    vmaDestroyBuffer(m_ctx.allocator(), staging.buffer, staging.allocation);
}

std::vector<uint8_t> ResourceManager::downloadImage(const AllocatedImage& src, VkOffset2D offset,
                                                    VkExtent2D extent, VkImageLayout srcLayout) {
    const VkDeviceSize size = VkDeviceSize(extent.width) * extent.height * 4;
    AllocatedBuffer readback = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                            VMA_MEMORY_USAGE_CPU_ONLY);

    immediateSubmit([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier2 toSrc{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        toSrc.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        toSrc.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        toSrc.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        toSrc.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        toSrc.oldLayout = srcLayout;
        toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toSrc.image = src.image;
        toSrc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &toSrc;
        vkCmdPipelineBarrier2(cmd, &dep);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {offset.x, offset.y, 0};
        region.imageExtent = {extent.width, extent.height, 1};
        vkCmdCopyImageToBuffer(cmd, src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readback.buffer, 1, &region);

        if (srcLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
            srcLayout != VK_IMAGE_LAYOUT_UNDEFINED) {
            VkImageMemoryBarrier2 back = toSrc;
            back.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            back.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            back.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            back.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
            back.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            back.newLayout = srcLayout;
            VkDependencyInfo depBack{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            depBack.imageMemoryBarrierCount = 1;
            depBack.pImageMemoryBarriers = &back;
            vkCmdPipelineBarrier2(cmd, &depBack);
        }
    });

    std::vector<uint8_t> out(size);
    std::memcpy(out.data(), readback.allocationInfo.pMappedData, size);
    vmaDestroyBuffer(m_ctx.allocator(), readback.buffer, readback.allocation);
    return out;
}

void ResourceManager::destroyBuffer(AllocatedBuffer& buffer) {
    vmaDestroyBuffer(m_ctx.allocator(), buffer.buffer, buffer.allocation);
    buffer = {};
}

void ResourceManager::destroyImage(AllocatedImage& image) {
    vkDestroyImageView(m_ctx.device(), image.imageView, nullptr);
    vmaDestroyImage(m_ctx.allocator(), image.image, image.allocation);
    image = {};
}

void ResourceManager::immediateSubmit(std::function<void(VkCommandBuffer)>&& fn) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_uploadPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_ctx.device(), &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    fn(cmd);

    vkEndCommandBuffer(cmd);

    VkCommandBufferSubmitInfo cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = cmd;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdInfo;

    vkQueueSubmit2(m_ctx.graphicsQueue(), 1, &submitInfo, m_uploadFence);
    vkWaitForFences(m_ctx.device(), 1, &m_uploadFence, VK_TRUE, UINT64_MAX);
    vkResetFences(m_ctx.device(), 1, &m_uploadFence);

    vkResetCommandPool(m_ctx.device(), m_uploadPool, 0);
}
