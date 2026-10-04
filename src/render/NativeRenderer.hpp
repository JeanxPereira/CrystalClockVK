#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <tuple>
#include <vector>

#include "render/Device.hpp"
#include "scene/Font.hpp"
#include "scene/Frame.hpp"
#include "scene/Text.hpp"

namespace render {

// The resolution the frame is drawn at (its scale is height / 448: the NTSC frame of two 224-line fields)
// and the MSAA sample count, which replaces the OSD's edge antialiasing.
struct NativeOutput {
    uint32_t width = 640, height = 448;
    uint32_t samples = 1;
    bool operator==(const NativeOutput&) const = default;
};

// Draws a scene::Frame with an ordinary graphics pipeline: float blending, no GS rules.
// Units (scene/Frame.hpp): colour bytes, alpha 0x80 = 1.0. Targets store alpha / 128, so a target read back
// or sampled gives the alpha byte the OSD would have; readTarget converts at that boundary.
class NativeRenderer {
public:
    NativeRenderer(Device& device, const std::filesystem::path& shaderDirectory);
    ~NativeRenderer();
    NativeRenderer(const NativeRenderer&) = delete;
    NativeRenderer& operator=(const NativeRenderer&) = delete;

    void setTexture(int32_t number, uint32_t width, uint32_t height, std::span<const uint8_t> rgba);
    // The ten clock textures (References/textures, written by References/scripts/extract_textures.mjs and
    // checked against the ROM by extract_rom_textures.mjs), texture n at its 640 x 224 address.
    void loadClockTextures(const std::filesystem::path& directory);
    // The glyph cache (scene::kGlyphTexture) a frame's text samples; made again only when its cells change.
    void setGlyphCache(const scene::Font& font, const scene::GlyphCache& cache);

    void configure(const NativeOutput& output);
    const NativeOutput& output() const { return m_output; }

    void record(VkCommandBuffer cmd, const scene::Frame& frame);
    void draw(const scene::Frame& frame);
    std::vector<uint8_t> readTarget(scene::TargetName target);
    // `image` is in COLOR_ATTACHMENT_OPTIMAL and is left so; the target is fitted in `extent` at `aspect`
    // (width / height of the shown picture) with black around it.
    void present(VkCommandBuffer cmd, VkImage image, VkExtent2D extent, scene::TargetName shown, float aspect);

    // The largest alpha a blended pass multiplies by (As or the constant, in bytes): vertex alpha, times the
    // texture's largest alpha / 128 when textured. Target texels are bounded by 0x80 (they store alpha / 128).
    uint32_t blendAlpha(const scene::Pass& pass) const;

private:
    struct Image {
        VkImage image{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
        VmaAllocation allocation{VK_NULL_HANDLE};
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkImageAspectFlags aspect{VK_IMAGE_ASPECT_COLOR_BIT};
        uint32_t width{0}, height{0};
        VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
        VkPipelineStageFlags2 stage{VK_PIPELINE_STAGE_2_NONE};
        VkAccessFlags2 access{0};
    };
    struct Target {
        Image resolved;
        Image multisampled;
    };
    struct Buffer {
        VkBuffer buffer{VK_NULL_HANDLE};
        VmaAllocation allocation{VK_NULL_HANDLE};
        void* mapped{nullptr};
        VkDeviceSize size{0};
    };
    struct Texture {
        Image image;
        uint8_t largestAlpha{0};
    };
    using PipelineKey = std::tuple<int, int, bool, bool, uint32_t>;

    Image createImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkSampleCountFlagBits samples, VkImageAspectFlags aspect);
    void destroyImage(Image& image);
    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage);
    void destroyBuffer(Buffer& buffer);
    void destroyTargets();
    void submit(const std::function<void(VkCommandBuffer)>& record);
    void transition(VkCommandBuffer cmd, Image& image, VkImageLayout layout, VkPipelineStageFlags2 stage, VkAccessFlags2 access);
    VkPipeline pipeline(const scene::Pass& pass);
    Target& target(scene::TargetName name) { return m_targets[static_cast<size_t>(name)]; }
    void beginRendering(VkCommandBuffer cmd, Target& target);

    Device& m_device;
    VkShaderModule m_vertex{VK_NULL_HANDLE}, m_fragment{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_setLayout{VK_NULL_HANDLE};
    VkPipelineLayout m_layout{VK_NULL_HANDLE};
    std::array<VkSampler, 4> m_samplers{};
    std::map<PipelineKey, VkPipeline> m_pipelines;
    VkCommandPool m_pool{VK_NULL_HANDLE};
    VkCommandBuffer m_commands{VK_NULL_HANDLE};
    std::map<int32_t, Texture> m_textures;
    Image m_blank;
    std::array<Target, 3> m_targets{};
    Image m_depth;
    NativeOutput m_output{0, 0, 0};
    std::array<Buffer, 3> m_vertices{};
    uint32_t m_slot{0};
    scene::GlyphCache m_glyphs;
};

}  // namespace render
