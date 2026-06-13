#include "app/GsRenderer.hpp"

#include <cstdio>

#include "gs/SwizzleEngine.hpp"
#include "gsvk/GsDrawRecipe.hpp"
#include "renderer/PipelineBuilder.hpp"
#include "renderer/ShaderLoader.hpp"

namespace {

// GS savestate v9: the 4MB VRAM (m_vm8) starts here in the freeze blob (validated
// in tools/vramdump). Texture byte address in VRAM = TBP0 * 256; FBP byte = FBP*8192,
// so a framebuffer's texture base is TBP0 = FBP * 32.
constexpr size_t kVramFreezeOffset = 433;
constexpr uint32_t kFbW = 640;
constexpr uint32_t kFbH = 224;

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

// FBP of a framebuffer that a texture base points at, or UINT32_MAX if not one.
uint32_t feedbackFbp(uint32_t tbp0) {
    return (tbp0 % 32 == 0) ? (tbp0 / 32) : 0xffffffffu;
}

}  // namespace

int GsRenderer::textureIndexFor(uint32_t tbp0, uint32_t, uint32_t) {
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

int GsRenderer::targetIndexFor(uint32_t fbp) {
    for (size_t i = 0; i < m_targets.size(); i++)
        if (m_targets[i].fbp == fbp) return static_cast<int>(i);
    return -1;
}

void GsRenderer::init(const VulkanContext& ctx, ResourceManager& res, const GsScene& scene,
                      VkFormat, VkFormat) {
    if (!scene.stats().loaded) return;
    const VkDevice device = ctx.device();
    m_freeze = scene.stream().freeze.data();
    const auto& prims = scene.stream().prims;
    const auto& recipes = scene.recipes();

    // --- sampler ---
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(device, &si, nullptr, &m_sampler);

    // --- descriptor set layout (binding 0 = combined image sampler) ---
    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 1;
    li.pBindings = &b;
    vkCreateDescriptorSetLayout(device, &li, nullptr, &m_setLayout);

    // --- create a render target per distinct GS framebuffer (FRAME.FBP) ---
    for (size_t i = 0; i < prims.size(); i++) {
        if (prims[i].prim.type != 4 && prims[i].prim.type != 6) continue;
        const uint32_t fbp = prims[i].frame.fbp;
        if (targetIndexFor(fbp) < 0) {
            Target t;
            t.fbp = fbp;
            t.img = res.createImage({kFbW, kFbH}, m_targetFormat,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
            m_targets.push_back(t);
        }
    }
    m_displayTarget = targetIndexFor(0);  // FBP 0 = display buffer
    if (m_displayTarget < 0 && !m_targets.empty()) m_displayTarget = 0;

    // --- decode resident textures (non-framebuffer PSMCT32) ---
    auto decodeTexture = [&](uint32_t tbp0, uint32_t tw, uint32_t th) {
        const int w = 1 << tw, h = 1 << th;
        const size_t base = kVramFreezeOffset + size_t(tbp0) * 256;
        std::vector<uint8_t> rgba = SwizzleEngine::deswizzle(
            m_freeze + base, w, h, GsPixelFormat::PSMCT32, nullptr);
        AllocatedImage img = res.createImage({uint32_t(w), uint32_t(h)},
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        res.uploadToImage(img, rgba.data(), {uint32_t(w), uint32_t(h)}, VK_FORMAT_R8G8B8A8_UNORM);
        m_textures.push_back(img);
        m_textureKeys.push_back(tbp0);
    };
    for (size_t i = 0; i < prims.size(); i++) {
        if (prims[i].prim.type != 4 && prims[i].prim.type != 6) continue;
        if (!recipes[i].textured || prims[i].tex0.psm != 0) continue;
        const uint32_t tbp0 = prims[i].tex0.tbp0;
        if (feedbackFbp(tbp0) != 0xffffffffu && targetIndexFor(feedbackFbp(tbp0)) >= 0) continue;
        if (textureIndexFor(tbp0, 0, 0) < 0) decodeTexture(tbp0, prims[i].tex0.tw, prims[i].tex0.th);
    }

    const uint8_t white[4] = {255, 255, 255, 255};
    m_white = res.createImage({1, 1}, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    res.uploadToImage(m_white, white, {1, 1}, VK_FORMAT_R8G8B8A8_UNORM);

    // --- descriptor sets (static) ---
    m_descAlloc.init(device, uint32_t(m_textures.size() + m_targets.size()) + 2,
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
    for (auto& t : m_targets) t.set = makeSet(t.img.imageView);
    m_whiteSet = makeSet(m_white.imageView);

    // --- shaders + layout ---
    m_vert = ShaderLoader::loadModule(device, "bin/shaders/gsclock.vert.spv");
    m_frag = ShaderLoader::loadModule(device, "bin/shaders/gsclock.frag.spv");
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

    // --- build geometry (triangle list) preserving submission order ---
    std::vector<GpuVertex> verts;
    verts.reserve(scene.stats().verts * 2);

    for (size_t i = 0; i < prims.size(); i++) {
        const auto& p = prims[i];
        const auto& r = recipes[i];
        const int type = p.prim.type;
        if (type != 4 && type != 6) continue;
        if (p.verts.size() < 2) continue;

        int texIdx = -1, texTgt = -1;
        float texW = 1.0f, texH = 1.0f;
        if (r.textured && p.tex0.psm == 0) {
            const uint32_t fb = feedbackFbp(p.tex0.tbp0);
            if (fb != 0xffffffffu && targetIndexFor(fb) >= 0) {
                texTgt = targetIndexFor(fb);
                texW = float(kFbW); texH = float(kFbH);
            } else {
                texIdx = textureIndexFor(p.tex0.tbp0, 0, 0);
                texW = float(1 << p.tex0.tw); texH = float(1 << p.tex0.th);
            }
        }

        auto mkVertex = [&](const GsVertex& v) {
            gsvk::ClipPos c = gsvk::toClip(v.x, v.y, v.z, kFbW, kFbH);
            GpuVertex g{};
            g.pos[0] = c.x; g.pos[1] = c.y; g.pos[2] = c.z;
            g.color[0] = v.r; g.color[1] = v.g; g.color[2] = v.b; g.color[3] = v.a;
            if (p.prim.fst) { g.uv[0] = v.u / texW; g.uv[1] = v.v / texH; }
            else { g.uv[0] = v.s; g.uv[1] = v.t; }
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
                verts.push_back(strip[k]);
                verts.push_back(strip[k + 1]);
                verts.push_back(strip[k + 2]);
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
        d.targetIndex = targetIndexFor(p.frame.fbp);
        d.textureIndex = texIdx;
        d.texTargetIndex = texTgt;
        d.alphaEnable = r.alphaTest.enable ? 1 : 0;
        d.alphaGreater = (r.alphaTest.pass == VK_COMPARE_OP_GREATER) ? 1 : 0;
        d.alphaRef = float(r.alphaTest.ref) / 255.0f;
        d.textured = (texIdx >= 0 || texTgt >= 0) ? 1 : 0;
        m_draws.push_back(d);
    }

    m_vertexCount = static_cast<uint32_t>(verts.size());
    if (m_vertexCount == 0 || m_displayTarget < 0) return;

    m_vbo = res.createBuffer(m_vertexCount * sizeof(GpuVertex),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    res.uploadToBuffer(m_vbo, verts.data(), m_vertexCount * sizeof(GpuVertex));

    std::fprintf(stderr, "GsRenderer: %zu draws, %u verts, %zu targets, %zu textures, %zu pipelines\n",
                 m_draws.size(), m_vertexCount, m_targets.size(), m_textures.size(), m_pipelines.size());
    std::fflush(stderr);
    m_ready = true;
}

void GsRenderer::record(PassRecorder& rec, VkImage dst, VkExtent2D dstExtent) {
    if (!m_ready) return;
    const VkExtent2D fb{kFbW, kFbH};
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    // Clear all framebuffers to black at frame start.
    const VkClearColorValue black{{0.0f, 0.0f, 0.0f, 1.0f}};
    for (auto& t : m_targets) {
        rec.transitionImage(t.img.image, t.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdClearColorImage(rec.cmd(), t.img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &black, 1, &range);
        t.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }

    // Ordered replay: contiguous same-target runs.
    size_t i = 0;
    while (i < m_draws.size()) {
        const int tgt = m_draws[i].targetIndex;
        size_t j = i;
        while (j < m_draws.size() && m_draws[j].targetIndex == tgt) j++;

        // Transition feedback sources sampled in this run to SHADER_READ (no self-feedback).
        for (size_t k = i; k < j; k++) {
            const int ft = m_draws[k].texTargetIndex;
            if (ft >= 0 && ft != tgt && m_targets[ft].layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                rec.transitionImage(m_targets[ft].img.image, m_targets[ft].layout,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                m_targets[ft].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
        }
        Target& T = m_targets[tgt];
        if (T.layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            rec.transitionImage(T.img.image, T.layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            T.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        rec.beginRendering(T.img.imageView, fb, nullptr);  // LOAD (keep prior content)
        rec.setViewportScissor(fb);
        rec.bindVertexBuffer(m_vbo.buffer);
        int boundPipeline = -1;
        for (size_t k = i; k < j; k++) {
            const Draw& d = m_draws[k];
            if (d.pipelineIndex != boundPipeline) {
                rec.bindPipeline(m_pipelines[d.pipelineIndex]);
                boundPipeline = d.pipelineIndex;
            }
            VkDescriptorSet set = d.texTargetIndex >= 0 ? m_targets[d.texTargetIndex].set
                                  : d.textureIndex >= 0 ? m_textureSets[d.textureIndex]
                                                        : m_whiteSet;
            rec.bindDescriptorSet(m_pipelineLayout, 0, set);
            PushConstants pc{d.alphaRef, d.alphaEnable, d.alphaGreater, d.textured};
            rec.pushConstants(m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, &pc, sizeof(pc));
            vkCmdDraw(rec.cmd(), d.vertexCount, 1, d.firstVertex, 0);
        }
        rec.endRendering();
        i = j;
    }

    // Blit the display buffer into the app's main color image.
    Target& disp = m_targets[m_displayTarget];
    rec.transitionImage(disp.img.image, disp.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    disp.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {int32_t(kFbW), int32_t(kFbH), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[1] = {int32_t(dstExtent.width), int32_t(dstExtent.height), 1};
    vkCmdBlitImage(rec.cmd(), disp.img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
}

void GsRenderer::destroy(const VulkanContext& ctx, ResourceManager& res) {
    const VkDevice device = ctx.device();
    if (m_vbo.buffer) res.destroyBuffer(m_vbo);
    for (auto& t : m_textures) res.destroyImage(t);
    for (auto& t : m_targets) res.destroyImage(t.img);
    if (m_white.image) res.destroyImage(m_white);
    for (auto p : m_pipelines) vkDestroyPipeline(device, p, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
    if (m_vert) vkDestroyShaderModule(device, m_vert, nullptr);
    if (m_frag) vkDestroyShaderModule(device, m_frag, nullptr);
    if (m_sampler) vkDestroySampler(device, m_sampler, nullptr);
    m_descAlloc.destroy();
}
