#include "PipelineBuilder.hpp"
#include <stdexcept>

PipelineBuilder::PipelineBuilder() {
    // Vertex input — default empty (for fullscreen quads or hardcoded geometry)
    m_vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    // Triangle list by default
    m_inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    m_inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    m_inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Default rasterizer: fill, back-face cull, CCW front
    m_rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    m_rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    m_rasterizer.lineWidth = 1.0f;
    m_rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    m_rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    // No MSAA
    m_multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    m_multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Default: opaque (no blending)
    m_colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    m_colorBlendAttachment.blendEnable = VK_FALSE;

    // Depth: disabled by default
    m_depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    m_depthStencil.depthTestEnable = VK_FALSE;
    m_depthStencil.depthWriteEnable = VK_FALSE;
    m_depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    // Dynamic rendering info
    m_renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
}

PipelineBuilder& PipelineBuilder::setShaders(VkShaderModule vertexShader, VkShaderModule fragmentShader) {
    m_shaderStages.clear();

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertexShader;
    vertStage.pName = "main";
    m_shaderStages.push_back(vertStage);

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragmentShader;
    fragStage.pName = "main";
    m_shaderStages.push_back(fragStage);

    return *this;
}

PipelineBuilder& PipelineBuilder::setVertexInput(
    const std::vector<VkVertexInputBindingDescription>& bindings,
    const std::vector<VkVertexInputAttributeDescription>& attributes) {
    m_bindings = bindings;
    m_attributes = attributes;
    return *this;
}

PipelineBuilder& PipelineBuilder::setTopology(VkPrimitiveTopology topology) {
    m_inputAssembly.topology = topology;
    return *this;
}

PipelineBuilder& PipelineBuilder::setCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace) {
    m_rasterizer.cullMode = cullMode;
    m_rasterizer.frontFace = frontFace;
    return *this;
}

PipelineBuilder& PipelineBuilder::setPolygonMode(VkPolygonMode mode) {
    m_rasterizer.polygonMode = mode;
    return *this;
}

PipelineBuilder& PipelineBuilder::setDepthTest(bool enable, bool writeEnable, VkCompareOp op) {
    m_depthStencil.depthTestEnable = enable ? VK_TRUE : VK_FALSE;
    m_depthStencil.depthWriteEnable = writeEnable ? VK_TRUE : VK_FALSE;
    m_depthStencil.depthCompareOp = op;
    return *this;
}

PipelineBuilder& PipelineBuilder::setBlendMode(BlendMode mode) {
    m_colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    switch (mode) {
        case BlendMode::Opaque:
            m_colorBlendAttachment.blendEnable = VK_FALSE;
            break;

        case BlendMode::AlphaBlend:
            // GS: (Cs - Cd) * As + Cd → standard src-over alpha blend
            m_colorBlendAttachment.blendEnable = VK_TRUE;
            m_colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            m_colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            m_colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            m_colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            m_colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            m_colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;

        case BlendMode::Additive:
            // GS: Cs * FIX + Cd * 1 → additive glow for edge highlights
            m_colorBlendAttachment.blendEnable = VK_TRUE;
            m_colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            m_colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            m_colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            m_colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            m_colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            m_colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;

        case BlendMode::ReverseAlpha:
            // GS: (Cd - Cs) * As + Cs → reverse blend for solid fill
            m_colorBlendAttachment.blendEnable = VK_TRUE;
            m_colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            m_colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            m_colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            m_colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            m_colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            m_colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
    }

    return *this;
}

PipelineBuilder& PipelineBuilder::setColorFormat(VkFormat format) {
    m_colorFormat = format;
    return *this;
}

PipelineBuilder& PipelineBuilder::setDepthFormat(VkFormat format) {
    m_depthFormat = format;
    return *this;
}

PipelineBuilder& PipelineBuilder::setPipelineLayout(VkPipelineLayout layout) {
    m_layout = layout;
    return *this;
}

PipelineBuilder& PipelineBuilder::setFlags(VkPipelineCreateFlags flags) {
    m_flags = flags;
    return *this;
}

VkPipeline PipelineBuilder::build(VkDevice device) {
    // Wire up vertex input with stored bindings/attributes
    m_vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(m_bindings.size());
    m_vertexInput.pVertexBindingDescriptions = m_bindings.data();
    m_vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_attributes.size());
    m_vertexInput.pVertexAttributeDescriptions = m_attributes.data();

    // Viewport/scissor are always dynamic
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &m_colorBlendAttachment;

    // Dynamic state: viewport + scissor (set at draw time)
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // Dynamic rendering format
    m_renderingInfo.colorAttachmentCount = 1;
    m_renderingInfo.pColorAttachmentFormats = &m_colorFormat;
    m_renderingInfo.depthAttachmentFormat = m_depthFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &m_renderingInfo;
    pipelineInfo.flags = m_flags;
    pipelineInfo.stageCount = static_cast<uint32_t>(m_shaderStages.size());
    pipelineInfo.pStages = m_shaderStages.data();
    pipelineInfo.pVertexInputState = &m_vertexInput;
    pipelineInfo.pInputAssemblyState = &m_inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &m_rasterizer;
    pipelineInfo.pMultisampleState = &m_multisampling;
    pipelineInfo.pDepthStencilState = &m_depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_layout;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline");
    }

    return pipeline;
}
