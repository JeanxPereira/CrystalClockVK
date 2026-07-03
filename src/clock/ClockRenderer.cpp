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
    if (m_depth.image) m_resources.destroyImage(m_depth);
    if (m_pipeline) vkDestroyPipeline(m_ctx.device(), m_pipeline, nullptr);
    if (m_prismPipeline) vkDestroyPipeline(m_ctx.device(), m_prismPipeline, nullptr);
    if (m_bgPipeline) vkDestroyPipeline(m_ctx.device(), m_bgPipeline, nullptr);
    if (m_spotPipeline) vkDestroyPipeline(m_ctx.device(), m_spotPipeline, nullptr);
    if (m_spotVbo.buffer) m_resources.destroyBuffer(m_spotVbo);
    if (m_spotIbo.buffer) m_resources.destroyBuffer(m_spotIbo);
    if (m_layout) vkDestroyPipelineLayout(m_ctx.device(), m_layout, nullptr);
}

void ClockRenderer::setSpotMesh(const ps2clock::SpotMesh& mesh) {
    if (!m_spotPipeline) {
        VkShaderModule vert = ShaderLoader::loadModule(m_ctx.device(), "bin/shaders/spot.vert.spv");
        VkShaderModule frag = ShaderLoader::loadModule(m_ctx.device(), "bin/shaders/spot.frag.spv");
        VkVertexInputBindingDescription bind{};
        bind.binding = 0;
        bind.stride = sizeof(ps2clock::SpotVertex);
        bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        std::vector<VkVertexInputAttributeDescription> attrs = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ps2clock::SpotVertex, pos)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ps2clock::SpotVertex, uv)},
            {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(ps2clock::SpotVertex, color)},
        };
        // Additive glow: src*srcAlpha + dst (accumulate light).
        VkPipelineColorBlendAttachmentState add{};
        add.blendEnable = VK_TRUE;
        add.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        add.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        add.colorBlendOp = VK_BLEND_OP_ADD;
        add.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        add.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        add.alphaBlendOp = VK_BLEND_OP_ADD;
        add.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        PipelineBuilder b;
        m_spotPipeline = b.setShaders(vert, frag).setVertexInput({bind}, attrs)
            .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
            .setCullMode(VK_CULL_MODE_NONE).setPolygonMode(VK_POLYGON_MODE_FILL)
            .setDepthTest(false, false).setBlendState(add)
            .setColorFormat(m_colorFormat).setPipelineLayout(m_layout)
            .build(m_ctx.device());
        vkDestroyShaderModule(m_ctx.device(), vert, nullptr);
        vkDestroyShaderModule(m_ctx.device(), frag, nullptr);
    }
    if (m_spotVbo.buffer) m_resources.destroyBuffer(m_spotVbo);
    if (m_spotIbo.buffer) m_resources.destroyBuffer(m_spotIbo);
    m_spotIndexCount = static_cast<uint32_t>(mesh.indices.size());
    const VkDeviceSize vSize = mesh.vertices.size() * sizeof(ps2clock::SpotVertex);
    const VkDeviceSize iSize = mesh.indices.size() * sizeof(uint32_t);
    m_spotVbo = m_resources.createBuffer(vSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
    m_spotIbo = m_resources.createBuffer(iSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
    m_resources.uploadToBuffer(m_spotVbo, mesh.vertices.data(), vSize);
    m_resources.uploadToBuffer(m_spotIbo, mesh.indices.data(), iSize);
}

void ClockRenderer::setPrismMesh(const ps2clock::PrismMesh& mesh) {
    // Build the prism pipeline lazily (pos + normal + colour, depth-tested).
    if (!m_prismPipeline) {
        VkShaderModule vert = ShaderLoader::loadModule(m_ctx.device(), "bin/shaders/rod_prism.vert.spv");
        VkShaderModule frag = ShaderLoader::loadModule(m_ctx.device(), "bin/shaders/rod_prism.frag.spv");
        VkVertexInputBindingDescription bind{};
        bind.binding = 0;
        bind.stride = sizeof(ps2clock::PrismVertex);
        bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        std::vector<VkVertexInputAttributeDescription> attrs = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ps2clock::PrismVertex, pos)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ps2clock::PrismVertex, normal)},
            {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(ps2clock::PrismVertex, color)},
            {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ps2clock::PrismVertex, uv)},
        };
        // Alpha-over (src-over) translucent glass: the 3D box stacks 6 facets,
        // so additive blows out — over-blend keeps the tint readable while the
        // faint-alpha ghost above the fill still shows through.
        VkPipelineColorBlendAttachmentState crystal{};
        crystal.blendEnable = VK_TRUE;
        crystal.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        crystal.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        crystal.colorBlendOp = VK_BLEND_OP_ADD;
        crystal.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        crystal.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        crystal.alphaBlendOp = VK_BLEND_OP_ADD;
        crystal.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        PipelineBuilder builder;
        m_prismPipeline = builder
            .setShaders(vert, frag)
            .setVertexInput({bind}, attrs)
            .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
            .setCullMode(VK_CULL_MODE_NONE)
            .setPolygonMode(VK_POLYGON_MODE_FILL)
            .setDepthTest(false, false)
            .setBlendState(crystal)
            .setColorFormat(m_colorFormat)
            .setPipelineLayout(m_layout)
            .build(m_ctx.device());
        vkDestroyShaderModule(m_ctx.device(), vert, nullptr);
        vkDestroyShaderModule(m_ctx.device(), frag, nullptr);

        // Fullscreen tunnel background pipeline (no vertex buffer).
        VkShaderModule bv = ShaderLoader::loadModule(m_ctx.device(), "bin/shaders/bg_tunnel.vert.spv");
        VkShaderModule bf = ShaderLoader::loadModule(m_ctx.device(), "bin/shaders/bg_tunnel.frag.spv");
        PipelineBuilder bgb;
        m_bgPipeline = bgb
            .setShaders(bv, bf)
            .setVertexInput({}, {})
            .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
            .setCullMode(VK_CULL_MODE_NONE)
            .setPolygonMode(VK_POLYGON_MODE_FILL)
            .setDepthTest(false, false)
            .setBlendState(opaqueBlend())
            .setColorFormat(m_colorFormat)
            .setPipelineLayout(m_layout)
            .build(m_ctx.device());
        vkDestroyShaderModule(m_ctx.device(), bv, nullptr);
        vkDestroyShaderModule(m_ctx.device(), bf, nullptr);
    }
    if (m_vbo.buffer) m_resources.destroyBuffer(m_vbo);
    if (m_ibo.buffer) m_resources.destroyBuffer(m_ibo);
    m_indexCount = static_cast<uint32_t>(mesh.indices.size());
    const VkDeviceSize vSize = mesh.vertices.size() * sizeof(ps2clock::PrismVertex);
    const VkDeviceSize iSize = mesh.indices.size() * sizeof(uint32_t);
    m_vbo = m_resources.createBuffer(vSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
    m_ibo = m_resources.createBuffer(iSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
    m_resources.uploadToBuffer(m_vbo, mesh.vertices.data(), vSize);
    m_resources.uploadToBuffer(m_ibo, mesh.indices.data(), iSize);
    m_prism = true;
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
    // Prism mode: draw the tunnel background first (fullscreen), then the
    // refractive crystal rods over it.
    if (m_prism && m_bgPipeline) {
        recorder.bindPipeline(m_bgPipeline);
        recorder.draw(3);
    }
    recorder.bindPipeline(m_prism ? m_prismPipeline : m_pipeline);
    recorder.pushConstants(m_layout, VK_SHADER_STAGE_VERTEX_BIT, &mvp, sizeof(ps2clock::Mat4));
    recorder.bindVertexBuffer(m_vbo.buffer);
    recorder.bindIndexBuffer(m_ibo.buffer);
    recorder.drawIndexed(m_indexCount);

    // Light-spot glow additively over everything (the orbiting after-image dots).
    if (m_prism && m_spotPipeline && m_spotIndexCount) {
        recorder.bindPipeline(m_spotPipeline);
        recorder.pushConstants(m_layout, VK_SHADER_STAGE_VERTEX_BIT, &mvp, sizeof(ps2clock::Mat4));
        recorder.bindVertexBuffer(m_spotVbo.buffer);
        recorder.bindIndexBuffer(m_spotIbo.buffer);
        recorder.drawIndexed(m_spotIndexCount);
    }
    recorder.endRendering();
    recorder.endDebugLabel();
}
