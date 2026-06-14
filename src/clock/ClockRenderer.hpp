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

    // Upload the rod field mesh to GPU buffers.
    void setRodField(const ps2clock::RodField& field);

    // Record the flat vertex-color draw into the given color target.
    void record(PassRecorder& recorder, VkImageView colorView, const ps2clock::Mat4& mvp);

private:
    const VulkanContext& m_ctx;
    ResourceManager& m_resources;
    VkExtent2D m_extent;

    VkPipelineLayout m_layout{VK_NULL_HANDLE};
    VkPipeline m_pipeline{VK_NULL_HANDLE};

    AllocatedBuffer m_vbo{};
    AllocatedBuffer m_ibo{};
    uint32_t m_indexCount{0};
};
