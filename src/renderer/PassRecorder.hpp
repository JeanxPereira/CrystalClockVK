#pragma once

#include <vulkan/vulkan.h>

// Wraps a VkCommandBuffer with typed recording helpers for structured pass recording.
// Uses Sync2 barriers and VK_KHR_dynamic_rendering.
class PassRecorder {
public:
    explicit PassRecorder(VkCommandBuffer cmd) : m_cmd(cmd) {}

    // Image layout transitions (Sync2)
    void transitionImage(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);

    // Dynamic rendering scope
    void beginRendering(VkImageView colorAttachment, VkExtent2D extent, VkClearValue* clearValue = nullptr, VkImageLayout layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    void beginRendering(VkImageView colorAttachment, VkImageView depthAttachment,
                        VkExtent2D extent, VkClearValue* clearValue = nullptr, VkImageLayout layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    void endRendering();

    // Pipeline and draw commands
    void bindPipeline(VkPipeline pipeline);
    void setViewportScissor(VkExtent2D extent);
    void pushConstants(VkPipelineLayout layout, VkShaderStageFlags stages, const void* data, uint32_t size);
    void bindVertexBuffer(VkBuffer buffer);
    void bindIndexBuffer(VkBuffer buffer, VkIndexType type = VK_INDEX_TYPE_UINT32);
    void bindDescriptorSet(VkPipelineLayout layout, uint32_t set, VkDescriptorSet descriptorSet);
    
    // Dynamic Rendering Local Read helpers
    void insertLocalReadBarrier();
    void setLocalReadInputIndices(uint32_t colorAttachmentIndex);
    void draw(uint32_t vertexCount, uint32_t instanceCount = 1);
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1);

    // Debug labels (visible in RenderDoc)
    void beginDebugLabel(const char* name, float r = 0.0f, float g = 0.5f, float b = 1.0f);
    void endDebugLabel();

    VkCommandBuffer cmd() const { return m_cmd; }

private:
    VkCommandBuffer m_cmd;
};
