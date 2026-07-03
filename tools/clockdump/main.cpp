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
#include "clock/ClockState.hpp"
#include "clock/Projection.hpp"
#include "clock/RodField.hpp"
#include "clock/ClockRenderer.hpp"
#include "app/TimeSync.hpp"

int main(int argc, char** argv) {
    std::string outPath = "ours.rgba";
    // Time source: real wall clock by default; --time HH:MM:SS forces a value
    // (deterministic renders / matching a specific dump).
    int fHour = -1, fMin = 0, fSec = 0;
    bool prism = false;
    for (int a = 1; a < argc; ++a) {
        std::string arg = argv[a];
        if (arg == "--dump-rgba" && a + 1 < argc) outPath = argv[++a];
        else if (arg == "--time" && a + 1 < argc)
            std::sscanf(argv[++a], "%d:%d:%d", &fHour, &fMin, &fSec);
        else if (arg == "--prism") prism = true;
    }

    int hour, minute, second;
    if (fHour >= 0) { hour = fHour; minute = fMin; second = fSec; }
    else {
        const TimeInfo t = TimeSync::getCurrentTime();
        hour = t.hour; minute = t.minute; second = t.second;
    }
    const ps2clock::ClockState clock = ps2clock::ClockState::fromTime(hour, minute, second);
    std::fprintf(stderr, "clock_dump: %02d:%02d:%02d -> litRod=%d fill=%.3f %s\n",
                 hour, minute, second, clock.litRod, clock.fill,
                 clock.amPm == ps2clock::AmPm::PM ? "PM" : "AM");

    const VkExtent2D extent{640, 224};

    // WindowContext() takes no arguments — opens a 1280x720 window.
    WindowContext window;
    VulkanContext ctx(window);
    ResourceManager resources(ctx);

    const VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM;
    AllocatedImage target = resources.createImage(extent, fmt,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    // Orthographic camera framing the 12-rod dial (XY plane, centred at origin).
    // The dial (radius ~outerRadius) fills the 224px height; wide side margins match
    // the 640x224 format (the real clock is centred). Vulkan NDC Y is down -> flip Y.
    const float halfH = 8.0f;
    const float halfW = halfH * (static_cast<float>(extent.width) / static_cast<float>(extent.height));
    ps2clock::Mat4 mvp(1.0f);
    mvp[0][0] = 1.0f / halfW;   // world X -> NDC X
    mvp[1][1] = -1.0f / halfH;  // world +Y (up) -> NDC -Y (Vulkan down) so up stays up
    mvp[2][2] = 1.0f;           // Z passthrough (depth test off in the flat pipeline)

    // Prism mode: face-on ortho with a slight tilt + depth mapping so the bar
    // thickness (Z) reads and the depth test sorts facets. Column-major GLM.
    if (prism) {
        const float tilt = 0.28f;  // radians, tip the dial so top faces show
        const float ct = std::cos(tilt), st = std::sin(tilt);
        ps2clock::Mat4 m(1.0f);
        // world (x,y,z) -> tilt about X, then ortho scale, Z into [0,1].
        m[0][0] = 1.0f / halfW;
        m[1][1] = -ct / halfH;  m[2][1] = -st / halfH;   // Y gets a bit of Z
        m[1][2] =  st * 0.02f;   m[2][2] = ct * 0.02f;    // depth (small range)
        m[3][2] = 0.5f;
        mvp = m;
    }

    // ── Diagnostic: print each rod's clock position + NDC of its centre ────────
    const ps2clock::RodField field = ps2clock::RodField::Generate();
    std::fprintf(stderr, "=== Rod diagnostic (%zu rods) ===\n", field.rods.size());
    std::fprintf(stderr, "  %-4s %-6s  %-8s %-8s  %-8s %-8s\n",
                 "rod", "hour", "dir.x", "dir.y", "ndc.x", "ndc.y");
    for (int i = 0; i < static_cast<int>(field.rods.size()); ++i) {
        const ps2clock::Rod& r = field.rods[i];
        const ps2clock::Vec4 clip = mvp * ps2clock::Vec4(r.center, 1.0f);
        std::fprintf(stderr, "  %-4d %-6d  %8.3f %8.3f  %8.4f %8.4f\n",
                     i, r.hour, r.direction.x, r.direction.y, clip.x, clip.y);
    }
    std::fprintf(stderr, "===============================\n");

    ClockRenderer renderer(ctx, resources, fmt, extent);
    if (prism) renderer.setPrismMesh(field.buildDialPrism(clock));
    else       renderer.setDialMesh(field.buildDialMesh(clock));

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
