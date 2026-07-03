#include "PassRecorder.hpp"

void PassRecorder::transitionImage(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.image = image;
    const bool depth = newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL ||
                       oldLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    barrier.subresourceRange.aspectMask = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(m_cmd, &depInfo);
}

void PassRecorder::beginRendering(VkImageView colorAttachment, VkExtent2D extent, VkClearValue* clearValue, VkImageLayout layout) {
    VkRenderingAttachmentInfo colorInfo{};
    colorInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorInfo.imageView = colorAttachment;
    colorInfo.imageLayout = layout;
    colorInfo.loadOp = clearValue ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    colorInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    if (clearValue) colorInfo.clearValue = *clearValue;

    VkRenderingInfo renderInfo{};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.renderArea.extent = extent;
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorInfo;

    vkCmdBeginRendering(m_cmd, &renderInfo);
}

void PassRecorder::beginRendering(VkImageView colorAttachment, VkImageView depthAttachment,
                                   VkExtent2D extent, VkClearValue* clearValue, VkImageLayout layout,
                                   VkAttachmentLoadOp depthLoadOp, float depthClearValue, bool depthStore) {
    VkRenderingAttachmentInfo colorInfo{};
    colorInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorInfo.imageView = colorAttachment;
    colorInfo.imageLayout = layout;
    colorInfo.loadOp = clearValue ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    colorInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    if (clearValue) colorInfo.clearValue = *clearValue;

    VkRenderingAttachmentInfo depthInfo{};
    depthInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthInfo.imageView = depthAttachment;
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthInfo.loadOp = depthLoadOp;
    depthInfo.storeOp = depthStore ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthInfo.clearValue.depthStencil = {depthClearValue, 0};

    VkRenderingInfo renderInfo{};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.renderArea.extent = extent;
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorInfo;
    renderInfo.pDepthAttachment = &depthInfo;

    vkCmdBeginRendering(m_cmd, &renderInfo);
}

void PassRecorder::beginRenderingMS(VkImageView colorMS, VkImageView resolve, VkImageView depthMS,
                                    VkRect2D area, VkAttachmentLoadOp colorLoadOp,
                                    VkAttachmentLoadOp depthLoadOp, float depthClearValue) {
    VkRenderingAttachmentInfo colorInfo{};
    colorInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorInfo.imageView = colorMS;
    colorInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorInfo.loadOp = colorLoadOp;
    colorInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorInfo.clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    if (resolve) {
        colorInfo.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        colorInfo.resolveImageView = resolve;
        colorInfo.resolveImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }

    VkRenderingAttachmentInfo depthInfo{};
    depthInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthInfo.imageView = depthMS;
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthInfo.loadOp = depthLoadOp;
    depthInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthInfo.clearValue.depthStencil = {depthClearValue, 0};

    VkRenderingInfo renderInfo{};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.renderArea = area;
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorInfo;
    renderInfo.pDepthAttachment = depthMS ? &depthInfo : nullptr;

    vkCmdBeginRendering(m_cmd, &renderInfo);
}

void PassRecorder::endRendering() {
    vkCmdEndRendering(m_cmd);
}

void PassRecorder::bindPipeline(VkPipeline pipeline) {
    vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
}

void PassRecorder::setViewportScissor(VkExtent2D extent) {
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(m_cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = extent;
    vkCmdSetScissor(m_cmd, 0, 1, &scissor);
}

void PassRecorder::pushConstants(VkPipelineLayout layout, VkShaderStageFlags stages,
                                  const void* data, uint32_t size) {
    vkCmdPushConstants(m_cmd, layout, stages, 0, size, data);
}

void PassRecorder::bindVertexBuffer(VkBuffer buffer) {
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(m_cmd, 0, 1, &buffer, &offset);
}

void PassRecorder::bindIndexBuffer(VkBuffer buffer, VkIndexType type) {
    vkCmdBindIndexBuffer(m_cmd, buffer, 0, type);
}

void PassRecorder::bindDescriptorSet(VkPipelineLayout layout, uint32_t set, VkDescriptorSet descriptorSet) {
    vkCmdBindDescriptorSets(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, set, 1, &descriptorSet, 0, nullptr);
}

void PassRecorder::insertLocalReadBarrier() {
    VkMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT;

    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    depInfo.memoryBarrierCount = 1;
    depInfo.pMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(m_cmd, &depInfo);
}

void PassRecorder::setLocalReadInputIndices(uint32_t colorAttachmentIndex) {
    VkRenderingInputAttachmentIndexInfoKHR indexInfo{};
    indexInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INPUT_ATTACHMENT_INDEX_INFO_KHR;
    indexInfo.colorAttachmentCount = 1;
    indexInfo.pColorAttachmentInputIndices = &colorAttachmentIndex;

    static auto fn = reinterpret_cast<PFN_vkCmdSetRenderingInputAttachmentIndicesKHR>(
        vkGetInstanceProcAddr(nullptr, "vkCmdSetRenderingInputAttachmentIndicesKHR"));
    if (fn) {
        fn(m_cmd, &indexInfo);
    }
}

void PassRecorder::draw(uint32_t vertexCount, uint32_t instanceCount) {
    vkCmdDraw(m_cmd, vertexCount, instanceCount, 0, 0);
}

void PassRecorder::drawIndexed(uint32_t indexCount, uint32_t instanceCount) {
    vkCmdDrawIndexed(m_cmd, indexCount, instanceCount, 0, 0, 0);
}

void PassRecorder::beginDebugLabel(const char* name, float r, float g, float b) {
    // Dynamically load the extension function (cached after first call)
    static auto fn = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
        vkGetInstanceProcAddr(nullptr, "vkCmdBeginDebugUtilsLabelEXT"));
    if (!fn) return;

    VkDebugUtilsLabelEXT label{};
    label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    label.pLabelName = name;
    label.color[0] = r;
    label.color[1] = g;
    label.color[2] = b;
    label.color[3] = 1.0f;
    fn(m_cmd, &label);
}

void PassRecorder::endDebugLabel() {
    static auto fn = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
        vkGetInstanceProcAddr(nullptr, "vkCmdEndDebugUtilsLabelEXT"));
    if (!fn) return;

    fn(m_cmd);
}
