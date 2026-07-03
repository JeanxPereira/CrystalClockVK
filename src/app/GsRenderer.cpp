#include "app/GsRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "gs/SwizzleEngine.hpp"
#include "gsvk/GsDrawRecipe.hpp"
#include "renderer/PipelineBuilder.hpp"
#include "renderer/ShaderLoader.hpp"

namespace {

// GS savestate v9: 4MB VRAM (m_vm8) starts here in the freeze. 425, NOT 433:
// derived from an external oracle — PCSX2's own texture dump of the button
// glyphs (TBP0=11968) matches our deswizzle 2048/2048 pixels at 425 and is
// uniformly shifted by 2 words at 433.
constexpr size_t kVramFreezeOffset = 425;
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
    int texaExpand24;
    float texaTA0;
    int texaAEM;
};

uint64_t pipeKey(const gsvk::GsBlendRecipe& b, const gsvk::GsDepthState& d, bool lines) {
    return (uint64_t(lines) << 62) | (uint64_t(b.fix) << 53) |
           (uint64_t(d.compare) << 48) | (uint64_t(d.writeEnable) << 44) |
           (uint64_t(d.testEnable) << 42) |
           (uint64_t(b.enable) << 40) | (uint64_t(b.colorOp) << 32) |
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

int GsRenderer::pipelineIndexFor(uint64_t key) {
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
    // Resident textures: every clock draw uses CLAMP WMS/WMT=0 (REPEAT) — the
    // tunnel background tiles its 128x128 texture ~3x via STQ.
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    vkCreateSampler(device, &si, nullptr, &m_samplerRepeat);

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
    m_vramMS = res.createImage({kVramW, kVramH}, m_targetFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VMA_MEMORY_USAGE_GPU_ONLY, VK_SAMPLE_COUNT_4_BIT);
    m_vramDepth = res.createImage({kVramW, kVramH}, VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VMA_MEMORY_USAGE_GPU_ONLY,
        VK_SAMPLE_COUNT_4_BIT);
    {
        std::vector<uint8_t> vram = SwizzleEngine::deswizzle(
            m_freeze + kVramFreezeOffset, kVramW, kVramH, kVramW, GsPixelFormat::PSMCT32, nullptr);
        m_vramSeed = res.createImage({kVramW, kVramH}, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        res.uploadToImage(m_vramSeed, vram.data(), {kVramW, kVramH}, VK_FORMAT_R8G8B8A8_UNORM);
    }

    // --- resident (non-framebuffer) textures, PSMCT32/24, swizzle by TBW*64 ---
    // TEX0 TW/TH can LIE: the Visor icon/glyph sprites declare 64x64 but their
    // FST=1 UVs reach v~440 — the font strip lives BELOW the declared texture
    // in VRAM. PCSX2's HW renderer (the reference frames) resolves this by
    // sourcing the rect the UVs actually cover, so decode an EXPANDED image
    // when UV-addressed draws exceed the declared size. STQ draws keep the
    // declared size + REPEAT (the tunnel background genuinely tiles).
    std::vector<float> uvMaxU, uvMaxV;  // per-texture, indexed like m_textures
    auto uvBoundsFor = [&](uint32_t tbp0) {
        float mu = 0.0f, mv = 0.0f;
        for (size_t i = 0; i < prims.size(); i++) {
            if (!recipes[i].textured || prims[i].tex0.tbp0 != tbp0 || !prims[i].prim.fst) continue;
            for (const auto& v : prims[i].verts) {
                mu = std::max<float>(mu, v.u);
                mv = std::max<float>(mv, v.v);
            }
        }
        return std::pair<float, float>(mu, mv);
    };
    auto decodeTexture = [&](uint32_t tbp0, uint32_t tw, uint32_t th, uint32_t tbw, uint32_t psm,
                             uint32_t cbp, const GsTexa& texa) {
        int w = 1 << tw, h = 1 << th;
        const auto [mu, mv] = uvBoundsFor(tbp0);
        w = std::max<int>(w, int(std::ceil(mu)));
        h = std::max<int>(h, int(std::ceil(mv)));
        const int stride = int(tbw) * 64;
        const size_t base = kVramFreezeOffset + size_t(tbp0) * 256;
        // Clamp height to the end of the 4MB VRAM.
        const size_t vramLeft = (4u << 20) - size_t(tbp0) * 256;
        const int bytesPerRow = (psm == 20) ? stride / 2 : stride * 4;
        h = std::min<int>(h, int(vramLeft / size_t(bytesPerRow)));
        std::vector<uint8_t> rgba;
        if (psm == 20) {  // PSMT4 (the text font): 16-entry CSM1 CLUT at CBP.
            static const int kClutT32I4[16] = {0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15};
            std::vector<uint8_t> clut(256 * 4, 0);
            for (int i = 0; i < 16; i++)
                std::memcpy(&clut[i * 4],
                            m_freeze + kVramFreezeOffset + size_t(cbp) * 256 + size_t(kClutT32I4[i]) * 4, 4);
            rgba = SwizzleEngine::deswizzle(m_freeze + base, w, h, stride, GsPixelFormat::PSMT4, clut.data());
        } else {
            const GsPixelFormat fmt = (psm == 1) ? GsPixelFormat::PSMCT24 : GsPixelFormat::PSMCT32;
            // PSMCT24 is the GS texture read: expand alpha via TEXA (Expand24To32).
            rgba = SwizzleEngine::deswizzle(m_freeze + base, w, h, stride, fmt, nullptr, texa);
        }
        AllocatedImage img = res.createImage({uint32_t(w), uint32_t(h)}, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        res.uploadToImage(img, rgba.data(), {uint32_t(w), uint32_t(h)}, VK_FORMAT_R8G8B8A8_UNORM);
        m_textures.push_back(img);
        m_textureKeys.push_back(tbp0);
        uvMaxU.push_back(float(w));
        uvMaxV.push_back(float(h));
    };
    for (size_t i = 0; i < prims.size(); i++) {
        if (prims[i].prim.type != 4 && prims[i].prim.type != 6) continue;
        const uint32_t psm = prims[i].tex0.psm;
        if (!recipes[i].textured || (psm != 0 && psm != 1 && psm != 20)) continue;
        const uint32_t tbp0 = prims[i].tex0.tbp0;
        if (isFeedback(tbp0)) continue;  // framebuffer-aliased -> feedback path
        if (textureIndexFor(tbp0) < 0)
            decodeTexture(tbp0, prims[i].tex0.tw, prims[i].tex0.th, prims[i].tex0.tbw, psm,
                          prims[i].tex0.cbp, prims[i].texa);
    }

    const uint8_t white[4] = {255, 255, 255, 255};
    m_white = res.createImage({1, 1}, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    res.uploadToImage(m_white, white, {1, 1}, VK_FORMAT_R8G8B8A8_UNORM);

    // --- descriptor sets ---
    m_descAlloc.init(device, uint32_t(m_textures.size()) + 4,
        {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1.0f}});
    auto makeSet = [&](VkImageView view, VkSampler sampler) {
        VkDescriptorSet set = m_descAlloc.allocate(m_setLayout);
        DescriptorWriter w;
        w.writeImage(0, view, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        w.updateSet(device, set);
        return set;
    };
    for (auto& t : m_textures) m_textureSets.push_back(makeSet(t.imageView, m_samplerRepeat));
    m_whiteSet = makeSet(m_white.imageView, m_sampler);
    m_vramReadSet = makeSet(m_vramRead.imageView, m_sampler);
    m_seedSet = makeSet(m_vramSeed.imageView, m_sampler);

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
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuVertex, uvq)}};
    auto buildPipeline = [&](const gsvk::GsBlendRecipe& blend, const gsvk::GsDepthState& depth,
                             bool lines) {
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
        // GS FIX coefficient: C = FIX/128 per channel, via the blend constant.
        const float fixC = float(blend.fix) / 128.0f;
        pb.setShaders(m_vert, m_frag)
            .setVertexInput(bindings, attrs)
            .setTopology(lines ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
                               : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
            .setBlendConstants(fixC, fixC, fixC, fixC)
            .setCullMode(VK_CULL_MODE_NONE)
            .setPolygonMode(VK_POLYGON_MODE_FILL)
            .setDepthTest(depth.testEnable, depth.writeEnable, depth.compare)
            .setBlendState(bs)
            .setColorFormat(m_targetFormat)
            .setDepthFormat(VK_FORMAT_D32_SFLOAT)
            .setSamples(VK_SAMPLE_COUNT_4_BIT)
            .setPipelineLayout(m_pipelineLayout);
        return pb.build(device);
    };

    // Fullscreen blit pipeline: seeds the MSAA VRAM image from m_vramSeed.
    m_blitVert = ShaderLoader::loadModule(device, findShader("blit.vert.spv"));
    m_blitFrag = ShaderLoader::loadModule(device, findShader("blit.frag.spv"));
    {
        VkPipelineColorBlendAttachmentState bs{};
        bs.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        PipelineBuilder pb;
        pb.setShaders(m_blitVert, m_blitFrag)
            .setVertexInput({}, {})
            .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
            .setCullMode(VK_CULL_MODE_NONE)
            .setPolygonMode(VK_POLYGON_MODE_FILL)
            .setDepthTest(false, false, VK_COMPARE_OP_ALWAYS)
            .setBlendState(bs)
            .setColorFormat(m_targetFormat)
            .setDepthFormat(VK_FORMAT_UNDEFINED)
            .setSamples(VK_SAMPLE_COUNT_4_BIT)
            .setPipelineLayout(m_pipelineLayout);
        m_blitPipeline = pb.build(device);
    }

    // --- geometry: render to VRAM rows; feedback UV over the VRAM image ---
    std::vector<GpuVertex> verts;
    verts.reserve(scene.stats().verts * 2);

    for (size_t i = 0; i < prims.size(); i++) {
        const auto& p = prims[i];
        const auto& r = recipes[i];
        const int type = p.prim.type;
        if (type != 2 && type != 4 && type != 6) continue;
        if (p.verts.size() < 2) continue;

        const float dstRow = float(fbRow(p.frame.fbp));
        int texIdx = -1;
        bool feedback = false;
        float texW = 1.0f, texH = 1.0f, texRow = 0.0f;
        float stqU = 1.0f, stqV = 1.0f;  // STQ texel scale relative to decoded dims
        if (r.textured && (p.tex0.psm == 0 || p.tex0.psm == 1 || p.tex0.psm == 20)) {
            texW = float(1 << p.tex0.tw);
            texH = float(1 << p.tex0.th);
            if (isFeedback(p.tex0.tbp0)) {  // framebuffer-as-texture
                feedback = true;
                texRow = float(fbRow(p.tex0.tbp0 / 32));
            } else {
                texIdx = textureIndexFor(p.tex0.tbp0);
                if (texIdx >= 0) {
                    // Decoded image may be larger than the declared 2^TW x 2^TH
                    // (UV-expanded); normalize against the real dimensions.
                    stqU = texW / uvMaxU[texIdx];
                    stqV = texH / uvMaxV[texIdx];
                    texW = uvMaxU[texIdx];
                    texH = uvMaxV[texIdx];
                }
            }
        }

        auto mkVertex = [&](const GsVertex& v) {
            GpuVertex g{};
            // GS raster samples pixel centers at INTEGER coords; Vulkan at
            // integer+0.5. Shift geometry +0.5px so VK coverage decisions land
            // on the GS sample grid (else every .5-aligned edge is 1px off).
            g.pos[0] = (v.x + 0.5f) * 2.0f / float(kVramW) - 1.0f;
            g.pos[1] = (dstRow + v.y + 0.5f) * 2.0f / float(kVramH) - 1.0f;
            g.pos[2] = float(v.z) / 4294967295.0f;
            g.color[0] = v.r; g.color[1] = v.g; g.color[2] = v.b; g.color[3] = v.a;
            const float q = (v.q != 0.0f) ? v.q : 1.0f;
            g.uvq[2] = 1.0f;
            if (feedback) {            // sample the VRAM image (framebuffer feedback)
                // Texel coords wrap (REPEAT) or clamp per the draw's CLAMP mode.
                // The clock's refraction passes bake a +256 v offset into the UVs
                // (TH=8): the REPEAT wrap lands back inside the source
                // framebuffer band — 210<->280 ping-pong, never the glyph atlas.
                // No draw crosses a wrap boundary (verified on clock_viewer.gs),
                // so per-vertex wrapping is exact.
                float u = p.prim.fst ? v.u : (v.s / q) * texW;
                float vv = p.prim.fst ? v.v : (v.t / q) * texH;
                u = (p.clamp.wms == 1) ? std::clamp(u, 0.0f, texW) : std::fmod(u, texW);
                vv = (p.clamp.wmt == 1) ? std::clamp(vv, 0.0f, texH) : std::fmod(vv, texH);
                g.uvq[0] = u / float(kVramW);
                g.uvq[1] = (texRow + vv) / float(kVramH);
            } else if (p.prim.fst) {   // resident UV (normalized to decoded dims)
                g.uvq[0] = v.u / texW; g.uvq[1] = v.v / texH;
            } else {                   // resident STQ: GS divides per pixel —
                // S,T,Q interpolate linearly in screen space, the shader does
                // S/Q. stqU/V rescale declared-texel space onto the (possibly
                // UV-expanded) decoded image; premultiplying S keeps it linear.
                g.uvq[0] = v.s * stqU; g.uvq[1] = v.t * stqV; g.uvq[2] = q;
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
        } else if (type == 2) {  // LINE_STRIP -> line list
            for (size_t k = 0; k + 1 < p.verts.size(); k++) {
                verts.push_back(mkVertex(p.verts[k]));
                verts.push_back(mkVertex(p.verts[k + 1]));
            }
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

        const bool lines = (type == 2);
        gsvk::GsBlendRecipe blend = r.blend;
        if (lines && p.prim.aa1 && !blend.enable) {
            // GS AA1 forces alpha blending regardless of ABE, using the CURRENT
            // ALPHA register with As = coverage (PCSX2 GSDrawScanline: aa1 sets
            // sel.abe path with ababcd from the register). The clock's swirl
            // lines carry ALPHA=0201 (additive): Cs*cov + Cd — bright blue
            // heads, tails fade out as the per-vertex color ramps to black.
            // SRC1 (vertex alpha, ramping with the color) stands in for the
            // per-pixel coverage; MSAA supplies the spatial part.
            blend = gsvk::translateBlend(p.alpha, true);
        }
        const uint64_t key = pipeKey(blend, r.depth, lines);
        int pipeIdx = pipelineIndexFor(key);
        if (pipeIdx < 0) {
            m_pipelineKeys.push_back(key);
            m_pipelines.push_back(buildPipeline(blend, r.depth, lines));
            pipeIdx = static_cast<int>(m_pipelines.size()) - 1;
        }

        Draw d{};
        d.firstVertex = first;
        d.vertexCount = count;
        d.primIndex = p.index;
        d.pipelineIndex = pipeIdx;
        d.fbpRow = int(dstRow);
        d.textureIndex = texIdx;
        d.feedback = feedback;
        d.alphaEnable = r.alphaTest.enable ? 1 : 0;
        d.alphaGreater = (r.alphaTest.pass == VK_COMPARE_OP_GREATER) ? 1 : 0;
        d.alphaRef = float(r.alphaTest.ref) / 255.0f;
        d.textured = (texIdx >= 0 || feedback) ? 1 : 0;
        // PSMCT24 framebuffer feedback: the framebuffer stores no alpha (we write
        // 1.0), so the GS texture read expands it via TEXA in the shader. Resident
        // PSMCT24 textures are already TEXA-expanded on the CPU (deswizzle).
        d.texaExpand24 = (feedback && p.tex0.psm == 1) ? 1 : 0;
        d.texaTa0 = float(p.texa.ta0) / 255.0f;
        d.texaAem = p.texa.aem ? 1 : 0;
        m_draws.push_back(d);
    }

    m_vertexCount = static_cast<uint32_t>(verts.size());
    if (m_vertexCount == 0) return;
    m_vbo = res.createBuffer(m_vertexCount * sizeof(GpuVertex),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    res.uploadToBuffer(m_vbo, verts.data(), m_vertexCount * sizeof(GpuVertex));

    size_t texa24 = 0;
    for (const auto& d : m_draws) texa24 += d.texaExpand24;
    std::fprintf(stderr, "GsRenderer: %zu draws (%zu PSMCT24-feedback TEXA), %u verts, %zu textures, %zu pipelines (VRAM %ux%u)\n",
                 m_draws.size(), texa24, m_vertexCount, m_textures.size(), m_pipelines.size(), kVramW, kVramH);
    std::fflush(stderr);
    m_ready = true;
}

void GsRenderer::record(PassRecorder& rec, VkImage dst, VkExtent2D dstExtent) {
    if (!m_ready) return;
    const VkCommandBuffer cmd = rec.cmd();

    // Re-seed the VRAM images from the freeze content: copy into read/write
    // (1x), and blit into the MSAA image via a fullscreen draw (a 1x image
    // cannot be copied into a multisampled one).
    rec.transitionImage(m_vramSeed.image, m_seedLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
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

    rec.transitionImage(m_vramSeed.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_seedLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    rec.transitionImage(m_vramMS.image, m_msLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    m_msLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    rec.beginRenderingMS(m_vramMS.imageView, VK_NULL_HANDLE, VK_NULL_HANDLE,
                         {{0, 0}, {kVramW, kVramH}}, VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                         VK_ATTACHMENT_LOAD_OP_DONT_CARE, 0.0f);
    {
        VkViewport vp{0.0f, 0.0f, float(kVramW), float(kVramH), 0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{{0, 0}, {kVramW, kVramH}};
        vkCmdSetScissor(cmd, 0, 1, &sc);
        rec.bindPipeline(m_blitPipeline);
        rec.bindDescriptorSet(m_pipelineLayout, 0, m_seedSet);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    rec.endRendering();

    auto setVP = [&](int rowOff) {
        VkViewport vp{0.0f, 0.0f, float(kVramW), float(kVramH), 0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        const int h = int(kFbH);
        const int y = rowOff < 0 ? 0 : rowOff;
        VkRect2D sc{{0, y}, {kVramW, uint32_t(h)}};
        vkCmdSetScissor(cmd, 0, 1, &sc);
    };

    rec.transitionImage(m_vramDepth.image, m_depthLayout, VK_IMAGE_LAYOUT_GENERAL);
    m_depthLayout = VK_IMAGE_LAYOUT_GENERAL;
    bool firstPass = true;
    int prevRow = -1;

    size_t i = 0;
    while (i < m_draws.size()) {
        const int row = m_draws[i].fbpRow;
        size_t j = i;
        while (j < m_draws.size() && m_draws[j].fbpRow == row) j++;

        rec.transitionImage(m_vramRead.image, m_readLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_readLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        rec.transitionImage(m_vramWrite.image, m_writeLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        m_writeLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

        // The GS has ONE Z-buffer (ZBP 140) shared by every framebuffer: rods
        // rendered into FBP 280 occlude lines drawn into FBP 0. Our targets are
        // row-regions, so mirror the shared Z by copying the previous band's
        // depth into this band's rows on every band switch (the single GS Z
        // plane evolving in draw order). Clear it once at frame start.
        const int bandH = std::min<int>(int(kFbH), int(kVramH) - row);
        if (!firstPass && prevRow >= 0 && prevRow != row) {
            rec.transitionImage(m_vramDepth.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            VkImageCopy zc{};
            zc.srcSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
            zc.dstSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
            zc.srcOffset = {0, prevRow, 0};
            zc.dstOffset = {0, row, 0};
            zc.extent = {kVramW, uint32_t(bandH), 1};
            vkCmdCopyImage(cmd, m_vramDepth.image, VK_IMAGE_LAYOUT_GENERAL,
                           m_vramDepth.image, VK_IMAGE_LAYOUT_GENERAL, 1, &zc);
            rec.transitionImage(m_vramDepth.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        }
        rec.beginRenderingMS(m_vramMS.imageView, m_vramWrite.imageView, m_vramDepth.imageView,
                             {{0, row < 0 ? 0 : row}, {kVramW, uint32_t(bandH)}},
                             VK_ATTACHMENT_LOAD_OP_LOAD,
                             firstPass ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
                             0.0f);
        firstPass = false;
        prevRow = row;
        setVP(row);
        rec.bindVertexBuffer(m_vbo.buffer);
        int boundPipeline = -1;
        for (size_t k = i; k < j; k++) {
            const Draw& d = m_draws[k];
            if (m_stopAtPrim >= 0 && int(d.primIndex) >= m_stopAtPrim) continue;
            if (d.pipelineIndex != boundPipeline) {
                rec.bindPipeline(m_pipelines[d.pipelineIndex]);
                boundPipeline = d.pipelineIndex;
            }
            VkDescriptorSet set = d.feedback ? m_vramReadSet
                                  : d.textureIndex >= 0 ? m_textureSets[d.textureIndex]
                                                        : m_whiteSet;
            rec.bindDescriptorSet(m_pipelineLayout, 0, set);
            PushConstants pc{d.alphaRef, d.alphaEnable, d.alphaGreater, d.textured,
                             d.texaExpand24, d.texaTa0, d.texaAem};
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

VkExtent2D GsRenderer::displayExtent() const { return {kVramW, kFbH}; }

std::vector<uint8_t> GsRenderer::readbackDisplay(ResourceManager& res) const {
    // FBP 0 occupies VRAM rows [0, kFbH). m_writeLayout is TRANSFER_SRC after record().
    return res.downloadImage(m_vramWrite, {0, 0}, {kVramW, kFbH}, m_writeLayout);
}

std::vector<uint8_t> GsRenderer::readbackVram(ResourceManager& res) const {
    return res.downloadImage(m_vramWrite, {0, 0}, {kVramW, kVramH}, m_writeLayout);
}

void GsRenderer::destroy(const VulkanContext& ctx, ResourceManager& res) {
    const VkDevice device = ctx.device();
    if (m_vbo.buffer) res.destroyBuffer(m_vbo);
    for (auto& t : m_textures) res.destroyImage(t);
    if (m_white.image) res.destroyImage(m_white);
    if (m_vramRead.image) res.destroyImage(m_vramRead);
    if (m_vramWrite.image) res.destroyImage(m_vramWrite);
    if (m_vramMS.image) res.destroyImage(m_vramMS);
    if (m_vramSeed.image) res.destroyImage(m_vramSeed);
    if (m_vramDepth.image) res.destroyImage(m_vramDepth);
    if (m_blitPipeline) vkDestroyPipeline(device, m_blitPipeline, nullptr);
    if (m_blitVert) vkDestroyShaderModule(device, m_blitVert, nullptr);
    if (m_blitFrag) vkDestroyShaderModule(device, m_blitFrag, nullptr);
    for (auto p : m_pipelines) vkDestroyPipeline(device, p, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
    if (m_vert) vkDestroyShaderModule(device, m_vert, nullptr);
    if (m_frag) vkDestroyShaderModule(device, m_frag, nullptr);
    if (m_sampler) vkDestroySampler(device, m_sampler, nullptr);
    if (m_samplerRepeat) vkDestroySampler(device, m_samplerRepeat, nullptr);
    m_descAlloc.destroy();
}
