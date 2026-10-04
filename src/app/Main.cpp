#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "../../tests/scene/SceneInputs.hpp"
#include "app/DebugPanel.hpp"
#include "app/Png.hpp"
#include "render/Device.hpp"
#include "render/NativeRenderer.hpp"
#include "scene/Clock.hpp"

namespace {

using Clock = scene::Clock<scene::NativeArithmetic>;
using std::chrono::system_clock;

struct Options {
    bool validation = true;
    bool smoke = false;
    double soak = 0;
    std::filesystem::path shaders = CLOCK_SHADERS;
    std::filesystem::path textures = CLOCK_TEXTURES;
    std::filesystem::path start = CLOCK_START;
    std::filesystem::path mesh = CLOCK_MESH;
    std::filesystem::path screenshots = CLOCK_SCREENSHOTS;
    std::filesystem::path font = CLOCK_FONT;
    std::filesystem::path program = CLOCK_PROGRAM;
};

// The OSD's time keeper gives hours, minutes, seconds and the milliseconds into the second; here the local time.
scene::ClockTime clockTime(system_clock::time_point now) {
    const auto local = std::chrono::current_zone()->to_local(now);
    const auto second = std::chrono::floor<std::chrono::seconds>(local);
    const std::chrono::hh_mm_ss day{second - std::chrono::floor<std::chrono::days>(local)};
    const float milliseconds = std::chrono::duration<float, std::milli>(local - second).count();
    return {milliseconds, static_cast<int32_t>(day.seconds().count()), static_cast<int32_t>(day.minutes().count()), static_cast<int32_t>(day.hours().count())};
}

// Configuration items 6 to 0xB, which the clock screen's text formats: the local date and time.
scene::ClockItems clockItems(system_clock::time_point now) {
    const auto local = std::chrono::current_zone()->to_local(now);
    const auto day = std::chrono::floor<std::chrono::days>(local);
    const std::chrono::year_month_day date{day};
    const std::chrono::hh_mm_ss time{std::chrono::floor<std::chrono::seconds>(local - day)};
    return {static_cast<int32_t>(date.year()), static_cast<int32_t>(static_cast<unsigned>(date.month())), static_cast<int32_t>(static_cast<unsigned>(date.day())),
            static_cast<int32_t>(time.hours().count()), static_cast<int32_t>(time.minutes().count()), static_cast<int32_t>(time.seconds().count())};
}

std::chrono::seconds secondsOfDay(system_clock::time_point now) {
    const auto local = std::chrono::current_zone()->to_local(now);
    return std::chrono::floor<std::chrono::seconds>(local - std::chrono::floor<std::chrono::days>(local));
}

// The picture is 640 x 448 (two 224-line fields) at a scale of height / 448; "window" takes the largest that fits.
render::NativeOutput outputFor(const app::PanelState& panel, VkExtent2D window, float aspect) {
    uint32_t height = 448;
    switch (panel.resolution) {
    case app::Resolution::Native: height = 448; break;
    case app::Resolution::Double: height = 896; break;
    case app::Resolution::Quadruple: height = 1792; break;
    case app::Resolution::Window:
        height = std::max<uint32_t>(2, std::min<uint32_t>(window.height, static_cast<uint32_t>(float(window.width) / aspect)));
        break;
    }
    return {std::max<uint32_t>(1, (640 * height + 224) / 448), height, panel.samples};
}

struct Step {
    double at;
    std::function<void()> act;
};

}  // namespace

int main(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool more = i + 1 < argc;
        if (arg == "--smoke") options.smoke = true;
        else if (arg == "--no-validation") options.validation = false;
        else if (arg == "--soak" && more) options.soak = std::atof(argv[++i]);
        else if (arg == "--shaders" && more) options.shaders = argv[++i];
        else if (arg == "--textures" && more) options.textures = argv[++i];
        else if (arg == "--start" && more) options.start = argv[++i];
        else if (arg == "--mesh" && more) options.mesh = argv[++i];
        else if (arg == "--screenshots" && more) options.screenshots = argv[++i];
        else if (arg == "--font" && more) options.font = argv[++i];
        else if (arg == "--program" && more) options.program = argv[++i];
        else {
            std::fprintf(stderr, "usage: CrystalClock [--smoke] [--soak seconds] [--no-validation] [--shaders dir] [--textures dir] [--start scene.json] [--mesh rod-mesh.json] [--screenshots dir] [--font FNTOSD] [--program hddosd.elf]\n");
            return 1;
        }
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
        render::Device device(window, {options.validation});
        render::NativeRenderer renderer(device, options.shaders);
        renderer.loadClockTextures(options.textures);

        // The clock's state when the whole3-clock capture starts (frames[0].input of its scene.json), then real time.
        const nlohmann::json input = scenetest::firstInput(options.start.string());
        scene::ClockInputs clockInputs = scenetest::clockInputs(input, scene::loadRodMesh(options.mesh));
        const auto font = std::make_shared<const scene::Font>(scene::Font::load(options.font));
        clockInputs.font = font;
        clockInputs.program = std::make_shared<const scene::ProgramImage>(scene::ProgramImage::load(options.program));
        Clock clock(clockInputs);
        scene::FrameInputs inputs = scenetest::frameInputs(input);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui_ImplSDL3_InitForVulkan(window);
        static const VkFormat swapchainFormat = device.swapchainFormat();
        ImGui_ImplVulkan_InitInfo imgui{};
        imgui.ApiVersion = VK_API_VERSION_1_4;
        imgui.Instance = device.instance();
        imgui.PhysicalDevice = device.physicalDevice();
        imgui.Device = device.device();
        imgui.QueueFamily = device.queueFamily();
        imgui.Queue = device.queue();
        imgui.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE;
        imgui.MinImageCount = 2;
        imgui.ImageCount = std::max(3u, device.swapchainImageCount());
        imgui.UseDynamicRendering = true;
        imgui.PipelineInfoMain.PipelineRenderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        imgui.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        imgui.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchainFormat;
        if (!ImGui_ImplVulkan_Init(&imgui)) throw std::runtime_error("ImGui Vulkan backend");

        app::PanelState panel;
        app::PanelInfo info;
        info.sampleCounts = device.sampleCounts();
        system_clock::duration offset{};
        scene::Frame frame;
        bool fresh = false;
        uint64_t logicFrames = 0;
        const auto produce = [&] {
            const system_clock::time_point now = system_clock::now() + offset;
            inputs.time = clockTime(now);
            inputs.items = clockItems(now);
            frame = clock.frame(inputs);
            inputs.field ^= 1;
            inputs.displayIndex ^= 1;
            ++logicFrames;
            fresh = true;
        };
        produce();

        std::string screenshotName;
        const auto shoot = [&](const std::string& name) {
            const render::NativeOutput& out = renderer.output();
            std::vector<uint8_t> rgba = renderer.readTarget(panel.shown);
            for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
            const std::filesystem::path path = options.screenshots / (name + "-" + std::to_string(out.width) + "x" + std::to_string(out.height) + "-msaa" + std::to_string(out.samples) + ".png");
            app::writePng(path, out.width, out.height, rgba);
            info.lastScreenshot = path.string();
            std::printf("screenshot: %s\n", info.lastScreenshot.c_str());
        };

        std::vector<Step> script;
        const auto resize = [&](int w, int h) { return [=] { SDL_SetWindowSize(window, w, h); }; };
        const auto set = [&](app::Resolution r, uint32_t samples) { return [&panel, r, samples] { panel.resolution = r; panel.samples = samples; }; };
        const auto shot = [&](const char* name) { return [&screenshotName, name] { screenshotName = name; }; };
        if (options.smoke) {
            script = {{1.0, resize(800, 600)}, {2.0, resize(1024, 700)}, {2.5, [&] { SDL_MinimizeWindow(window); }}, {3.5, [&] { SDL_RestoreWindow(window); }},
                      {4.5, resize(1280, 896)}};
            options.soak = 5.5;
        } else if (options.soak > 0) {
            script = {
                {8.0, shot("native")},
                {9.0, resize(800, 600)},
                {10.0, set(app::Resolution::Window, 4)},
                {12.0, shot("window")},
                {13.0, [&] { SDL_MinimizeWindow(window); }},
                {15.0, [&] { SDL_RestoreWindow(window); }},
                {17.0, set(app::Resolution::Double, 8)},
                {20.0, set(app::Resolution::Quadruple, 4)},
                {22.0, shot("x4")},
                {23.0, [&] { panel.paused = true; }},
                {24.0, [&] { panel.step = true; }},
                {24.5, [&] { panel.step = true; }},
                {25.0, [&] { panel.step = true; }},
                {26.0, [&] { panel.paused = false; }},
                {27.0, [&] { panel.shown = scene::TargetName::RefractionSource; }},
                {29.0, [&] { panel.shown = scene::TargetName::Work; }},
                {31.0, [&] { panel.shown = scene::TargetName::Display; }},
                {32.0, resize(1600, 1000)},
                {34.0, set(app::Resolution::Native, 2)},
                {36.0, [&] { panel.time[0] = 23; panel.time[1] = 59; panel.time[2] = 50; panel.setTime = true; }},
                {38.0, set(app::Resolution::Window, 1)},
                {40.0, [&] { panel.tvAspect = false; }},
                {42.0, resize(1280, 896)},
                {44.0, [&] { panel.tvAspect = true; }},
                {46.0, set(app::Resolution::Double, 4)},
                {49.0, [&] { panel.localTime = true; }},
                {51.0, [&] { SDL_MinimizeWindow(window); }},
                {52.0, [&] { SDL_RestoreWindow(window); }},
                {53.0, set(app::Resolution::Window, 8)},
                {55.0, resize(700, 900)},
                {57.0, set(app::Resolution::Native, 1)},
                {58.0, resize(1280, 896)},
            };
        }
        size_t next = 0;

        const double step = 1001.0 / 60000.0;
        double owed = 0;
        const Uint64 begun = SDL_GetTicksNS();
        Uint64 last = begun;
        uint64_t presented = 0;
        float fps = 0;
        bool running = true;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
            }
            const Uint64 now = SDL_GetTicksNS();
            const double elapsed = double(now - begun) * 1e-9, delta = double(now - last) * 1e-9;
            last = now;
            if (delta > 0) fps = fps * 0.95f + float(1.0 / delta) * 0.05f;
            if (options.soak > 0) {
                while (next < script.size() && elapsed >= script[next].at) script[next++].act();
                if (elapsed >= options.soak) running = false;
            }

            if (panel.setTime) {
                const std::chrono::seconds wanted{panel.time[0] * 3600 + panel.time[1] * 60 + panel.time[2]};
                offset = wanted - secondsOfDay(system_clock::now());
                panel.setTime = false;
            }
            if (panel.localTime) {
                offset = {};
                panel.localTime = false;
            }
            if (panel.paused) {
                owed = 0;
                if (panel.step) produce();
            } else {
                owed += std::min(delta, 0.25);
                for (int n = 0; owed >= step; ++n) {
                    owed -= step;
                    if (n < 4) produce();
                }
            }
            panel.step = false;

            const scene::ClockTime shownTime = clockTime(system_clock::now() + offset);
            char text[64];
            std::snprintf(text, sizeof text, "%02d:%02d:%02d", shownTime.hours, shownTime.minutes, shownTime.seconds);
            info.clock = text;
            info.logicFrames = logicFrames;
            info.framesPerSecond = fps;
            info.validationErrors = device.validationErrors();
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            app::drawPanel(panel, info);
            ImGui::Render();

            renderer.setGlyphCache(*font, frame.glyphs);
            auto context = device.beginFrame();
            if (!context) {
                SDL_Delay(10);
                continue;
            }
            const float aspect = panel.tvAspect ? 4.0f / 3.0f : 640.0f / 448.0f;
            const render::NativeOutput output = outputFor(panel, device.swapchainExtent(), aspect);
            if (!(output == renderer.output())) {
                renderer.configure(output);
                fresh = true;
            }
            info.outputWidth = output.width;
            info.outputHeight = output.height;
            if (fresh) renderer.record(context->cmd, frame);
            fresh = false;
            renderer.present(context->cmd, context->image, device.swapchainExtent(), panel.shown, aspect);

            VkRenderingAttachmentInfo colour{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            colour.imageView = context->view;
            colour.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colour.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
            rendering.renderArea = {{0, 0}, device.swapchainExtent()};
            rendering.layerCount = 1;
            rendering.colorAttachmentCount = 1;
            rendering.pColorAttachments = &colour;
            vkCmdBeginRendering(context->cmd, &rendering);
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), context->cmd);
            vkCmdEndRendering(context->cmd);
            device.endFrame(*context);
            ++presented;

            if (panel.screenshot || !screenshotName.empty()) {
                shoot(screenshotName.empty() ? "clock" : screenshotName);
                panel.screenshot = false;
                screenshotName.clear();
            }
        }
        vkDeviceWaitIdle(device.device());
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        if (options.soak > 0)
            std::printf("%s: %.1f s, %llu frames presented, %llu logic frames, %u validation errors\n", options.smoke ? "smoke" : "soak", options.soak,
                        static_cast<unsigned long long>(presented), static_cast<unsigned long long>(logicFrames), device.validationErrors());
        if (device.validationErrors() != 0) code = 1;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fatal: %s\n", error.what());
        code = 1;
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return code;
}
