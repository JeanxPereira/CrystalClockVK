#include "app/GsRenderer.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "gs/SwizzleEngine.hpp"
#include "gsvk/GsDrawRecipe.hpp"
#include "renderer/PipelineBuilder.hpp"
#include "renderer/ShaderLoader.hpp"

namespace {

// GS savestate v9: 4MB VRAM (m_vm8) starts here in the freeze (tools/vramdump).
constexpr size_t kVramFreezeOffset = 433;
// VRAM modeled as a 640-wide linear image; framebuffers are row-regions of it.
constexpr uint32_t kVramW = 640;
constexpr uint32_t kVramH = 1408;  // covers FBP 0/70/210/280 + feedback rows
constexpr uint32_t kFbH = 224;     // visible/display height

// VRAM row offset of a framebuffer (FBP byte = fbp*8192; row = byte/(640*4)).
uint32_t fbRow(uint32_t fbp) { return fbp * 8192u / (kVramW * 4u); }

struct PushConstants {
    float alphaRef;
    int alphaEnable;
    int alphaGreater;
    int textured;
};

uint64_t blendKey(const gsvk::GsBlendRecipe& b) {
    return (uint64_t(b.enable) << 40) | (uint64_t(b.colorOp) << 32) |
           (uint64_t(b.srcFactor) << 16) | uint64_t(b.dstFactor);
}

std::string findShader(const std::string& name) {
    namespace fs = std::filesystem;
    for (const char* base : {"bin/shaders/", "shaders/", "./shaders/"}) {
        std::string p = std::string(base) + name;
        if (fs::exists(p)) return p;
    }
    return "bin/shaders/" + name;
}

}  // namespace

int GsRenderer::textureIndexFor(uint32_t tbp0) {
    for (size_t i = 0; i < m_textureKeys.size(); i++)
        if (m_textureKeys[i] == tbp0) return static_cast<int>(i);
    return -1;
}

int GsRenderer::pipelineIndexFor(const gsvk::GsBlendRecipe& blend) {
    const uint64_t key = blendKey(blend);
    for (size_t i = 0; i < m_pipelineKeys.size(); i++)
        if (m_pipelineKeys[i] == key) return static_cast<int>(i);
    return -1;
}

void GsRenderer::init(const VulkanContext& ctx, ResourceManager& res, const GsScene& scene,
                      VkFormat, VkFormat) {
    if (!scene.stats().loaded) return;
    const VkDevice device = ctx.device();
    m_freeze = scene.stream().freeze.data();
    const auto& prims = scene.stream().prims;
    const auto& recipes = scene.recipes();

    // Distinct framebuffers actually rendered to (FRAME.FBP). A texture is feedback
    // only if it aliases one of these (tbp0 == fbp*32) — many resident textures are
    // also multiples of 32, so the bare tbp0%32 test is too broad.
    std::vector<uint32_t> fbps;
    for (const auto& p : prims)
        if ((p.prim.type == 4 || p.prim.type == 6)) {
            bool found = false;
            for (uint32_t f : fbps) found |= (f == p.frame.fbp);
            if (!found) fbps.push_back(p.frame.fbp);
        }
    auto isFeedback = [&](uint32_t tbp0) {
        if (tbp0 % 32) return false;
        const uint32_t f = tbp0 / 32;
        for (uint32_t x : fbps) if (x == f) return true;
        return false;
    };

    // --- sampler + descriptor layout ---
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(device, &si, nullptr, &m_sampler);

    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 1;
    li.pBindings = &b;
    vkCreateDescriptorSetLayout(device, &li, nullptr, &m_setLayout);

    // --- VRAM ping-pong images + seed (freeze deswizzled at stride 640) ---
    const VkImageUsageFlags vramUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    m_vramRead = res.createImage({kVramW, kVramH}, m_targetFormat, vramUsage);
    m_vramWrite = res.createImage({kVramW, kVramH}, m_targetFormat, vramUsage);
    {
        std::vector<uint8_t> vram = SwizzleEngine::deswizzle(
            m_freeze + kVramFreezeOffset, kVramW, kVramH, kVramW, GsPixelFormat::PSMCT32, nullptr);
        m_vramSeed = res.createImage({kVramW, kVramH}, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        res.uploadToImage(m_vramSeed, vram.data(), {kVramW, kVramH}, VK_FORMAT_R8G8B8A8_UNORM);
    }

    // --- resident (non-framebuffer) textures, PSMCT32/24, swizzle by TBW*64 ---
    auto decodeTexture = [&](uint32_t tbp0, uint32_t tw, uint32_t th, uint32_t tbw, uint32_t psm,
                             const GsTexa& texa) {
        const int w = 1 << tw, h = 1 << th;
        const int stride = int(tbw) * 64;
        const size_t base = kVramFreezeOffset + size_t(tbp0) * 256;
        const GsPixelFormat fmt = (psm == 1) ? GsPixelFormat::PSMCT24 : GsPixelFormat::PSMCT32;
        // PSMCT24 is the GS texture read: expand alpha via TEXA (Expand24To32).
        std::vector<uint8_t> rgba = SwizzleEngine::deswizzle(m_freeze + base, w, h, stride, fmt, nullptr, texa);
        AllocatedImage img = res.createImage({uint32_t(w), uint32_t(h)}, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        res.uploadToImage(img, rgba.data(), {uint32_t(w), uint32_t(h)}, VK_FORMAT_R8G8B8A8_UNORM);
        m_textures.push_back(img);
        m_textureKeys.push_back(tbp0);
    };
    for (size_t i = 0; i < prims.size(); i++) {
        if (prims[i].prim.type != 4 && prims[i].prim.type != 6) continue;
        const uint32_t psm = prims[i].tex0.psm;
        if (!recipes[i].textured || (psm != 0 && psm != 1)) continue;
        const uint32_t tbp0 = prims[i].tex0.tbp0;
        if (isFeedback(tbp0)) continue;  // framebuffer-aliased -> feedback path
        if (textureIndexFor(tbp0) < 0)
            decodeTexture(tbp0, prims[i].tex0.tw, prims[i].tex0.th, prims[i].tex0.tbw, psm, prims[i].texa);
    }

    const uint8_t white[4] = {255, 255, 255, 255};
    m_white = res.createImage({1, 1}, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    res.uploadToImage(m_white, white, {1, 1}, VK_FORMAT_R8G8B8A8_UNORM);

    // --- descriptor sets ---
    m_descAlloc.init(device, uint32_t(m_textures.size()) + 3,
        {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1.0f}});
    auto makeSet = [&](VkImageView view) {
        VkDescriptorSet set = m_descAlloc.allocate(m_setLayout);
        DescriptorWriter w;
        w.writeImage(0, view, m_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        w.updateSet(device, set);
        return set;
    };
    for (auto& t : m_textures) m_textureSets.push_back(makeSet(t.imageView));
    m_whiteSet = makeSet(m_white.imageView);
    m_vramReadSet = makeSet(m_vramRead.imageView);

    // --- shaders + layout + pipelines ---
    m_vert = ShaderLoader::loadModule(device, findShader("gsclock.vert.spv"));
    m_frag = ShaderLoader::loadModule(device, findShader("gsclock.frag.spv"));
    VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants)};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &m_setLayout;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(device, &pli, nullptr, &m_pipelineLayout);

    std::vector<VkVertexInputBindingDescription> bindings = {
        {0, sizeof(GpuVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
    std::vector<VkVertexInputAttributeDescription> attrs = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuVertex, pos)},
        {1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(GpuVertex, color)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuVertex, uv)}};
    auto buildPipeline = [&](const gsvk::GsBlendRecipe& blend) {
        VkPipelineColorBlendAttachmentState bs{};
        bs.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        bs.blendEnable = blend.enable;
        bs.srcColorBlendFactor = blend.enable ? blend.srcFactor : VK_BLEND_FACTOR_ONE;
        bs.dstColorBlendFactor = blend.enable ? blend.dstFactor : VK_BLEND_FACTOR_ZERO;
        bs.colorBlendOp = blend.colorOp;
        bs.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        bs.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        bs.alphaBlendOp = VK_BLEND_OP_ADD;
        PipelineBuilder pb;
        pb.setShaders(m_vert, m_frag)
            .setVertexInput(bindings, attrs)
            .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
            .setCullMode(VK_CULL_MODE_NONE)
            .setPolygonMode(VK_POLYGON_MODE_FILL)
            .setDepthTest(false, false, VK_COMPARE_OP_ALWAYS)
            .setBlendState(bs)
            .setColorFormat(m_targetFormat)
            .setDepthFormat(VK_FORMAT_UNDEFINED)
            .setPipelineLayout(m_pipelineLayout);
        return pb.build(device);
    };

    // --- geometry: render to VRAM rows; feedback UV over the VRAM image ---
    std::vector<GpuVertex> verts;
    verts.reserve(scene.stats().verts * 2);

    for (size_t i = 0; i < prims.size(); i++) {
        const auto& p = prims[i];
        const auto& r = recipes[i];
        const int type = p.prim.type;
        if (type != 4 && type != 6) continue;
        if (p.verts.size() < 2) continue;

        const float dstRow = float(fbRow(p.frame.fbp));
        int texIdx = -1;
        bool feedback = false;
        float texW = 1.0f, texH = 1.0f, texRow = 0.0f;
        if (r.textured && (p.tex0.psm == 0 || p.tex0.psm == 1)) {
            if (isFeedback(p.tex0.tbp0)) {  // framebuffer-as-texture
                feedback = true;
                texRow = float(fbRow(p.tex0.tbp0 / 32));
            } else {
                texIdx = textureIndexFor(p.tex0.tbp0);
                texW = float(1 << p.tex0.tw);
                texH = float(1 << p.tex0.th);
            }
        }

        auto mkVertex = [&](const GsVertex& v) {
            GpuVertex g{};
            g.pos[0] = v.x * 2.0f / float(kVramW) - 1.0f;
            g.pos[1] = (dstRow + v.y) * 2.0f / float(kVramH) - 1.0f;
            g.pos[2] = float(v.z) / 4294967295.0f;
            g.color[0] = v.r; g.color[1] = v.g; g.color[2] = v.b; g.color[3] = v.a;
            if (feedback) {            // sample the VRAM image (framebuffer feedback)
                const float u = p.prim.fst ? v.u : v.s * float(kVramW);
                const float vv = p.prim.fst ? v.v : v.t * 224.0f;
                g.uv[0] = u / float(kVramW);
                g.uv[1] = (texRow + vv) / float(kVramH);
            } else if (p.prim.fst) {   // resident UV
                g.uv[0] = v.u / texW; g.uv[1] = v.v / texH;
            } else {                   // resident STQ
                g.uv[0] = v.s; g.uv[1] = v.t;
            }
            return g;
        };

        const uint32_t first = static_cast<uint32_t>(verts.size());
        if (type == 6) {
            const GsVertex& a = p.verts[0];
            const GsVertex& bb = p.verts[1];
            GsVertex c00 = a, c10 = a, c11 = bb, c01 = bb;
            c10.x = bb.x; c10.u = bb.u; c10.y = a.y; c10.v = a.v;
            c01.x = a.x;  c01.u = a.u;  c01.y = bb.y; c01.v = bb.v;
            GpuVertex g00 = mkVertex(c00), g10 = mkVertex(c10), g11 = mkVertex(c11), g01 = mkVertex(c01);
            verts.push_back(g00); verts.push_back(g10); verts.push_back(g11);
            verts.push_back(g00); verts.push_back(g11); verts.push_back(g01);
        } else {
            std::vector<GpuVertex> strip;
            strip.reserve(p.verts.size());
            for (const auto& v : p.verts) strip.push_back(mkVertex(v));
            for (size_t k = 0; k + 2 < strip.size(); k++) {
                verts.push_back(strip[k]); verts.push_back(strip[k + 1]); verts.push_back(strip[k + 2]);
            }
        }
        const uint32_t count = static_cast<uint32_t>(verts.size()) - first;
        if (count == 0) continue;

        int pipeIdx = pipelineIndexFor(r.blend);
        if (pipeIdx < 0) {
            m_pipelineKeys.push_back(blendKey(r.blend));
            m_pipelines.push_back(buildPipeline(r.blend));
            pipeIdx = static_cast<int>(m_pipelines.size()) - 1;
        }

        Draw d{};
        d.firstVertex = first;
        d.vertexCount = count;
        d.pipelineIndex = pipeIdx;
        d.fbpRow = int(dstRow);
        d.textureIndex = texIdx;
        d.feedback = feedback;
        d.alphaEnable = r.alphaTest.enable ? 1 : 0;
        d.alphaGreater = (r.alphaTest.pass == VK_COMPARE_OP_GREATER) ? 1 : 0;
        d.alphaRef = float(r.alphaTest.ref) / 255.0f;
        d.textured = (texIdx >= 0 || feedback) ? 1 : 0;
        m_draws.push_back(d);
    }

    m_vertexCount = static_cast<uint32_t>(verts.size());
    if (m_vertexCount == 0) return;
    m_vbo = res.createBuffer(m_vertexCount * sizeof(GpuVertex),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    res.uploadToBuffer(m_vbo, verts.data(), m_vertexCount * sizeof(GpuVertex));

    std::fprintf(stderr, "GsRenderer: %zu draws, %u verts, %zu textures, %zu pipelines (VRAM %ux%u)\n",
                 m_draws.size(), m_vertexCount, m_textures.size(), m_pipelines.size(), kVramW, kVramH);
    std::fflush(stderr);
    m_ready = true;
}

void GsRenderer::record(PassRecorder& rec, VkImage dst, VkExtent2D dstExtent) {
    if (!m_ready) return;
    const VkCommandBuffer cmd = rec.cmd();

    // Re-seed both VRAM images from the freeze content.
    rec.transitionImage(m_vramSeed.image, m_seedLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    m_seedLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    rec.transitionImage(m_vramRead.image, m_readLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    rec.transitionImage(m_vramWrite.image, m_writeLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageCopy full{};
    full.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    full.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    full.extent = {kVramW, kVramH, 1};
    vkCmdCopyImage(cmd, m_vramSeed.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   m_vramRead.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &full);
    vkCmdCopyImage(cmd, m_vramSeed.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   m_vramWrite.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &full);
    m_readLayout = m_writeLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    auto setVP = [&](int rowOff) {
        VkViewport vp{0.0f, 0.0f, float(kVramW), float(kVramH), 0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        const int h = int(kFbH);
        const int y = rowOff < 0 ? 0 : rowOff;
        VkRect2D sc{{0, y}, {kVramW, uint32_t(h)}};
        vkCmdSetScissor(cmd, 0, 1, &sc);
    };

    size_t i = 0;
    while (i < m_draws.size()) {
        const int row = m_draws[i].fbpRow;
        size_t j = i;
        while (j < m_draws.size() && m_draws[j].fbpRow == row) j++;

        rec.transitionImage(m_vramRead.image, m_readLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_readLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        rec.transitionImage(m_vramWrite.image, m_writeLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        m_writeLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        rec.beginRendering(m_vramWrite.imageView, {kVramW, kVramH}, nullptr);
        setVP(row);
        rec.bindVertexBuffer(m_vbo.buffer);
        int boundPipeline = -1;
        for (size_t k = i; k < j; k++) {
            const Draw& d = m_draws[k];
            if (d.pipelineIndex != boundPipeline) {
                rec.bindPipeline(m_pipelines[d.pipelineIndex]);
                boundPipeline = d.pipelineIndex;
            }
            VkDescriptorSet set = d.feedback ? m_vramReadSet
                                  : d.textureIndex >= 0 ? m_textureSets[d.textureIndex]
                                                        : m_whiteSet;
            rec.bindDescriptorSet(m_pipelineLayout, 0, set);
            PushConstants pc{d.alphaRef, d.alphaEnable, d.alphaGreater, d.textured};
            rec.pushConstants(m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, &pc, sizeof(pc));
            vkCmdDraw(cmd, d.vertexCount, 1, d.firstVertex, 0);
        }
        rec.endRendering();

        // Copy the rendered framebuffer band write->read so later feedback sees it.
        const int h = std::min<int>(kFbH, int(kVramH) - row);
        if (h > 0) {
            rec.transitionImage(m_vramWrite.image, m_writeLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            m_writeLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            rec.transitionImage(m_vramRead.image, m_readLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            m_readLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            VkImageCopy band{};
            band.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            band.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            band.srcOffset = {0, row, 0};
            band.dstOffset = {0, row, 0};
            band.extent = {kVramW, uint32_t(h), 1};
            vkCmdCopyImage(cmd, m_vramWrite.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           m_vramRead.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &band);
        }
        i = j;
    }

    // Display = FBP 0 (rows [0,224]) of the VRAM, blitted to the main image.
    rec.transitionImage(m_vramWrite.image, m_writeLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    m_writeLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {int32_t(kVramW), int32_t(kFbH), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[1] = {int32_t(dstExtent.width), int32_t(dstExtent.height), 1};
    vkCmdBlitImage(cmd, m_vramWrite.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
}

void GsRenderer::destroy(const VulkanContext& ctx, ResourceManager& res) {
    const VkDevice device = ctx.device();
    if (m_vbo.buffer) res.destroyBuffer(m_vbo);
    for (auto& t : m_textures) res.destroyImage(t);
    if (m_white.image) res.destroyImage(m_white);
    if (m_vramRead.image) res.destroyImage(m_vramRead);
    if (m_vramWrite.image) res.destroyImage(m_vramWrite);
    if (m_vramSeed.image) res.destroyImage(m_vramSeed);
    for (auto p : m_pipelines) vkDestroyPipeline(device, p, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
    if (m_vert) vkDestroyShaderModule(device, m_vert, nullptr);
    if (m_frag) vkDestroyShaderModule(device, m_frag, nullptr);
    if (m_sampler) vkDestroySampler(device, m_sampler, nullptr);
    m_descAlloc.destroy();
}
