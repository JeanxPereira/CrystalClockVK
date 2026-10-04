#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "scene/SceneInputs.hpp"
#include "app/ClockAssets.hpp"
#include "app/DebugPanel.hpp"
#include "app/BootChain.hpp"
#include "app/ClockScreen.hpp"
#include "app/NativeFrames.hpp"
#include "app/BootChain.hpp"
#include "app/Input.hpp"
#include "app/OpeningScreen.hpp"
#include "app/Png.hpp"
#include "assets/AssetPack.hpp"
#include "assets/ClockTextures.hpp"
#include "assets/Program.hpp"
#include "app/Screens.hpp"
#include "render/Device.hpp"
#include "render/NativeRenderer.hpp"
#include "scene/Clock.hpp"

namespace {

using Clock = scene::Clock<scene::NativeArithmetic>;

class FunctionScreen : public app::FrameSource {
public:
    explicit FunctionScreen(std::function<scene::Frame()> step) : m_step(std::move(step)) {}
    void step() override { m_frame = m_step(); }
    scene::Frame frame() override { return m_frame; }
    bool done() const override { return false; }

private:
    std::function<scene::Frame()> m_step;
    scene::Frame m_frame;
};
using std::chrono::system_clock;

struct Options {
    bool validation = true;
    bool smoke = false;
    double soak = 0;
    bool boot = false;
    bool towersDemo = false;
    uint32_t lightsPhase = 0xD80;
    std::filesystem::path openingTextures;
    std::filesystem::path bootStart = CLOCK_START_BOOT;
    std::string capture;
    std::filesystem::path shaders = CLOCK_SHADERS;
    std::filesystem::path textures;
    std::filesystem::path start = CLOCK_MENUS_START;
    bool startGiven = false;
    bool clockStart = false;
    std::filesystem::path cubeMesh;
    std::filesystem::path mesh;
    std::filesystem::path screenshots = CLOCK_SCREENSHOTS;
    std::filesystem::path font;
    std::filesystem::path program;
    std::filesystem::path resources;
    std::filesystem::path bios;
    std::filesystem::path pack;
};

// The folder of raw OSD resource files: --resources, else the user's own (where --bios extracts to) when it holds a
// TEXIMAGE or a BIOS is given.
std::filesystem::path resourceFolder(const Options& options) {
    if (!options.resources.empty()) return options.resources;
    return assets::userDataDirectory() / "resources";
}

std::vector<uint8_t> fileOrEmpty(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) ? assets::readFile(path) : std::vector<uint8_t>{};
}

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
    std::set<std::string> explicitFlags;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool more = i + 1 < argc;
        explicitFlags.insert(arg);
        if (arg == "--smoke") options.smoke = true;
        else if (arg == "--boot") options.boot = true;
        else if (arg == "--towers" && more) options.towersDemo = std::string(argv[++i]) == "demo";
        else if (arg == "--lights-phase" && more) options.lightsPhase = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 0));
        else if (arg == "--opening-textures" && more) options.openingTextures = argv[++i];
        else if (arg == "--capture" && more) options.capture = argv[++i];
        else if (arg == "--no-validation") options.validation = false;
        else if (arg == "--soak" && more) options.soak = std::atof(argv[++i]);
        else if (arg == "--shaders" && more) options.shaders = argv[++i];
        else if (arg == "--textures" && more) options.textures = argv[++i];
        else if (arg == "--start" && more) { options.start = argv[++i]; options.startGiven = true; }
        else if (arg == "--clock") options.clockStart = true;
        else if (arg == "--cube-mesh" && more) options.cubeMesh = argv[++i];
        else if (arg == "--mesh" && more) options.mesh = argv[++i];
        else if (arg == "--screenshots" && more) options.screenshots = argv[++i];
        else if (arg == "--font" && more) options.font = argv[++i];
        else if (arg == "--program" && more) options.program = argv[++i];
        else if (arg == "--resources" && more) options.resources = argv[++i];
        else if (arg == "--bios" && more) options.bios = argv[++i];
        else if (arg == "--assets" && more) options.pack = argv[++i];
        else {
            std::fprintf(stderr, "usage: CrystalClock [--resources dir] [--bios rom.bin] [--assets assets.bin] [--smoke] [--soak seconds] [--clock] [--boot [--towers none|demo] [--lights-phase N] [--opening-textures dir] [--capture all|n,n,...]] [--no-validation] [--shaders dir]\n"
                                 "                    [--start scene.json] [--screenshots dir] [--textures dir] [--mesh rod-mesh.json] [--cube-mesh cube-mesh.json] [--font FNTOSD] [--program hddosd.elf]\n");
            return 1;
        }
    }
    if (!options.startGiven) options.start = options.clockStart ? CLOCK_CLOCK_START : CLOCK_MENUS_START;
    if (options.pack.empty()) options.pack = assets::userDataDirectory() / "assets.bin";
    // The clock's assets: the console's raw resource files decoded (or their cache); loose files only from explicit flags.
    std::optional<assets::LoadedAssets> loaded;
    const std::filesystem::path folder = resourceFolder(options);
    if (!options.bios.empty()) {
        try {
            std::string names;
            for (const std::string& name : assets::extractBios(options.bios, folder)) names += " " + name;
            std::printf("bios: %s gives%s, in %s\n", options.bios.string().c_str(), names.c_str(), folder.string().c_str());
        } catch (const std::exception& error) {
            std::fprintf(stderr, "bios: %s\n", error.what());
        }
    }
    try {
        loaded = assets::loadAssets(folder, options.pack);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "assets: %s\n", error.what());
    }
    if (loaded) {
        std::printf("assets: %zu in %.1f ms, %s %s\n", loaded->set.assets.size(), loaded->milliseconds, loaded->fromPack ? "from the cache" : "decoded and cached in",
                    options.pack.string().c_str());
    } else {
        std::printf("assets: no resource files in %s and no cache\n", folder.string().c_str());
    }
    const assets::AssetSet* decoded = loaded ? &loaded->set : nullptr;
    const auto sourceOf = [&](const assets::Asset& a) { return "decoded from " + decoded->sourceOf(a).path; };

    // Each group from one place: the decoded set, or a loose file named on the command line.
    const bool useTextureFiles = explicitFlags.contains("--textures");
    const std::string textureSource = useTextureFiles ? "PNG files in " + options.textures.string() : decoded && decoded->find("TEXCFLOW") ? sourceOf(*decoded->find("TEXCFLOW")) : std::string("none");
    const assets::Asset* meshAsset = decoded && !explicitFlags.contains("--mesh") ? decoded->find("RODMESH") : nullptr;
    const assets::Asset* cubeAsset = decoded && !explicitFlags.contains("--cube-mesh") ? decoded->find("CUBEMESH") : nullptr;
    const std::string meshSource = meshAsset ? sourceOf(*meshAsset) : options.mesh.string();
    const assets::Asset* fontAsset = decoded ? decoded->find("FNTOSD") : nullptr;
    const assets::Asset* programAsset = decoded ? decoded->find("PROGRAM") : nullptr;
    const bool fontLoose = explicitFlags.contains("--font");
    const bool programLoose = explicitFlags.contains("--program");
    std::vector<uint8_t> fontFile = fontLoose ? fileOrEmpty(options.font) : fontAsset ? fontAsset->data : std::vector<uint8_t>{};
    std::vector<uint8_t> programFile = programLoose ? fileOrEmpty(options.program) : programAsset ? programAsset->data : std::vector<uint8_t>{};
    if (programLoose && !programFile.empty()) {
        bool known = false;
        try {
            known = assets::isHddOsd110U(assets::ElfImage(programFile));
        } catch (const std::exception&) {
        }
        if (!known) {
            std::fprintf(stderr, "text: %s is not HDD OSD 1.10U's hddosd.elf: not read\n", options.program.string().c_str());
            programFile.clear();
        }
    }
    const std::string textSource = fontFile.empty() || programFile.empty()
                                       ? std::string("none (no FNTOSD or no HDD OSD 1.10U hddosd.elf)")
                                       : (fontLoose ? options.font.string() : sourceOf(*fontAsset)) + " and " + (programLoose ? options.program.string() : sourceOf(*programAsset));
    std::printf("textures: %s\nmesh: %s\ntext: %s\n", textureSource.c_str(), meshSource.c_str(), textSource.c_str());
    std::vector<std::string> missing;
    if (!useTextureFiles && !(decoded && decoded->find("TEXCFLOW"))) missing.push_back("the clock textures");
    if (!meshAsset && options.mesh.empty()) missing.push_back("the rod mesh");
    if (!cubeAsset && options.cubeMesh.empty()) missing.push_back("the cube mesh");
    if (options.boot && options.openingTextures.empty() && !(decoded && decoded->find(assets::kOpeningTextures[0].name))) missing.push_back("the opening textures");
    if (!missing.empty()) {
        std::string list;
        for (const std::string& what : missing) list += (list.empty() ? "" : ", ") + what;
        std::fprintf(stderr, "missing: %s.\nGive the console's files: --bios rom.bin (a PS2 BIOS image) or --resources dir (a folder with TEXIMAGE, FNTOSD and hddosd.elf).\n"
                             "Decoded data is cached in %s.\n", list.c_str(), options.pack.string().c_str());
        return 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
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
        if (useTextureFiles) renderer.loadClockTextures(options.textures);
        else app::uploadClockTextures(renderer, *decoded);
        if (options.boot) {
            const bool openingFiles = !options.openingTextures.empty();
            const std::filesystem::path openingDirectory = options.openingTextures;
            if (openingFiles) renderer.loadOpeningTextures(openingDirectory);
            else app::uploadOpeningTextures(renderer, *decoded);
            std::printf("opening textures: %s\n", openingFiles ? ("PNG files in " + openingDirectory.string()).c_str() : sourceOf(*decoded->find(assets::kOpeningTextures[0].name)).c_str());
        }

        // The clock as the whole3-clock capture holds it at its first frame (resources/clock/start.json, or a
        // capture's scene.json through --start), then real time.
        const scene::RodMesh cubeMesh = cubeAsset ? app::rodMeshOf(*cubeAsset) : scene::loadRodMesh(options.cubeMesh);
        nlohmann::json input = scene::firstInput(options.start.string());
        if (options.boot) {
            input = scene::firstInput(options.bootStart.string());
            for (const auto& [key, value] : scene::firstInput(CLOCK_MENUS_START).items())
                if (!input.contains(key)) input[key] = value;
        }
        scene::ClockInputs clockInputs = scene::hasMenus(input) ? scene::clockInputs(input, (meshAsset ? app::rodMeshOf(*meshAsset) : scene::loadRodMesh(options.mesh)), &cubeMesh)
                                                                                      : scene::clockInputs(input, (meshAsset ? app::rodMeshOf(*meshAsset) : scene::loadRodMesh(options.mesh)));
        if (clockInputs.menus) clockInputs.menus->options.browserEnters = false;
        // The text needs the font library's context of the start state; a start without it runs without text.
        std::shared_ptr<const scene::Font> font;
        std::shared_ptr<const scene::ProgramImage> program;
        if (!input.contains("font")) {
            std::printf("%s has no font context: the clock runs without text\n", options.start.string().c_str());
        } else if (!fontFile.empty() && !programFile.empty()) {
            font = std::make_shared<const scene::Font>(std::move(fontFile));
            clockInputs.font = font;
            program = std::make_shared<const scene::ProgramImage>(std::move(programFile));
            clockInputs.program = program;
        }
        const scene::ClockInputs clockInputs0 = clockInputs;
        auto clockPtr = std::make_unique<Clock>(clockInputs0);
        scene::FrameInputs inputs = scene::frameInputs(input);
        app::firstFrame(inputs);
        inputs.threadStep = false;

        const bool captureAll = options.capture == "all";
        std::set<uint64_t> captureAt;
        for (size_t at = 0; at < options.capture.size() && !captureAll;) {
            captureAt.insert(std::strtoull(options.capture.c_str() + at, nullptr, 10));
            const size_t comma = options.capture.find(',', at);
            if (comma == std::string::npos) break;
            at = comma + 1;
        }
        const bool capturing = options.boot && !options.capture.empty();

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
        app::Bindings bindings = app::Bindings::defaults();
        app::PadReader reader;
        std::map<SDL_JoystickID, SDL_Gamepad*> gamepads;
        scene::ConfigItems items = clockInputs.menus ? clockInputs.menus->items : scene::ConfigItems{};
        int32_t previousLevel = 0;
        std::vector<std::string> visited;
        bool soakFailed = false;
        scene::Frame frame;
        bool fresh = false;
        uint64_t logicFrames = 0;
        const auto stepClock = [&] {
            const system_clock::time_point now = system_clock::now() + offset;
            scene::Frame produced;
            if (const scene::MenusState* menus = clockPtr->menus()) {
                const int32_t level = menus->page.level;
                if (previousLevel == 1 && level == 0 && menus->page.selected >= 0 && menus->page.selected < 9 &&
                    menus->entries[menus->page.selected].confirm == 0x00227b90u) {
                    const std::chrono::local_time<std::chrono::seconds> wanted{std::chrono::local_days{std::chrono::year(items[6]) / items[7] / items[8]} +
                                                                                std::chrono::hours(items[9]) + std::chrono::minutes(items[10]) + std::chrono::seconds(items[11])};
                    offset = std::chrono::current_zone()->to_sys(wanted) - system_clock::now();
                }
                previousLevel = level;
                if (level != 1) {
                    const scene::ClockItems local = clockItems(system_clock::now() + offset);
                    items[6] = local.year, items[7] = local.month, items[8] = local.day, items[9] = local.hour, items[10] = local.minute, items[11] = local.second;
                }
                const auto utc9 = std::chrono::floor<std::chrono::seconds>(now) + std::chrono::minutes(540);
                const auto day = std::chrono::floor<std::chrono::days>(utc9);
                const std::chrono::year_month_day date{day};
                const std::chrono::hh_mm_ss hms{utc9 - day};
                inputs.menu.pad = reader.frame();
                inputs.menu.rtcMirror = std::array<int32_t, 6>{int32_t(date.year()), int32_t(unsigned(date.month())), int32_t(unsigned(date.day())),
                                                               int32_t(hms.hours().count()), int32_t(hms.minutes().count()), int32_t(hms.seconds().count())};
                inputs.configItems = items;
            }
            produced = clockPtr->frame(inputs);
            if (const scene::MenusState* menus = clockPtr->menus()) {
                items = clockPtr->items();
                const app::Screen screen = app::screenOf(*menus, clockPtr->state().menuRamp);
                if (visited.empty() || visited.back() != app::screenName(screen)) visited.push_back(app::screenName(screen));
                if (clockPtr->state().mode == 3 || clockPtr->state().scene.leaving != 0 || menus->screenCode == 9999) soakFailed = true;
            }
            if (options.boot) inputs.timeFilled = 1;
            inputs.threadStep = true;
            app::nextFrame(inputs);
            return produced;
        };
        std::unique_ptr<app::BootChain> chain;
        app::OpeningScreen* bootOpening = nullptr;
        const scene::FrameInputs inputs0 = inputs;
        const auto buildBoot = [&] {
            if (!program) throw std::runtime_error("--boot needs hddosd.elf (HDD OSD 1.10U) and FNTOSD");
            clockPtr = std::make_unique<Clock>(clockInputs0);
            inputs = inputs0;
            items = clockInputs0.menus ? clockInputs0.menus->items : scene::ConfigItems{};
            previousLevel = 0;
            scene::opening::BootOptions boot;
            boot.lightsPhase = options.lightsPhase;
            if (options.towersDemo) {
                scene::opening::History history{};
                const char* names[] = {"DEMO A", "DEMO B", "DEMO C", "DEMO D", "DEMO E"};
                for (size_t i = 0; i < 5; ++i) {
                    for (size_t c = 0; names[i][c]; ++c) history[i].name[c] = names[i][c];
                    history[i].count = static_cast<uint8_t>(3 + 4 * i);
                    history[i].mask = static_cast<uint8_t>(0x0F << i);
                    history[i].mainCell = static_cast<uint8_t>(i);
                }
                boot.history = history;
            }
            auto opening = std::make_unique<app::OpeningScreen>(boot, program);
            bootOpening = opening.get();
            chain = std::make_unique<app::BootChain>(std::move(opening), std::make_unique<FunctionScreen>([&] { return stepClock(); }), scene::opening::kFramesToClock);
        };
        if (options.boot) buildBoot();
        const auto produce = [&] {
            const system_clock::time_point now = system_clock::now() + offset;
            inputs.time = clockTime(now);
            inputs.items = clockItems(now);
            if (chain) {
                chain->step();
                frame = chain->frame();
            } else {
                frame = stepClock();
            }
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
        } else if (options.soak > 0 && options.boot) {
            const auto press = [&](double at, app::PadButton button) {
                const uint32_t bit = app::bitOf(button);
                script.push_back({at, [&reader, bit] { reader.down(0, bit); }});
                script.push_back({at + 0.1, [&reader, bit] { reader.up(0, bit); }});
            };
            script.push_back({2.0, shot("boot-opening")});
            script.push_back({9.0, shot("boot-clock")});
            press(10.0, app::PadButton::Square);
            script.push_back({12.5, shot("boot-after-square")});
            press(13.0, app::PadButton::Down);
            press(14.0, app::PadButton::Cross);
            script.push_back({17.0, shot("boot-menu-next")});
        } else if (options.soak > 0 && clockInputs.menus && !options.clockStart) {
            const auto press = [&](double at, app::PadButton button) {
                const uint32_t bit = app::bitOf(button);
                script.push_back({at, [&reader, bit] { reader.down(0, bit); }});
                script.push_back({at + 0.1, [&reader, bit] { reader.up(0, bit); }});
            };
            using app::PadButton;
            script.push_back({1.5, shot("main-menu")});
            press(2.0, PadButton::Down);
            press(3.5, PadButton::Up);
            press(5.0, PadButton::Up);
            press(6.5, PadButton::Triangle);
            press(8.0, PadButton::Cross);
            press(10.0, PadButton::Down);
            press(11.5, PadButton::Cross);
            script.push_back({17.0, shot("configuration")});
            for (double at : {18.0, 19.0, 20.0}) press(at, PadButton::Down);
            for (double at : {21.0, 22.0, 23.0, 24.0, 25.0}) press(at, PadButton::Up);
            for (double at : {26.0, 27.0, 28.0}) press(at, PadButton::Down);
            press(30.0, PadButton::Cross);
            script.push_back({32.0, shot("entry")});
            press(33.0, PadButton::Right);
            press(34.5, PadButton::Circle);
            press(36.0, PadButton::Cross);
            press(38.0, PadButton::Cross);
            press(40.0, PadButton::Square);
            script.push_back({44.0, shot("clock-alone")});
            script.push_back({45.0, resize(800, 600)});
            script.push_back({46.0, set(app::Resolution::Window, 4)});
            press(48.0, PadButton::Square);
            script.push_back({50.0, [&] { SDL_MinimizeWindow(window); }});
            script.push_back({51.0, [&] { SDL_RestoreWindow(window); }});
            script.push_back({52.0, set(app::Resolution::Quadruple, 4)});
            press(54.0, PadButton::Circle);
            script.push_back({55.0, set(app::Resolution::Native, 1)});
            script.push_back({56.0, resize(1280, 896)});
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
        std::stable_sort(script.begin(), script.end(), [](const Step& l, const Step& r) { return l.at < r.at; });
        size_t next = 0;

        const double step = 1001.0 / 60000.0;
        // One present per logic frame, the loop sleeping until the next step is due: FIFO then shows every logic
        // frame for the same number of refreshes. Presenting as fast as the loop wakes (2 frames in flight, so in
        // bursts) showed the 59.94 Hz frames for uneven runs of refreshes, a judder.
        const auto refreshRate = [&] {
            const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
            return mode ? double(mode->refresh_rate) : 0.0;
        };
        double owed = 0;
        const Uint64 begun = SDL_GetTicksNS();
        Uint64 last = begun, lastPresent = begun;
        uint64_t presented = 0;
        // Intervals between presents of consecutive logic frames, for the cadence report.
        uint64_t intervals = 0, uneven = 0;
        double intervalSum = 0, intervalWorst = 0;
        float fps = 0;
        bool running = true;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                    if (SDL_Gamepad* pad = SDL_OpenGamepad(event.gdevice.which)) gamepads[event.gdevice.which] = pad;
                }
                if (!ImGui::GetIO().WantCaptureKeyboard || event.type != SDL_EVENT_KEY_DOWN) app::feed(reader, bindings, event);
                if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                    const auto found = gamepads.find(event.gdevice.which);
                    if (found != gamepads.end()) {
                        SDL_CloseGamepad(found->second);
                        gamepads.erase(found);
                    }
                }
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
            } else if (capturing) {
                if (!fresh) produce();
            } else {
                owed += std::min(delta, 0.25);
                for (int n = 0; owed >= step; ++n) {
                    owed -= step;
                    if (n < 4) produce();
                }
            }
            panel.step = false;
            const double sincePresent = double(now - lastPresent) * 1e-9;
            if (!capturing && (panel.paused ? sincePresent < step : !fresh)) {
                const double wait = panel.paused ? step - sincePresent : step - owed;
                SDL_DelayPrecise(static_cast<Uint64>(std::max(0.0, wait) * 1e9));
                continue;
            }

            const scene::ClockTime shownTime = clockTime(system_clock::now() + offset);
            char text[64];
            std::snprintf(text, sizeof text, "%02d:%02d:%02d", shownTime.hours, shownTime.minutes, shownTime.seconds);
            info.clock = text;
            if (chain && chain->phase() != app::BootPhase::Clock) {
                info.screen = chain->name();
                info.counter = bootOpening->counter();
                info.stage = bootOpening->stage();
                info.cameraZ = bootOpening->cameraZ();
                if (const scene::opening::HandOff* h = bootOpening->handOff()) info.handOff = "module " + std::to_string(h->module) + ", execute type " + std::to_string(h->executeAppType);
                info.sounds = bootOpening->sounds().size();
            }
            info.logicFrames = logicFrames;
            if (!chain || chain->phase() == app::BootPhase::Clock) info.screen = visited.empty() ? "" : visited.back();
            info.pad = inputs.menu.pad;
            info.framesPerSecond = fps;
            info.validationErrors = device.validationErrors();
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            app::drawPanel(panel, info);
            ImGui::Render();

            if (font && frame.textureSet == scene::TextureSet::Clock) renderer.setGlyphCache(*font, frame.glyphs);
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
            const bool newFrame = fresh;
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
            const Uint64 shown = now;
            if (newFrame && !panel.paused && presented > 1) {
                const double interval = double(shown - lastPresent) * 1e-9;
                ++intervals;
                intervalSum += interval;
                intervalWorst = std::max(intervalWorst, std::fabs(interval - step));
                uneven += std::fabs(interval - step) > 0.002;
            }
            lastPresent = shown;

            if (capturing) {
                if (captureAll || captureAt.count(logicFrames)) {
                    char name[32];
                    std::snprintf(name, sizeof name, "boot-%03llu", static_cast<unsigned long long>(logicFrames));
                    shoot(name);
                }
                if (logicFrames >= 246 + (captureAll ? 40 : 0)) running = false;
            }
            if (panel.restartOpening && chain) {
                panel.restartOpening = false;
                visited.clear();
                buildBoot();
                logicFrames = 0;
                produce();
            }
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
        if (options.soak > 0)
            std::printf("display %.2f Hz; %llu present intervals, mean %.3f ms (step %.3f ms), %llu off by more than 2 ms, worst off by %.3f ms\n", refreshRate(),
                        static_cast<unsigned long long>(intervals), intervals ? intervalSum / double(intervals) * 1e3 : 0.0, step * 1e3,
                        static_cast<unsigned long long>(uneven), intervalWorst * 1e3);
        if (options.soak > 0 && clockPtr->menus()) {
            std::string list;
            for (const std::string& name : visited) list += (list.empty() ? "" : ", ") + name;
            std::printf("screens visited: %s\n", list.c_str());
            if (soakFailed) {
                std::fprintf(stderr, "soak: mode 3, leaving or screen code 9999 seen\n");
                code = 1;
            }
        }
        if (device.validationErrors() != 0) code = 1;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fatal: %s\n", error.what());
        code = 1;
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return code;
}
