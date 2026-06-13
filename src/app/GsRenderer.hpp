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
    void record(PassRecorder& rec, VkExtent2D extent);
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
        int textureIndex;  // -1 => untextured (white)
        float alphaRef;
        int alphaEnable;
        int alphaGreater;
        int textured;
    };

    int textureIndexFor(uint32_t tbp0, uint32_t tw, uint32_t th);
    int pipelineIndexFor(const gsvk::GsBlendRecipe& blend);

    bool m_ready = false;
    const uint8_t* m_freeze = nullptr;

    std::vector<Draw> m_draws;
    AllocatedBuffer m_vbo{};
    uint32_t m_vertexCount = 0;

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
