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
    glm::vec4 refractA;    // x=eta, y=refractScale, z=refractBoost, w=rimStrength
    glm::vec4 refractB;    // x=emissiveBase, y=diffuseMix, z=reflectStrength, w=fadeAlpha
    glm::vec4 tintA;       // rgb=color1, w=tintLerp override (<0 = animate)
    glm::vec4 tintB;       // rgb=color2, w=colorPeriod
    glm::vec4 lightDir[3];
    glm::vec4 lightColor[3];
    glm::vec4 ambient;     // rgb=ambient, w=icon lighting enable
};

struct TestSceneParams {
    bool enabled = false;
    int bgMode = 2;
    float bgScale = 8.0f;
    float bgScrollSpeed = 0.0f;
    glm::vec3 bgColor1{0.0f, 0.0f, 0.0f};
    glm::vec3 bgColor2{1.0f, 1.0f, 1.0f};
    float eta = 0.5f;
    float refractScale = 1.0f;
    float refractBoost = 4.0f;
    float rimStrength = 1.0f;
    float emissiveBase = 0.25f;
    float diffuseMix = 0.2f;
    float reflectStrength = 0.6f;
    float fadeAlpha = 1.0f;
    bool animateTint = true;
    float tintLerp = 0.0f;
    float colorPeriod = 20.0f;
    glm::vec3 tint1{1.0f, 0.25f, 1.0f};
    glm::vec3 tint2{0.25f, 1.0f, 1.0f};
    int composition = 0;
    glm::mat4 cubeModel{1.0f};
    bool iconLighting = true;
    glm::vec3 lightDir[3] = {
        {0.5f, 0.5f, 0.5f}, {0.0f, -0.4f, -1.0f}, {-0.5f, -0.5f, 0.5f}};
    glm::vec3 lightColor[3] = {
        {0.4794f, 0.4794f, 0.4306f}, {0.2599f, 0.3294f, 0.5f}, {0.14f, 0.14f, 0.38f}};
    glm::vec3 ambient{0.24f, 0.24f, 0.24f};

    void loadIconPreset(int preset) {
        if (preset == 0) {
            lightDir[0] = {0.5f, 0.5f, 0.5f};
            lightDir[1] = {0.0f, -0.4f, -1.0f};
            lightDir[2] = {-0.5f, -0.5f, 0.5f};
            lightColor[0] = {0.4794f, 0.4794f, 0.4306f};
            lightColor[1] = {0.2599f, 0.3294f, 0.5f};
            lightColor[2] = {0.14f, 0.14f, 0.38f};
        } else {
            lightDir[0] = {0.5f, 0.2f, 0.5f};
            lightDir[1] = {-0.5f, 0.5f, 0.0f};
            lightDir[2] = {0.0f, -1.0f, 0.0f};
            lightColor[0] = {0.4f, 0.4f, 0.4f};
            lightColor[1] = {0.2f, 0.2f, 0.2f};
            lightColor[2] = {0.0f, 0.0f, 0.0f};
        }
        ambient = {0.24f, 0.24f, 0.24f};
    }
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
    void recordTestBackgroundPass(PassRecorder& recorder, const FrameParams& params, const TestSceneParams& test);
    void recordTestCubePass(PassRecorder& recorder, const FrameParams& params, const TestSceneParams& test);
    void updateUBO(const FrameParams& params, const TestSceneParams* test = nullptr);
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
    VkPipelineLayout m_testBgLayout{VK_NULL_HANDLE};
    VkPipeline m_testBgPipeline{VK_NULL_HANDLE};
    VkPipeline m_testCubePipeline{VK_NULL_HANDLE};

    // Meshes
    AllocatedBuffer m_rodVertexBuffer{};
    uint32_t m_rodVertexCount{0};
    AllocatedBuffer m_tunnelVertexBuffer{};
    uint32_t m_tunnelVertexCount{0};
    AllocatedBuffer m_cubeVertexBuffer{};
    uint32_t m_cubeVertexCount{0};

    // GS register config per pass
    static constexpr int PASS_COUNT = 5;
    std::array<GsAlpha, PASS_COUNT> m_passAlpha;

    // Per-rod runtime state
    std::array<RodData, CrystalMath::ROD_COUNT> m_rodState{};

    const VulkanContext* m_ctx{nullptr};
};
