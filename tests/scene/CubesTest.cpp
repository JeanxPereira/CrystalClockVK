#include <cstdio>
#include <string>

#include "SceneFixture.hpp"
#include "StageCompare.hpp"
#include "scene/Cubes.hpp"
#include "scene/SceneInputs.hpp"

namespace {

using nlohmann::json;

bool sameVertex(const scene::RodVertex& v, const json& gs, int32_t width, int32_t height) {
    const int32_t ox = (0x800 - (width >> 1)) << 4, oy = (0x800 - (height >> 1)) << 4;
    return static_cast<int32_t>(v.x * 16.0f) + ox == gs.at("x").get<int32_t>() && static_cast<int32_t>(v.y * 16.0f) + oy == gs.at("y").get<int32_t>() &&
           (static_cast<uint32_t>(static_cast<int64_t>(v.z * 16.0f)) & 0xffffff) == gs.at("z").get<uint32_t>();
}

int draws(const std::vector<scene::CubeDraw>& ours, const json& theirs, int32_t width, int32_t height, const std::string& at) {
    size_t k = 0;
    for (const scene::CubeDraw& d : ours) {
        if (d.send == scene::CubeSend::LayerClear) { ++k; continue; }
        if (k >= theirs.size()) { std::fprintf(stderr, "%s: more draws than the model's %zu\n", at.c_str(), theirs.size()); return 1; }
        const json& t = theirs.at(k++);
        if (d.label != t.at("label").get<std::string>()) { std::fprintf(stderr, "%s draw %zu: %s vs %s\n", at.c_str(), k - 1, d.label.c_str(), t.at("label").get<std::string>().c_str()); return 1; }
        const json& vs = t.at("vertices");
        size_t n = 0;
        if (d.rectangle) n = d.rectangle->vertices.size();
        for (const scene::RodFaceDraw& f : d.faces)
            for (const scene::RodVertex& v : f.strip) {
                if (n >= vs.size() || !sameVertex(v, vs.at(n), width, height)) { std::fprintf(stderr, "%s draw %zu (%s) vertex %zu differs\n", at.c_str(), k - 1, d.label.c_str(), n); return 1; }
                ++n;
            }
        if (n != vs.size()) { std::fprintf(stderr, "%s draw %zu (%s): %zu vertices, the model %zu\n", at.c_str(), k - 1, d.label.c_str(), n, vs.size()); return 1; }
    }
    if (k != theirs.size()) { std::fprintf(stderr, "%s: %zu draws, the model %zu\n", at.c_str(), k, theirs.size()); return 1; }
    return 0;
}

template <class A>
int sceneFile(const std::string& path, const scene::RodMesh& mesh, bool compare) {
    const json scene = scenetest::loadScene(path);
    size_t drawn = 0;
    for (const json& frame : scene.at("frames")) {
        const std::string at = path + " frame " + std::to_string(frame.at("index").get<int>());
        const json& stage = frame.at("expect").at("stages").at("cubes");
        if (stage.is_null()) continue;
        StagePieces p = stagePieces(stage.at("before"));
        scene::Cubes<A> cubes(mesh);
        const json& input = frame.at("input");
        const scene::CubeFrameInputs in{p.width, p.height, input.at("scene").at("field").get<int32_t>(), input.at("body").get<int32_t>()};
        const auto out = cubes.frame(p.cubes, p.head, p.clock, in);
        if (!compare) continue;
        const auto differences = stageDifferences(p, stage.at("after"));
        if (!differences.empty()) { std::fprintf(stderr, "%s: %s\n", at.c_str(), differences.front().c_str()); return 1; }
        if (draws(out, frame.at("expect").at("cubes"), p.width, p.height, at)) return 1;
        drawn += out.size();
    }
    if (compare) std::printf("%s: every frame equal, %zu draws\n", path.c_str(), drawn);
    return 0;
}

}

int main(int argc, char** argv) {
    return scenetest::run(argc, argv, [](int count, char** arguments) {
        const scene::RodMesh mesh = scene::loadRodMesh(arguments[1]);
        for (int i = 2; i < count; ++i) {
            if (int failed = sceneFile<scene::EeArithmetic>(arguments[i], mesh, true)) return failed;
            if (int failed = sceneFile<scene::NativeArithmetic>(arguments[i], mesh, false)) return failed;
        }
        return 0;
    });
}
