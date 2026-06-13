#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

#include "core/VulkanContext.hpp"
#include "renderer/ResourceManager.hpp"
#include "renderer/PassRecorder.hpp"
#include "renderer/DescriptorAllocator.hpp"
#include "app/GsScene.hpp"

// Renders a loaded GsScene: decodes the freeze textures, builds one triangle-list
// vertex buffer from the draws (sprites + strips expanded, transformed to clip
// space via gsvk::toClip), and replays each draw through a gsvk-derived pipeline.
// First textured pass: depth off (painter's order), lines/points skipped.
class GsRenderer {
public:
    void init(const VulkanContext& ctx, ResourceManager& res, const GsScene& scene,
              VkFormat colorFormat, VkFormat depthFormat);
    // Ordered multi-target replay into the GS framebuffers, then blit the display
    // buffer into `dst` (the app's main color image, in TRANSFER_DST layout on entry,
    // left in TRANSFER_DST on exit).
    void record(PassRecorder& rec, VkImage dst, VkExtent2D dstExtent);
    void destroy(const VulkanContext& ctx, ResourceManager& res);

    bool ready() const { return m_ready; }
    uint32_t triangles() const { return m_vertexCount / 3; }
    uint32_t drawCount() const { return static_cast<uint32_t>(m_draws.size()); }

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
        int targetIndex;     // GS framebuffer this draw renders to
        int textureIndex;    // resident texture, or -1
        int texTargetIndex;  // sample another framebuffer (feedback), or -1
        float alphaRef;
        int alphaEnable;
        int alphaGreater;
        int textured;
    };

    // One GS framebuffer (FBP) as a VK render target + sampleable image. `seed`
    // holds the framebuffer's freeze VRAM content (textures alias framebuffers in
    // GS unified memory); it re-seeds `img` each frame instead of a black clear.
    struct Target {
        uint32_t fbp = 0;
        AllocatedImage img{};
        AllocatedImage seed{};
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout seedLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };

    int textureIndexFor(uint32_t tbp0, uint32_t tw, uint32_t th);
    int pipelineIndexFor(const gsvk::GsBlendRecipe& blend);
    int targetIndexFor(uint32_t fbp);

    bool m_ready = false;
    const uint8_t* m_freeze = nullptr;
    VkFormat m_targetFormat = VK_FORMAT_R8G8B8A8_UNORM;

    std::vector<Draw> m_draws;
    AllocatedBuffer m_vbo{};
    uint32_t m_vertexCount = 0;

    std::vector<Target> m_targets;
    int m_displayTarget = -1;

    VkSampler m_sampler = VK_NULL_HANDLE;
    std::vector<AllocatedImage> m_textures;
    std::vector<VkDescriptorSet> m_textureSets;
    std::vector<uint32_t> m_textureKeys;  // tbp0 of each decoded texture
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
