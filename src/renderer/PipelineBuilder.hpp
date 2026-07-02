#pragma once

#include <vulkan/vulkan.h>
#include <vector>

// Builder pattern for VkPipeline construction using VK_KHR_dynamic_rendering.
class PipelineBuilder {
public:
    PipelineBuilder();

    // Shader stages
    PipelineBuilder& setShaders(VkShaderModule vertexShader, VkShaderModule fragmentShader);

    // Vertex input (empty = hardcoded in shader, or provide bindings/attributes)
    PipelineBuilder& setVertexInput(
        const std::vector<VkVertexInputBindingDescription>& bindings,
        const std::vector<VkVertexInputAttributeDescription>& attributes);

    // Input assembly
    PipelineBuilder& setTopology(VkPrimitiveTopology topology);

    // Rasterizer
    PipelineBuilder& setCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE);
    PipelineBuilder& setPolygonMode(VkPolygonMode mode);

    // Depth
    PipelineBuilder& setDepthTest(bool enable, bool writeEnable = true, VkCompareOp op = VK_COMPARE_OP_LESS_OR_EQUAL);

    // Blending — explicit VK blend state derived from decoded GS ALPHA
    PipelineBuilder& setBlendState(const VkPipelineColorBlendAttachmentState& state);
    PipelineBuilder& setBlendConstants(float r, float g, float b, float a);

    // Dynamic rendering color format
    PipelineBuilder& setColorFormat(VkFormat format);
    PipelineBuilder& setDepthFormat(VkFormat format);

    // Pipeline layout
    PipelineBuilder& setPipelineLayout(VkPipelineLayout layout);

    // Pipeline creation flags
    PipelineBuilder& setFlags(VkPipelineCreateFlags flags);

    // Build the final pipeline
    VkPipeline build(VkDevice device);

private:
    std::vector<VkPipelineShaderStageCreateInfo> m_shaderStages;
    VkPipelineVertexInputStateCreateInfo m_vertexInput{};
    VkPipelineInputAssemblyStateCreateInfo m_inputAssembly{};
    VkPipelineRasterizationStateCreateInfo m_rasterizer{};
    VkPipelineMultisampleStateCreateInfo m_multisampling{};
    VkPipelineColorBlendAttachmentState m_colorBlendAttachment{};
    VkPipelineDepthStencilStateCreateInfo m_depthStencil{};
    VkPipelineRenderingCreateInfo m_renderingInfo{};
    VkPipelineLayout m_layout{VK_NULL_HANDLE};
    VkFormat m_colorFormat{VK_FORMAT_UNDEFINED};
    VkFormat m_depthFormat{VK_FORMAT_UNDEFINED};
    VkPipelineCreateFlags m_flags{0};
    float m_blendConstants[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    std::vector<VkVertexInputBindingDescription> m_bindings;
    std::vector<VkVertexInputAttributeDescription> m_attributes;
};
