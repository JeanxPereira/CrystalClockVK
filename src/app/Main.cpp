#include "render/Device.hpp"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iterator>

namespace {
// Clear colour of the clock screen: fixture whole3-clock frames[0].input.clearColour = [0,0,0,128] (alpha 0x80 = 1.0).
constexpr float kClear[4] = {0.0f, 0.0f, 0.0f, 1.0f};

struct SmokeStep {
    Uint64 at;
    void (*act)(SDL_Window*);
};
}  // namespace

int main(int argc, char** argv) {
    bool smoke = false;
    bool validation = true;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--smoke")) smoke = true;
        if (!std::strcmp(argv[i], "--no-validation")) validation = false;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("Crystal Clock", 1280, 896, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::fprintf(stderr, "window: %s\n", SDL_GetError());
        return 1;
    }
    int code = 0;
    try {
        render::Device device(window, {validation});
        const SmokeStep steps[] = {
            {1000, [](SDL_Window* w) { SDL_SetWindowSize(w, 800, 600); }},
            {2000, [](SDL_Window* w) { SDL_SetWindowSize(w, 1024, 700); }},
            {2500, [](SDL_Window* w) { SDL_MinimizeWindow(w); }},
            {3500, [](SDL_Window* w) { SDL_RestoreWindow(w); }},
            {4500, [](SDL_Window* w) { SDL_SetWindowSize(w, 1280, 896); }},
        };
        constexpr Uint64 kSmokeEnd = 5500;
        size_t next = 0;
        const Uint64 start = SDL_GetTicks();
        uint64_t presented = 0;
        bool running = true;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) running = false;
            }
            const Uint64 elapsed = SDL_GetTicks() - start;
            if (smoke) {
                while (next < std::size(steps) && elapsed >= steps[next].at) steps[next++].act(window);
                if (elapsed >= kSmokeEnd) running = false;
            }
            auto frame = device.beginFrame();
            if (!frame) {
                SDL_Delay(10);
                continue;
            }
            VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            color.imageView = frame->view;
            color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color.clearValue.color = {{kClear[0], kClear[1], kClear[2], kClear[3]}};
            VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
            rendering.renderArea = {{0, 0}, device.swapchainExtent()};
            rendering.layerCount = 1;
            rendering.colorAttachmentCount = 1;
            rendering.pColorAttachments = &color;
            vkCmdBeginRendering(frame->cmd, &rendering);
            vkCmdEndRendering(frame->cmd);
            device.endFrame(*frame);
            ++presented;
        }
        vkDeviceWaitIdle(device.device());
        if (smoke) std::printf("smoke: %llu frames presented, %u validation errors\n", static_cast<unsigned long long>(presented), device.validationErrors());
        if (device.validationErrors() != 0) code = 1;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fatal: %s\n", error.what());
        code = 1;
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return code;
}
