#pragma once

#include "core/VulkanContext.hpp"
#include "renderer/SwapchainManager.hpp"
#include "renderer/ResourceManager.hpp"
#include "renderer/PassRecorder.hpp"
#include "renderer/PipelineBuilder.hpp"
#include "gs/GsRegisterState.hpp"
#include "app/CrystalGeometry.hpp"
#include "app/CrystalMath.hpp"
#include "app/TimeSync.hpp"
#include "renderer/DescriptorAllocator.hpp"
#include <glm/glm.hpp>
#include <array>

struct FrameParams {
    TimeInfo time;
    VkExtent2D extent;
    float aspect;
    float totalTime; // Continuous app time for animations (seconds)
    VkDevice device; // For descriptor allocation
    VkImageView currentImageView; // The framebuffer to read from in Local Read
    uint32_t frameIndex;
};

struct CrystalPushConstants {
    glm::mat4 mvp;
    glm::vec4 rodColor;
    glm::vec4 screenParams; // x=width, y=height, z=time, w=rodAlpha
};

class RenderOrchestrator {
public:
    void init(const VulkanContext& ctx, const SwapchainManager& swapchain, ResourceManager& resources);
    void recordFrame(PassRecorder& recorder, const FrameParams& params);
    void destroy(VkDevice device, ResourceManager& resources);

    VkPipelineLayout pipelineLayout() const { return m_pipelineLayout; }

private:
    void createPipelines(VkDevice device, VkFormat colorFormat);
    void uploadMesh(ResourceManager& resources);

    static glm::vec4 getRodColor(int rodIndex, float dayNight);

    // Descriptors for Local Read
    VkDescriptorSetLayout m_inputAttachmentLayout{VK_NULL_HANDLE};
    DescriptorAllocator m_descriptorAllocator[2]; // One per frame-in-flight

    // Pipelines
    VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
    VkPipeline m_tunnelPipeline{VK_NULL_HANDLE};
    VkPipeline m_glassPipeline{VK_NULL_HANDLE};
    VkPipeline m_specularPipeline{VK_NULL_HANDLE};
    VkPipeline m_reversePipeline{VK_NULL_HANDLE};

    // Mesh
    AllocatedBuffer m_vertexBuffer{};
    uint32_t m_vertexCount{0};

    // GS register config per pass (validation)
    static constexpr int PASS_COUNT = 5;
    std::array<GsAlpha, PASS_COUNT> m_passAlpha;
};
