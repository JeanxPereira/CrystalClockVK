#pragma once

#include <vulkan/vulkan.h>
#include <vector>

// The 4 blend modes needed by the 5-pass Crystal Clock pipeline.
// Maps directly to the GS ALPHA register configurations from opus-rod-analysis.md.
enum class BlendMode {
    Opaque,        // No blending (tunnel background)
    AlphaBlend,    // Standard alpha: srcAlpha * src + (1-srcAlpha) * dst  (Pass 1, 4)
    Additive,      // Additive glow: one * src + one * dst                 (Pass 2, 3)
    ReverseAlpha,  // Reverse fill: (1-srcAlpha) * src + srcAlpha * dst    (Pass 5)
};

// Builder pattern for VkPipeline construction using VK_KHR_dynamic_rendering.
// Designed for the Crystal Clock's specific pipeline needs.
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

    // Blending — maps to GS ALPHA register per-pass
    PipelineBuilder& setBlendMode(BlendMode mode);

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

    std::vector<VkVertexInputBindingDescription> m_bindings;
    std::vector<VkVertexInputAttributeDescription> m_attributes;
};
