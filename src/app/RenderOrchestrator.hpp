#pragma once

#include "core/VulkanContext.hpp"
#include "renderer/SwapchainManager.hpp"
#include "renderer/ResourceManager.hpp"
#include "renderer/PassRecorder.hpp"
#include "renderer/PipelineBuilder.hpp"
#include "gs/GsRegisterState.hpp"
#include "gs/GsConstants.hpp"
#include "gs/GsCrystalMath.hpp"
#include "app/CrystalGeometry.hpp"
#include "app/CrystalMath.hpp"
#include "app/TimeSync.hpp"
#include "renderer/DescriptorAllocator.hpp"
#include <glm/glm.hpp>
#include <array>

struct FrameUBO {
    glm::mat4 viewProj;
    glm::mat4 view;
    glm::vec4 viewPos;     // xyz = camera position, w = unused
    glm::vec4 prismColor;  // rgb = cycling color, a = time
};

struct FrameParams {
    TimeInfo time;
    VkExtent2D extent;
    float aspect;
    float totalTime;
    VkDevice device;
    VkImageView currentImageView;
    uint32_t frameIndex;
    VkImageView tunnelImageView;
};

struct CrystalPushConstants {
    glm::mat4 model;
    glm::vec4 rodColor;
    glm::vec4 screenParams; // x=width, y=height, z=time, w=rodAlpha
};

// Per-rod runtime state (mirrors OSDSYS rod struct key fields)
struct RodData {
    bool selected;        // +0x150: selection flag (active hour rod)
    int screenRatio;      // +0xAC: screen ratio (0x10=16:9, 0x0E=4:3)
    float yScale;         // +0x60: computed Y scale
};

class RenderOrchestrator {
public:
    void init(const VulkanContext& ctx, const SwapchainManager& swapchain, ResourceManager& resources);
    void recordTunnelPass(PassRecorder& recorder, const FrameParams& params);
    void recordCrystalPasses(PassRecorder& recorder, const FrameParams& params);
    void updateUBO(const FrameParams& params);
    void destroy(VkDevice device, ResourceManager& resources);

    VkPipelineLayout pipelineLayout() const { return m_pipelineLayout; }

private:
    void createDescriptorResources(const VulkanContext& ctx);
    void createPipelines(VkDevice device, VkFormat colorFormat);
    void uploadMeshes(ResourceManager& resources);
    void loadTextures(ResourceManager& resources);

    // Rod state management
    void updateRodStates(const FrameParams& params);

    // Descriptors
    VkDescriptorSetLayout m_descriptorLayout{VK_NULL_HANDLE};
    DescriptorAllocator m_descriptorAllocator[2];
    VkSampler m_sampler{VK_NULL_HANDLE};
    VkDescriptorSet m_tunnelDescSet[2]{};
    VkDescriptorSet m_crystalDescSet[2]{};

    // UBO
    AllocatedBuffer m_uboBuffer[2]{};

    // Textures
    AllocatedImage m_noiseTexture{};
    AllocatedImage m_normalTexture{};

    // Pipelines
    VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
    VkPipeline m_tunnelPipeline{VK_NULL_HANDLE};
    VkPipeline m_glassPipeline{VK_NULL_HANDLE};
    VkPipeline m_specularPipeline{VK_NULL_HANDLE};
    VkPipeline m_reversePipeline{VK_NULL_HANDLE};

    // Meshes
    AllocatedBuffer m_rodVertexBuffer{};
    uint32_t m_rodVertexCount{0};
    AllocatedBuffer m_tunnelVertexBuffer{};
    uint32_t m_tunnelVertexCount{0};

    // GS register config per pass
    static constexpr int PASS_COUNT = 5;
    std::array<GsAlpha, PASS_COUNT> m_passAlpha;

    // Per-rod runtime state
    std::array<RodData, CrystalMath::ROD_COUNT> m_rodState{};

    const VulkanContext* m_ctx{nullptr};
};
