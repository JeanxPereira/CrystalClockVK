#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

#include "core/VulkanContext.hpp"
#include "renderer/ResourceManager.hpp"
#include "renderer/PassRecorder.hpp"
#include "renderer/DescriptorAllocator.hpp"
#include "app/GsScene.hpp"

// Renders a loaded GsScene faithfully via a GPU "VRAM texture-cache": one 640xH
// linear image pair (ping-pong) models GS unified memory — each GS framebuffer is
// a row-region (FBP row = fbp*8192/2560), draws render to their region, and
// framebuffer-as-texture feedback samples by linear UV. The rendered region is
// copied write->read after each run so later feedback sees current content. All in
// one command buffer (no CPU readback). Resident narrow textures stay separate.
class GsRenderer {
public:
    void init(const VulkanContext& ctx, ResourceManager& res, const GsScene& scene,
              VkFormat colorFormat, VkFormat depthFormat);
    // Replays into the VRAM ping-pong, then blits the display framebuffer (FBP 0)
    // into `dst` (main color image, TRANSFER_DST on entry, left TRANSFER_DST).
    void record(PassRecorder& rec, VkImage dst, VkExtent2D dstExtent);
    void destroy(const VulkanContext& ctx, ResourceManager& res);

    bool ready() const { return m_ready; }
    uint32_t drawCount() const { return static_cast<uint32_t>(m_draws.size()); }

    // Native GS display frame (FBP 0, 640x224) for numeric pixel-diff. Call after
    // record() — m_vramWrite holds the rendered frame in TRANSFER_SRC layout.
    VkExtent2D displayExtent() const;
    std::vector<uint8_t> readbackDisplay(ResourceManager& res) const;

private:
    struct GpuVertex {
        float pos[3];
        uint8_t color[4];
        float uv[2];
    };
    struct Draw {
        uint32_t firstVertex;
        uint32_t vertexCount;
        int pipelineIndex;
        int fbpRow;          // target framebuffer's VRAM row offset
        int textureIndex;    // resident texture, or -1
        bool feedback;       // samples the VRAM image (framebuffer-as-texture)
        float alphaRef;
        int alphaEnable;
        int alphaGreater;
        int textured;
        int texaExpand24;   // PSMCT24 feedback: expand alpha via TEXA in-shader
        float texaTa0;      // TA0 / 255
        int texaAem;        // AEM
    };

    int textureIndexFor(uint32_t tbp0);
    int pipelineIndexFor(uint64_t key);

    bool m_ready = false;
    const uint8_t* m_freeze = nullptr;
    VkFormat m_targetFormat = VK_FORMAT_R8G8B8A8_UNORM;

    std::vector<Draw> m_draws;
    AllocatedBuffer m_vbo{};
    uint32_t m_vertexCount = 0;

    // VRAM ping-pong: m_vramRead is sampled, m_vramWrite is rendered; m_vramSeed
    // holds the freeze-deswizzled initial state (re-seeded each frame).
    AllocatedImage m_vramRead{};
    AllocatedImage m_vramWrite{};
    AllocatedImage m_vramSeed{};
    // GS Z-buffer (ZBP 140, PSMZ32, shared by every draw): one depth plane over
    // the VRAM image; cleared to 0 per frame (GS z: larger = nearer).
    AllocatedImage m_vramDepth{};
    VkImageLayout m_depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout m_readLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout m_writeLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout m_seedLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorSet m_vramReadSet = VK_NULL_HANDLE;

    VkSampler m_sampler = VK_NULL_HANDLE;
    std::vector<AllocatedImage> m_textures;
    std::vector<VkDescriptorSet> m_textureSets;
    std::vector<uint32_t> m_textureKeys;
    AllocatedImage m_white{};
    VkDescriptorSet m_whiteSet = VK_NULL_HANDLE;

    DescriptorAllocator m_descAlloc;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkShaderModule m_vert = VK_NULL_HANDLE;
    VkShaderModule m_frag = VK_NULL_HANDLE;
    std::vector<uint64_t> m_pipelineKeys;
    std::vector<VkPipeline> m_pipelines;
};
