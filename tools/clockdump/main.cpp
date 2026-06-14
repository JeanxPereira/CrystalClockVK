#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include <climits>

#include "core/WindowContext.hpp"
#include "core/VulkanContext.hpp"
#include "renderer/ResourceManager.hpp"
#include "renderer/PassRecorder.hpp"
#include "clock/ClockMath.hpp"
#include "clock/Projection.hpp"
#include "clock/RodField.hpp"
#include "clock/ClockRenderer.hpp"

int main(int argc, char** argv) {
    std::string outPath = "ours.rgba";
    for (int a = 1; a < argc; ++a) {
        std::string arg = argv[a];
        if (arg == "--dump-rgba" && a + 1 < argc) outPath = argv[++a];
    }

    const VkExtent2D extent{640, 224};

    // WindowContext() takes no arguments — opens a 1280x720 window.
    WindowContext window;
    VulkanContext ctx(window);
    ResourceManager resources(ctx);

    const VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM;
    AllocatedImage target = resources.createImage(extent, fmt,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    // Fitted projection (rod0-locked). Maps world -> GS pixel space [0..2048].
    // fov/near are provisional fit values (vu0-math-pipeline.md §Blocker 1).
    ps2clock::ProjectionParams pp;
    pp.fov = 1.047f;
    pp.nearPlane = 1.0f;
    pp.halfWidth = 512.0f;
    const ps2clock::Mat4 proj = ps2clock::BuildProjectionMatrix(pp);

    // Normalize GS pixel space [0..2048] -> NDC [-1..1] for the render target.
    // GLM column-major: m[col][row]. toNdc[0][0]=col0.x, toNdc[3][0]=col3.x.
    ps2clock::Mat4 toNdc(1.0f);
    toNdc[0][0] = 2.0f / 2048.0f;  // scale X
    toNdc[3][0] = -1.0f;            // translate X: maps GS 0 -> NDC -1
    toNdc[1][1] = 2.0f / 2048.0f;  // scale Y
    toNdc[3][1] = -1.0f;            // translate Y: maps GS 0 -> NDC -1
    const ps2clock::Mat4 mvp = toNdc * proj;

    // ── Diagnostic: print screen + NDC position for each rod ──────────────────
    const ps2clock::RodField field = ps2clock::RodField::Generate();
    std::fprintf(stderr, "=== Rod diagnostic (8 rods) ===\n");
    std::fprintf(stderr, "  %-4s  %-10s %-10s %-10s  %-10s %-10s  %-8s %-8s\n",
                 "rod", "world.x", "world.y", "world.z",
                 "gs.x", "gs.y", "ndc.x", "ndc.y");
    for (int i = 0; i < static_cast<int>(field.rods.size()); ++i) {
        const ps2clock::Vec3& w = field.rods[i].world;
        const glm::vec2 gs = ps2clock::ProjectWorldToScreen(proj, w);
        // toNdc is a simple scale+translate: ndcX = gs.x * (2/2048) - 1
        const float ndcX = gs.x * toNdc[0][0] + toNdc[3][0];
        const float ndcY = gs.y * toNdc[1][1] + toNdc[3][1];
        std::fprintf(stderr, "  %-4d  %10.3f %10.3f %10.3f  %10.3f %10.3f  %8.4f %8.4f\n",
                     i, w.x, w.y, w.z, gs.x, gs.y, ndcX, ndcY);
    }
    std::fprintf(stderr, "===============================\n");

    ClockRenderer renderer(ctx, resources, fmt, extent);
    renderer.setRodField(field);

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.queueFamilyIndex = ctx.graphicsQueueFamily();
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool{};
    vkCreateCommandPool(ctx.device(), &pci, nullptr, &pool);

    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd{};
    vkAllocateCommandBuffers(ctx.device(), &ai, &cmd);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    {
        PassRecorder recorder(cmd);
        recorder.transitionImage(target.image, VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        renderer.record(recorder, target.imageView, mvp);
        recorder.transitionImage(target.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    }
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(ctx.graphicsQueue(), 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.graphicsQueue());

    const std::vector<uint8_t> px = resources.downloadImage(
        target, {0, 0}, extent, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    std::ofstream out(outPath, std::ios::binary);
    out.write(reinterpret_cast<const char*>(px.data()), static_cast<std::streamsize>(px.size()));
    out.close();
    std::printf("clock_dump: wrote %zu bytes (640x224 RGBA8) -> %s\n", px.size(), outPath.c_str());

    // ── Diagnostic: lit-pixel stats ────────────────────────────────────────────
    int litCount = 0;
    int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
    const int W = static_cast<int>(extent.width);
    const int H = static_cast<int>(extent.height);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const size_t idx = static_cast<size_t>((y * W + x) * 4);
            if (px[idx] || px[idx + 1] || px[idx + 2]) {
                ++litCount;
                if (x < minX) minX = x;
                if (y < minY) minY = y;
                if (x > maxX) maxX = x;
                if (y > maxY) maxY = y;
            }
        }
    }

    if (litCount == 0) {
        std::fprintf(stderr, "clock_dump: lit pixels = 0 (frame is ALL BLACK)\n");
        std::fprintf(stderr, "clock_dump: rods are off-screen — projection needs toNdc adjustment\n");
    } else {
        std::fprintf(stderr, "clock_dump: lit pixels = %d  bbox=(%d,%d)-(%d,%d)\n",
                     litCount, minX, minY, maxX, maxY);
        std::fprintf(stderr, "clock_dump: OK\n");
    }

    vkDestroyCommandPool(ctx.device(), pool, nullptr);
    resources.destroyImage(target);

    return (litCount == 0) ? 1 : 0;
}
