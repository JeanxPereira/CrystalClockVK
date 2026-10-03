#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "SceneFixture.hpp"
#include "scene/FrameHead.hpp"

namespace {

using nlohmann::json;
using scene::EeArithmetic;
using scene::HeadDraw;
using scene::NativeArithmetic;
using scenetest::sameBits;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 1;                                                            \
        }                                                                        \
    } while (0)

scene::Rect rectOf(const json& j) {
    scene::Rect r;
    for (int i = 0; i < 4; ++i) r.colour[i] = j.at("colour").at(i).get<int32_t>();
    r.x0 = j.at("x0");
    r.y0 = j.at("y0");
    r.u0 = j.at("u0");
    r.v0 = j.at("v0");
    r.x1 = j.at("x1");
    r.y1 = j.at("y1");
    r.u1 = j.at("u1");
    r.v1 = j.at("v1");
    r.z = j.at("z");
    r.blend = j.at("blend");
    r.textured = j.at("textured");
    return r;
}

scene::Ramp rampOf(const json& j) { return {j.at("length"), j.at("counter"), j.at("changed"), j.at("state")}; }

scene::HeadState stateOf(const json& in) {
    scene::HeadState s;
    s.greyRamp = rampOf(in.at("greyRamp"));
    for (int i = 0; i < 3; ++i) s.greys[i] = in.at("greys").at(i);
    s.vignetteRamp = rampOf(in.at("vignetteRamp"));
    const auto& ring = in.at("ringRecord");
    s.ring = {ring.at("alpha"), ring.at("cx"), ring.at("cy"), ring.at("rx"), ring.at("ry"), ring.at("z")};
    s.tint = rectOf(in.at("tint"));
    s.blur = rectOf(in.at("blurRecord"));
    s.copy = rectOf(in.at("copyRecord"));
    s.fade = rectOf(in.at("fadeRecord"));
    s.bars = rectOf(in.at("bars"));
    s.column = rectOf(in.at("column"));
    return s;
}

scene::HeadInputs inputsOf(const json& in) {
    scene::HeadInputs h;
    h.width = in.at("screen").at("width");
    h.height = in.at("screen").at("height");
    h.mode = in.at("mode");
    h.overlayLevel = in.at("overlayLevel");
    h.level = static_cast<uint32_t>(in.at("level").get<int32_t>());
    h.counter = in.at("counter");
    h.item0 = in.at("item0");
    h.proportionX = scenetest::hexFloat(in.at("proportions").at("ax"));
    h.proportionY = scenetest::hexFloat(in.at("proportions").at("ay"));
    for (int i = 0; i < 4; ++i) h.clearColour[i] = in.at("clearColour").at(i);
    const auto& t = in.at("tubeConstants");
    h.tube = {scenetest::hexFloat(t.at("near")),      scenetest::hexFloat(t.at("turn")), scenetest::hexFloat(t.at("scroll")), scenetest::hexFloat(t.at("ripple")),
              scenetest::hexFloat(t.at("scrollEnd")), scenetest::hexFloat(t.at("turnEnd")), scenetest::hexFloat(t.at("far")), scenetest::hexFloat(t.at("radius"))};
    return h;
}

bool rectEqual(const scene::Rect& r, const json& j, const std::string& what) {
    const scene::Rect e = rectOf(j);
    const bool same = r.colour == e.colour && r.x0 == e.x0 && r.y0 == e.y0 && r.u0 == e.u0 && r.v0 == e.v0 && r.x1 == e.x1 && r.y1 == e.y1 && r.u1 == e.u1 &&
                      r.v1 == e.v1 && r.z == e.z && r.blend == e.blend && r.textured == e.textured;
    if (!same) std::fprintf(stderr, "%s: record differs from the fixture\n", what.c_str());
    return same;
}

bool rampEqual(const scene::Ramp& r, const json& j, const std::string& what) {
    const scene::Ramp e = rampOf(j);
    const bool same = r.length == e.length && r.counter == e.counter && r.changed == e.changed && r.state == e.state;
    if (!same) std::fprintf(stderr, "%s: ramp differs: %d %d %d %d\n", what.c_str(), r.length, r.counter, r.changed, r.state);
    return same;
}

bool stateEqual(const scene::HeadState& s, const json& after, const std::string& at) {
    bool ok = rampEqual(s.greyRamp, after.at("greyRamp"), at + " greyRamp") && rampEqual(s.vignetteRamp, after.at("vignetteRamp"), at + " vignetteRamp");
    for (int i = 0; i < 3; ++i) ok = ok && s.greys[i] == after.at("greys").at(i).get<int32_t>();
    const auto& ring = after.at("ringRecord");
    ok = ok && s.ring.alpha == ring.at("alpha") && s.ring.cx == ring.at("cx") && s.ring.cy == ring.at("cy") && s.ring.rx == ring.at("rx") && s.ring.ry == ring.at("ry") &&
         s.ring.z == ring.at("z");
    ok = ok && rectEqual(s.tint, after.at("tint"), at + " tint") && rectEqual(s.blur, after.at("blurRecord"), at + " blurRecord") &&
         rectEqual(s.copy, after.at("copyRecord"), at + " copyRecord") && rectEqual(s.fade, after.at("fadeRecord"), at + " fadeRecord") &&
         rectEqual(s.bars, after.at("bars"), at + " bars") && rectEqual(s.column, after.at("column"), at + " column");
    if (!ok) std::fprintf(stderr, "%s: state after the frame differs\n", at.c_str());
    return ok;
}

// The native draw converted back to GS registers, compared with what the fixture decoded from the packets.
bool drawEqual(const HeadDraw& d, const json& e, int32_t width, int32_t height, const std::string& at) {
    using scene::Coordinates;
    const uint32_t prim = (d.topology == scene::Topology::Sprite ? 6u : 4u) | (d.gouraud ? 8u : 0u) | (d.textured ? 0x10u : 0u) | (d.blended ? 0x40u : 0u) |
                          (d.coordinates == Coordinates::Uv ? 0x100u : 0u);
    if (prim != e.at("prim").get<uint32_t>()) {
        std::fprintf(stderr, "%s: prim %u, expected %u\n", at.c_str(), prim, e.at("prim").get<uint32_t>());
        return false;
    }
    const auto& vs = e.at("vertices");
    if (vs.size() != d.vertices.size()) {
        std::fprintf(stderr, "%s: %zu vertices, expected %zu\n", at.c_str(), d.vertices.size(), vs.size());
        return false;
    }
    const float ox = static_cast<float>(2048 - (width >> 1)), oy = static_cast<float>(2048 - (height >> 1));
    for (size_t i = 0; i < vs.size(); ++i) {
        const auto& v = d.vertices[i];
        const auto& x = vs[i];
        const std::string where = at + " vertex " + std::to_string(i);
        const bool hasSt = x.contains("s"), hasUv = x.contains("u");
        const float px = (v.x + ox) * 16, py = (v.y + oy) * 16;
        const bool ok = static_cast<float>(x.at("x").get<int32_t>()) == px && static_cast<float>(x.at("y").get<int32_t>()) == py && v.z == x.at("z").get<uint32_t>() &&
                        v.fog == x.at("f").get<uint32_t>() && v.r * 128 == x.at("r").get<float>() && v.g * 128 == x.at("g").get<float>() &&
                        v.b * 128 == x.at("b").get<float>() && v.a * 128 == x.at("a").get<float>() && hasSt == (d.coordinates == Coordinates::St) &&
                        hasUv == (d.coordinates == Coordinates::Uv);
        if (!ok) {
            std::fprintf(stderr, "%s: position, depth, colour or coordinate kind differs\n", where.c_str());
            return false;
        }
        if (!sameBits(v.q, x.at("q"), where + " q")) return false;
        if (hasSt && (!sameBits(v.s, x.at("s"), where + " s") || !sameBits(v.t, x.at("t"), where + " t"))) return false;
        if (hasUv && (v.u * 16 != x.at("u").get<float>() || v.v * 16 != x.at("v").get<float>())) {
            std::fprintf(stderr, "%s: texel differs\n", where.c_str());
            return false;
        }
    }
    return true;
}

template <class A>
std::vector<HeadDraw> frameDraws(scene::FrameHead<A>& head, const scene::HeadInputs& in, const scene::Mat4& view, const scene::Mat4& screen) {
    std::vector<HeadDraw> all = head.head(in, view, screen);
    for (auto part : {head.overlay(in), head.tripsAfter(in), head.bars(in), head.column(in)}) all.insert(all.end(), part.begin(), part.end());
    return all;
}

struct Coverage {
    size_t frames = 0, background = 0, blur = 0, blurAfter = 0, vignette = 0, bars = 0;
};

bool drawsEqual(const std::vector<HeadDraw>& draws, const json& head, int32_t width, int32_t height, const std::string& at, Coverage& cover) {
    using scene::Part;
    auto group = [&](Part part) {
        std::vector<const HeadDraw*> out;
        for (const auto& d : draws)
            if (d.part == part) out.push_back(&d);
        return out;
    };
    auto many = [&](Part part, const char* key, size_t& counter) {
        const auto got = group(part);
        const auto& want = head.at(key);
        if (got.size() != want.size()) {
            std::fprintf(stderr, "%s %s: %zu draws, expected %zu\n", at.c_str(), key, got.size(), want.size());
            return false;
        }
        counter += got.size();
        for (size_t i = 0; i < got.size(); ++i)
            if (!drawEqual(*got[i], want[i], width, height, at + " " + key + "[" + std::to_string(i) + "]")) return false;
        return true;
    };
    auto one = [&](Part part, const char* key) {
        const auto got = group(part);
        const auto& want = head.at(key);
        if (want.is_null()) {
            if (!got.empty()) std::fprintf(stderr, "%s %s: drawn, the fixture has none\n", at.c_str(), key);
            return got.empty();
        }
        if (got.size() != 1) {
            std::fprintf(stderr, "%s %s: %zu draws, expected 1\n", at.c_str(), key, got.size());
            return false;
        }
        return drawEqual(*got[0], want, width, height, at + " " + key);
    };
    size_t unused = 0;
    ++cover.frames;
    return many(Part::Background, "background", cover.background) && many(Part::Blur, "blur", cover.blur) && many(Part::Copy, "copies", unused) && one(Part::Tint, "tint") &&
           many(Part::Vignette, "vignette", cover.vignette) && one(Part::Fade, "fade") && many(Part::BlurAfter, "blurAfter", cover.blurAfter) &&
           many(Part::Bars, "bars", cover.bars) && one(Part::Column, "column");
}

int sceneFixture(const std::string& path) {
    const json scene = scenetest::loadScene(path);
    const auto& frames = scene.at("frames");
    CHECK(!frames.empty());
    Coverage isolated, carriedCover;
    std::optional<scene::FrameHead<EeArithmetic>> carried;
    for (const auto& frame : frames) {
        const auto& in = frame.at("input");
        const auto& expect = frame.at("expect");
        const std::string at = "frame " + std::to_string(frame.at("index").get<int>());
        const scene::Mat4 view = scenetest::hexMat4(expect.at("camera").at("view")), screen = scenetest::hexMat4(expect.at("camera").at("screen"));
        const scene::HeadInputs inputs = inputsOf(in);
        const auto& head = expect.at("head");

        scene::FrameHead<EeArithmetic> alone(stateOf(in));
        CHECK(drawsEqual(frameDraws(alone, inputs, view, screen), head, inputs.width, inputs.height, at + " isolated", isolated));
        CHECK(stateEqual(alone.state(), expect.at("after"), at + " isolated"));

        if (!carried) carried.emplace(stateOf(in));
        CHECK(drawsEqual(frameDraws(*carried, inputs, view, screen), head, inputs.width, inputs.height, at + " carried", carriedCover));
        CHECK(stateEqual(carried->state(), expect.at("after"), at + " carried"));

        scene::FrameHead<NativeArithmetic> native(stateOf(in));
        const auto nativeDraws = frameDraws(native, inputs, view, screen);
        scene::FrameHead<EeArithmetic> again(stateOf(in));
        const auto exact = frameDraws(again, inputs, view, screen);
        CHECK(nativeDraws.size() == exact.size());
        for (size_t i = 0; i < exact.size(); ++i) {
            CHECK(nativeDraws[i].part == exact[i].part);
            if (exact[i].part != scene::Part::Background && exact[i].part != scene::Part::Vignette) CHECK(nativeDraws[i].vertices.size() == exact[i].vertices.size());
        }
    }
    std::printf("%s: %zu frames, isolated and carried equal; draws: background %zu, blur %zu, blurAfter %zu, vignette %zu, bars %zu\n", path.c_str(), frames.size(),
                isolated.background, isolated.blur, isolated.blurAfter, isolated.vignette, isolated.bars);
    return 0;
}

}

int main(int argc, char** argv) {
    return scenetest::run(argc, argv, [](int count, char** arguments) {
        if (count < 2) {
            std::fprintf(stderr, "usage: HeadTest <scene.json>...\n");
            return 1;
        }
        for (int i = 1; i < count; ++i)
            if (int failed = sceneFixture(arguments[i])) return failed;
        std::printf("head: all equal\n");
        return 0;
    });
}
