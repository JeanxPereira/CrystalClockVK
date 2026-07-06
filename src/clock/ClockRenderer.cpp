#include "clock/ClockRenderer.hpp"

#include "renderer/PipelineBuilder.hpp"
#include "renderer/ShaderLoader.hpp"

#include <stdexcept>

namespace {
VkPipelineColorBlendAttachmentState opaqueBlend() {
    VkPipelineColorBlendAttachmentState s{};
    s.blendEnable = VK_FALSE;
    s.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    return s;
}
}  // namespace

ClockRenderer::ClockRenderer(const VulkanContext& ctx, ResourceManager& resources,
                             VkFormat colorFormat, VkExtent2D extent)
    : m_ctx(ctx), m_resources(resources), m_extent(extent), m_colorFormat(colorFormat) {
    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pc.offset = 0;
    pc.size = sizeof(ps2clock::Mat4);

    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &pc;
    if (vkCreatePipelineLayout(m_ctx.device(), &li, nullptr, &m_layout) != VK_SUCCESS)
        throw std::runtime_error("ClockRenderer: pipeline layout");

    VkShaderModule vert = ShaderLoader::loadModule(m_ctx.device(), "bin/shaders/rod_flat.vert.spv");
    VkShaderModule frag = ShaderLoader::loadModule(m_ctx.device(), "bin/shaders/rod_flat.frag.spv");

    VkVertexInputBindingDescription bind{};
    bind.binding = 0;
    bind.stride = sizeof(ps2clock::RodVertex);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attrs = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ps2clock::RodVertex, pos)},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(ps2clock::RodVertex, color)},
    };

    PipelineBuilder builder;
    m_pipeline = builder
        .setShaders(vert, frag)
        .setVertexInput({bind}, attrs)
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setCullMode(VK_CULL_MODE_NONE)
        .setPolygonMode(VK_POLYGON_MODE_FILL)
        .setDepthTest(false, false)
        .setBlendState(opaqueBlend())
        .setColorFormat(colorFormat)
        .setPipelineLayout(m_layout)
        .build(m_ctx.device());

    vkDestroyShaderModule(m_ctx.device(), vert, nullptr);
    vkDestroyShaderModule(m_ctx.device(), frag, nullptr);
}

ClockRenderer::~ClockRenderer() {
    if (m_vbo.buffer) m_resources.destroyBuffer(m_vbo);
    if (m_ibo.buffer) m_resources.destroyBuffer(m_ibo);
    if (m_pipeline) vkDestroyPipeline(m_ctx.device(), m_pipeline, nullptr);
    if (m_layout) vkDestroyPipelineLayout(m_ctx.device(), m_layout, nullptr);
}

void ClockRenderer::setRodField(const ps2clock::RodField& field) {
    setDialMesh(field.buildFlatMesh());
}

void ClockRenderer::setDialMesh(const ps2clock::FlatMesh& mesh) {
    if (m_vbo.buffer) m_resources.destroyBuffer(m_vbo);
    if (m_ibo.buffer) m_resources.destroyBuffer(m_ibo);
    m_indexCount = static_cast<uint32_t>(mesh.indices.size());

    const VkDeviceSize vSize = mesh.vertices.size() * sizeof(ps2clock::RodVertex);
    const VkDeviceSize iSize = mesh.indices.size() * sizeof(uint32_t);

    m_vbo = m_resources.createBuffer(vSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    m_ibo = m_resources.createBuffer(iSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    m_resources.uploadToBuffer(m_vbo, mesh.vertices.data(), vSize);
    m_resources.uploadToBuffer(m_ibo, mesh.indices.data(), iSize);
}

void ClockRenderer::record(PassRecorder& recorder, VkImageView colorView, const ps2clock::Mat4& mvp) {
    VkClearValue clear{};
    clear.color = {{m_clear[0], m_clear[1], m_clear[2], 1.0f}};

    recorder.beginDebugLabel("ClockRenderer::rods");
    recorder.beginRendering(colorView, m_extent, &clear);
    recorder.setViewportScissor(m_extent);
    recorder.bindPipeline(m_pipeline);
    recorder.pushConstants(m_layout, VK_SHADER_STAGE_VERTEX_BIT, &mvp, sizeof(ps2clock::Mat4));
    recorder.bindVertexBuffer(m_vbo.buffer);
    recorder.bindIndexBuffer(m_ibo.buffer);
    recorder.drawIndexed(m_indexCount);
    recorder.endRendering();
    recorder.endDebugLabel();
}
