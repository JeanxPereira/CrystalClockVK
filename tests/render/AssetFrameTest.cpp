#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "../Check.hpp"
#include "app/ClockAssets.hpp"
#include "assets/AssetPack.hpp"
#include "render/Device.hpp"
#include "render/NativeRenderer.hpp"
#include "scene/Clock.hpp"
#include "scene/SceneInputs.hpp"

namespace {

using Clock = scene::Clock<scene::NativeArithmetic>;
constexpr int kFrames = 8;
constexpr scene::TargetName kTargets[3] = {scene::TargetName::Display, scene::TargetName::RefractionSource, scene::TargetName::Work};

// Eight frames of the clock from the start file, every target read back after each.
std::vector<std::vector<uint8_t>> drawFrames(render::NativeRenderer& renderer, const nlohmann::json& start, scene::RodMesh mesh, std::shared_ptr<const scene::Font> font,
                                         std::shared_ptr<const scene::ProgramImage> program) {
    scene::ClockInputs inputs = scene::clockInputs(start, std::move(mesh));
    inputs.font = font;
    inputs.program = std::move(program);
    Clock clock(inputs);
    scene::FrameInputs in = scene::frameInputs(start);
    renderer.configure({640, 448, 4});
    std::vector<std::vector<uint8_t>> out;
    for (int n = 0; n < kFrames; ++n) {
        const scene::Frame frame = clock.frame(in);
        renderer.setGlyphCache(*font, frame.glyphs);
        renderer.draw(frame);
        for (scene::TargetName t : kTargets) out.push_back(renderer.readTarget(t));
        in.field ^= 1;
        in.displayIndex ^= 1;
    }
    return out;
}

}  // namespace

// (d) The frame drawn from the pack (textures, font, program and mesh decoded from the console's files) equals,
// pixel for pixel in every target, the frame drawn from the PNGs, the font and ELF files and rod-mesh.json.
int main(int argc, char** argv) {
    if (argc != 9) {
        std::fprintf(stderr, "usage: AssetFrameTest <shader dir> <start.json> <rod-mesh.json> <png dir> <FNTOSD> <hddosd.elf> <resource folder> <scratch>\n");
        return 1;
    }
    try {
        const std::filesystem::path scratch = argv[8];
        std::filesystem::remove_all(scratch);
        CHECK(assets::loadAssets(argv[7], scratch / "assets.bin").has_value());
        const auto loaded = assets::loadAssets(argv[7], scratch / "assets.bin");
        CHECK(loaded && loaded->fromPack);
        const assets::AssetSet& set = loaded->set;
        const nlohmann::json start = scene::firstInput(argv[2]);

        render::Device device(nullptr, {true});
        std::vector<std::vector<uint8_t>> files, pack;
        {
            render::NativeRenderer renderer(device, argv[1]);
            renderer.loadClockTextures(argv[4]);
            files = drawFrames(renderer, start, scene::loadRodMesh(argv[3]), std::make_shared<const scene::Font>(scene::Font::load(argv[5])),
                           std::make_shared<const scene::ProgramImage>(scene::ProgramImage::load(argv[6])));
        }
        {
            render::NativeRenderer renderer(device, argv[1]);
            app::uploadClockTextures(renderer, set);
            pack = drawFrames(renderer, start, app::rodMeshOf(*set.find("RODMESH")), std::make_shared<const scene::Font>(set.find("FNTOSD")->data),
                          std::make_shared<const scene::ProgramImage>(set.find("PROGRAM")->data));
        }
        CHECK(files.size() == pack.size());
        size_t differing = 0, lit = 0;
        for (size_t i = 0; i < files.size(); ++i) {
            CHECK(files[i].size() == pack[i].size());
            for (size_t b = 0; b < files[i].size(); ++b) differing += files[i][b] != pack[i][b];
        }
        const auto& display = pack[pack.size() - 3];
        for (size_t i = 0; i < display.size(); i += 4) lit += display[i] + display[i + 1] + display[i + 2] > 0;
        std::printf("%d frames, 3 targets each: %zu differing bytes between the pack and the files; %zu lit display pixels\n", kFrames, differing, lit);
        CHECK(differing == 0 && lit > 640 * 448 / 4);
        CHECK(device.validationErrors() == 0);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "exception: %s\n", e.what());
        return 1;
    }
    return 0;
}
