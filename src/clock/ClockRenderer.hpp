#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

#include "core/VulkanContext.hpp"
#include "renderer/ResourceManager.hpp"
#include "renderer/PassRecorder.hpp"
#include "clock/ClockMath.hpp"
#include "clock/RodField.hpp"

class ClockRenderer {
public:
    ClockRenderer(const VulkanContext& ctx, ResourceManager& resources,
                  VkFormat colorFormat, VkExtent2D extent);
    ~ClockRenderer();

    ClockRenderer(const ClockRenderer&) = delete;
    ClockRenderer& operator=(const ClockRenderer&) = delete;

    // Upload the rod field mesh to GPU buffers (white placeholder body).
    void setRodField(const ps2clock::RodField& field);

    // Upload an arbitrary pre-built mesh (e.g. RodField::buildDialMesh with the
    // clock-state colours). Replaces any mesh set by setRodField.
    void setDialMesh(const ps2clock::FlatMesh& mesh);

    // Upload a 3D prism mesh (RodField::buildDialPrism); record() then draws it
    // with the crystal-facet shader + depth test instead of the flat pipeline.
    void setPrismMesh(const ps2clock::PrismMesh& mesh);

    // Background clear colour for record() (default black).
    void setClearColor(float r, float g, float b) { m_clear[0]=r; m_clear[1]=g; m_clear[2]=b; }

    // Record the flat vertex-color draw into the given color target.
    void record(PassRecorder& recorder, VkImageView colorView, const ps2clock::Mat4& mvp);

private:
    const VulkanContext& m_ctx;
    ResourceManager& m_resources;
    VkExtent2D m_extent;

    VkPipelineLayout m_layout{VK_NULL_HANDLE};
    VkPipeline m_pipeline{VK_NULL_HANDLE};       // flat (2D dial)
    VkPipeline m_prismPipeline{VK_NULL_HANDLE};  // 3D crystal prism
    VkFormat m_colorFormat{};

    AllocatedBuffer m_vbo{};
    AllocatedBuffer m_ibo{};
    uint32_t m_indexCount{0};
    bool m_prism{false};                         // which pipeline record() uses
    AllocatedImage m_depth{};
    float m_clear[3]{0.0f, 0.0f, 0.0f};
};
