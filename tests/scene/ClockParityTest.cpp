#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "../Check.hpp"
#include "SceneFixture.hpp"
#include "SceneInputs.hpp"
#include "parity/Fixture.hpp"
#include "parity/FromScene.hpp"

using nlohmann::json;

namespace {

using EeClock = scene::Clock<scene::EeArithmetic>;

std::string vertexText(const parity::GsVertex& v) {
    char text[256];
    std::snprintf(text, sizeof text, "(%.9g, %.9g, z %u, rgba %g %g %g %g, stq %.9g %.9g %.9g)", v.x, v.y, v.depth, v.r, v.g, v.b, v.a, v.s, v.t, v.q);
    return text;
}

bool regionMode(parity::GsAddressMode mode) { return mode == parity::GsAddressMode::RegionClamp || mode == parity::GsAddressMode::RegionRepeat; }

bool sameAddress(const parity::GsAddress& a, const parity::GsAddress& b) {
    return a.mode == b.mode && (!regionMode(a.mode) || (a.min == b.min && a.max == b.max));
}

bool sameVertex(const parity::GsVertex& a, const parity::GsVertex& b, bool textured) {
    const bool position = a.x == b.x && a.y == b.y && a.depth == b.depth && a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    return position && (!textured || (a.s == b.s && a.t == b.t && a.q == b.q));
}

// The first difference of state between two passes, or empty. The texture's address limits count only in the
// region modes, where the GS uses them; s, t, q count only when the pass is textured.
std::string stateDifference(const parity::GsPass& ours, const parity::GsPass& theirs) {
    if (ours.target != theirs.target) return "target " + ours.target + " vs " + theirs.target;
    if (ours.primitive != theirs.primitive) return "primitive";
    if (ours.scissor.x0 != theirs.scissor.x0 || ours.scissor.y0 != theirs.scissor.y0 || ours.scissor.x1 != theirs.scissor.x1 || ours.scissor.y1 != theirs.scissor.y1) return "scissor";
    if (ours.blend.has_value() != theirs.blend.has_value()) return std::string("blend ") + (ours.blend ? "on" : "off") + " vs " + (theirs.blend ? "on" : "off");
    if (ours.blend && (ours.blend->a != theirs.blend->a || ours.blend->b != theirs.blend->b || ours.blend->c != theirs.blend->c || ours.blend->d != theirs.blend->d ||
                       ours.blend->fixed != theirs.blend->fixed))
        return "blend terms";
    if (ours.antialias != theirs.antialias) return "antialias";
    if (ours.depth.test != theirs.depth.test || ours.depth.write != theirs.depth.write) return "depth";
    if (ours.texture.has_value() != theirs.texture.has_value()) return "texture presence";
    if (ours.texture) {
        const parity::GsTexture &a = *ours.texture, &b = *theirs.texture;
        if (a.source != b.source || a.sourceIsTarget != b.sourceIsTarget) return "texture source " + a.source + " vs " + b.source;
        if (a.width != b.width || a.height != b.height) return "texture size";
        if (a.coordinates != b.coordinates) return "texture coordinates";
        if (!sameAddress(a.addressU, b.addressU) || !sameAddress(a.addressV, b.addressV)) return "texture address";
        if (a.filter != b.filter) return "texture filter";
        if (a.alpha.constant != b.alpha.constant || a.alpha.value != b.alpha.value || a.alpha.zeroWhenBlack != b.alpha.zeroWhenBlack) return "texture alpha";
    }
    if (ours.skip != theirs.skip) return "skip '" + ours.skip + "' vs '" + theirs.skip + "'";
    return {};
}

std::string passDifference(const parity::GsPass& ours, const parity::GsPass& theirs) {
    if (std::string state = stateDifference(ours, theirs); !state.empty()) return state;
    if (ours.vertices.size() != theirs.vertices.size()) return "vertex count " + std::to_string(ours.vertices.size()) + " vs " + std::to_string(theirs.vertices.size());
    for (size_t v = 0; v < ours.vertices.size(); ++v)
        if (!sameVertex(ours.vertices[v], theirs.vertices[v], ours.texture.has_value()))
            return "vertex " + std::to_string(v) + " " + vertexText(ours.vertices[v]) + " vs " + vertexText(theirs.vertices[v]);
    return {};
}

std::string passLabel(const parity::GsPass& pass) {
    static const char* primitives[] = {"triangles", "sprites", "lines"};
    return pass.name + " " + primitives[int(pass.primitive)] + " " + (pass.texture ? pass.texture->source : std::string("flat"));
}

// The passes of the date, time and button hint the scene does not draw: 26 font draws (triangles of the
// paletted font texture t2f04, PSM 0x14) and the hint's panel sprite (clock texture 9, t2ec0).
enum class TextKind { None, Font, Hint };
TextKind textKind(const parity::GsPass& pass) {
    if (!pass.texture || pass.texture->sourceIsTarget) return TextKind::None;
    const std::string& source = pass.texture->source;
    if (pass.primitive == parity::GsPrimitive::Triangles && source.starts_with("t2f04-") && source.ends_with("-20-9x9") && pass.skip == "texture format 0x14") return TextKind::Font;
    if (pass.primitive == parity::GsPrimitive::Sprites && source == "t2ec0-1-0-6x6" && pass.skip.empty()) return TextKind::Hint;
    return TextKind::None;
}

struct TextCount {
    size_t font = 0, hint = 0;
    bool take(const parity::GsPass& pass, const std::string& at) {
        const TextKind kind = textKind(pass);
        if (kind == TextKind::None) { std::fprintf(stderr, "%s: dump pass %s where the text goes is not text\n", at.c_str(), passLabel(pass).c_str()); return false; }
        (kind == TextKind::Font ? font : hint) += 1;
        return true;
    }
    bool complete(const std::string& at) const {
        if (font == 26 && hint == 1) return true;
        std::fprintf(stderr, "%s: %zu font passes and %zu hint passes where the text goes (26 and 1 expected)\n", at.c_str(), font, hint);
        return false;
    }
};

// The scene's passes against the dump's, in order: every scene pass equal to a dump pass; the dump passes the
// scene does not produce must all stand where the text goes.
int compareFrame(const parity::GsFrame& ours, const json& dump, size_t textAt, std::vector<std::string>& text, const std::string& at) {
    size_t j = 0;
    TextCount count;
    for (const json& p : dump.at("passes")) {
        const parity::GsPass theirs = parity::readPass(p);
        if (j < ours.passes.size()) {
            const std::string difference = passDifference(ours.passes[j], theirs);
            if (difference.empty()) { ++j; continue; }
            if (j != textAt) {
                std::fprintf(stderr, "%s: scene pass %zu (%s) differs from dump %s: %s\n", at.c_str(), j, ours.passes[j].name.c_str(), theirs.name.c_str(), difference.c_str());
                return 1;
            }
        } else if (j != textAt) {
            std::fprintf(stderr, "%s: dump %s after the last scene pass\n", at.c_str(), passLabel(theirs).c_str());
            return 1;
        }
        if (!count.take(theirs, at)) return 1;
        text.push_back(passLabel(theirs));
    }
    if (!count.complete(at)) return 1;
    if (j != ours.passes.size()) {
        std::fprintf(stderr, "%s: scene pass %zu (%s) is not in the dump\n", at.c_str(), j, ours.passes[j].name.c_str());
        return 1;
    }
    return 0;
}

// The state the frame leaves, against the model's: every piece the clock screen steps.
int compareAfter(const EeClock& clock, const json& after, const std::string& at) {
    using scenetest::sameBits;
    const scene::ClockState& s = clock.state();
    const auto ramp = [&](const scene::Ramp& r, const json& j, const char* what) {
        if (r.length == j.at("length") && r.counter == j.at("counter") && r.changed == j.at("changed") && r.state == j.at("state")) return true;
        std::fprintf(stderr, "%s: %s differs\n", at.c_str(), what);
        return false;
    };
    const auto integer = [&](int32_t v, const json& j, const char* what) {
        if (v == j.get<int32_t>()) return true;
        std::fprintf(stderr, "%s: %s %d vs %d\n", at.c_str(), what, v, j.get<int32_t>());
        return false;
    };
    bool ok = integer(s.counter, after.at("counter"), "counter") && integer(s.level, after.at("level"), "level") && integer(s.mode, after.at("mode"), "mode") &&
              integer(s.overlayLevel, after.at("overlayLevel"), "overlayLevel") && ramp(s.vignetteRamp, after.at("vignetteRamp"), "vignetteRamp") &&
              ramp(s.menuRamp, after.at("menuRamp"), "menuRamp") && ramp(s.appearance, after.at("appearance"), "appearance") &&
              ramp(clock.head().greyRamp, after.at("greyRamp"), "greyRamp") && ramp(clock.orbs().spriteFade, after.at("spriteFade"), "spriteFade") &&
              integer(s.eased.secondHand, after.at("eased").at("secondHand"), "eased.secondHand") && integer(s.eased.hourHand, after.at("eased").at("hourHand"), "eased.hourHand") &&
              sameBits(s.eased.progress, after.at("eased").at("progress"), at + " eased.progress") &&
              sameBits(s.eased.fraction, after.at("eased").at("fraction"), at + " eased.fraction") && sameBits(s.cameraOffset, after.at("cameraOffset"), at + " cameraOffset") &&
              sameBits(s.scene.scale, after.at("scene").at("scale"), at + " scene.scale") && integer(s.state.currentRod, after.at("state").at("currentRod"), "currentRod");
    for (int i = 0; ok && i < 3; ++i) ok = integer(clock.head().greys[i], after.at("greys").at(i), "greys");
    for (int k = 0; ok && k < scene::kOrbCount; ++k) {
        const scene::OrbRing& r = clock.orbs().rings[k];
        const json& j = after.at("rings").at(k);
        ok = integer(r.head, j.at("head"), "ring head") && integer(r.count, j.at("count"), "ring count") && integer(r.full, j.at("full"), "ring full");
    }
    ok = ok && sameBits(clock.rodTemplate().local, after.at("template").at("local"), at + " template.local");
    return ok ? 0 : 1;
}

// The pieces the menus, the cubes and the menu ramp step need are not in this capture, so the model leaves them idle.
int menusIdle(const json& frame, const std::string& at) {
    const json& input = frame.at("input");
    for (const char* piece : {"pad", "configPage", "cubeRecord", "screenCode"})
        if (input.contains(piece)) { std::fprintf(stderr, "%s: the capture has %s: the menus are not idle\n", at.c_str(), piece); return 1; }
    const json& after = frame.at("expect").at("after");
    if (after.at("menuRamp") != input.at("menuRamp") || after.at("cubeRamp") != input.at("cubeRamp")) {
        std::fprintf(stderr, "%s: the menu or cube ramp moved\n", at.c_str());
        return 1;
    }
    return 0;
}

bool primitivesMatch(const parity::GsPass& ours, const parity::GsPass& oracle) {
    if (!stateDifference(ours, oracle).empty()) return false;
    const size_t size = oracle.primitive == parity::GsPrimitive::Triangles ? 3 : 2;
    size_t at = 0;
    for (size_t p = 0; p < oracle.vertices.size(); p += size) {
        bool found = false;
        for (; !found && at + size <= ours.vertices.size(); at += size) {
            found = true;
            for (size_t k = 0; k < size; ++k) found = found && sameVertex(ours.vertices[at + k], oracle.vertices[p + k], ours.texture.has_value());
        }
        if (!found) return false;
    }
    return true;
}

// Frame 0 as a fixture for ParityTool: the scene's passes with the oracle's results of the draws they match
// (a pass the oracle culled whole takes the results before it); the text passes, which the scene does not
// produce, are taken from the oracle.
int writeSceneFixture(const parity::GsFrame& ours, size_t textAt, const std::filesystem::path& f0, const std::filesystem::path& out, std::vector<std::string>& text) {
    std::ifstream in(f0 / "frame.json");
    if (!in) { std::fprintf(stderr, "no frame.json in %s\n", f0.string().c_str()); return 1; }
    json fixture = json::parse(in);
    const auto absolute = [&](const json& relative) { return (f0 / relative.get<std::string>()).generic_string(); };
    for (json& t : fixture.at("targets")) t["start"] = absolute(t.at("start"));
    for (json& t : fixture.at("textures")) t["file"] = absolute(t.at("file"));
    fixture["depthStart"] = absolute(fixture.at("depthStart"));
    const json& theirs = fixture.at("passes");

    json passes = json::array();
    std::map<std::string, json> lastColour;
    json lastDepth;
    size_t i = 0, j = 0, culled = 0;
    TextCount count;
    const auto matchesLater = [&](const parity::GsPass& pass) {
        for (size_t k = i; k < theirs.size(); ++k)
            if (primitivesMatch(pass, parity::readPass(theirs[k]))) return true;
        return false;
    };
    while (i < theirs.size()) {
        const json* oracle = i < theirs.size() ? &theirs[i] : nullptr;
        if (j < ours.passes.size() && oracle && primitivesMatch(ours.passes[j], parity::readPass(*oracle))) {
            json p = parity::writePass(ours.passes[j]);
            p["index"] = oracle->at("index");
            p["name"] = oracle->at("name");
            p["oracle"] = {{"colour", absolute(oracle->at("oracle").at("colour"))}, {"depth", absolute(oracle->at("oracle").at("depth"))}};
            lastColour[ours.passes[j].target] = p["oracle"]["colour"];
            lastDepth = p["oracle"]["depth"];
            passes.push_back(std::move(p));
            ++i, ++j;
        } else if (oracle && j == textAt) {
            json p = *oracle;
            if (p.at("skip").is_null()) p["skip"] = "text: not produced by the scene";
            p["oracle"] = {{"colour", absolute(oracle->at("oracle").at("colour"))}, {"depth", absolute(oracle->at("oracle").at("depth"))}};
            lastColour[p.at("target").get<std::string>()] = p["oracle"]["colour"];
            lastDepth = p["oracle"]["depth"];
            if (!count.take(parity::readPass(*oracle), "frame 0 oracle")) return 1;
            text.push_back(passLabel(parity::readPass(*oracle)));
            passes.push_back(std::move(p));
            ++i;
        } else if (j < ours.passes.size() && !matchesLater(ours.passes[j])) {
            if (!lastColour.contains(ours.passes[j].target) || lastDepth.is_null()) { std::fprintf(stderr, "scene pass %zu is culled before any oracle result of its target\n", j); return 1; }
            json p = parity::writePass(ours.passes[j]);
            p["index"] = 1000 + j;
            p["name"] = "scene-" + std::to_string(j);
            p["oracle"] = {{"colour", lastColour.at(ours.passes[j].target)}, {"depth", lastDepth}};
            passes.push_back(std::move(p));
            ++j, ++culled;
        } else {
            std::fprintf(stderr, "frame 0: oracle %s has no scene pass (scene pass %zu, text at %zu)\n", oracle ? oracle->at("name").get<std::string>().c_str() : "(none)", j, textAt);
            return 1;
        }
    }
    if (!count.complete("frame 0 oracle")) return 1;
    // The oracle's frame ends with its last draw: what the scene draws after it has no result to compare.
    std::string beyond;
    for (; j < ours.passes.size(); ++j) beyond += (beyond.empty() ? "" : ", ") + ours.passes[j].name;
    fixture["passes"] = std::move(passes);
    std::filesystem::create_directories(out);
    std::ofstream(out / "frame.json") << fixture.dump();
    std::printf("scene fixture: %zu scene passes (%zu culled whole by the oracle; after the oracle's last draw: %s), %zu text passes from the oracle, in %s\n",
                ours.passes.size(), culled, beyond.empty() ? "none" : beyond.c_str(), text.size(), out.string().c_str());
    return 0;
}

}

int main(int argc, char** argv) {
    return scenetest::run(argc, argv, [](int argc, char** argv) {
        if (argc != 6) {
            std::fprintf(stderr, "usage: ClockParityTest <scene.json> <rod-mesh.json> <passes.json> <parity fixture f0> <scene fixture out>\n");
            return 1;
        }
        const json sceneJson = scenetest::loadScene(argv[1]);
        const scene::RodMesh mesh = scene::loadRodMesh(argv[2]);
        const json dump = scenetest::loadScene(argv[3]);
        const json& frames = sceneJson.at("frames");
        const json& dumpFrames = dump.at("frames");
        CHECK(!frames.empty());

        EeClock clock(scenetest::clockInputs(frames.at(0).at("input"), mesh));
        size_t compared = 0, passes = 0;
        for (const json& frame : frames) {
            const int index = frame.at("index");
            const std::string at = "frame " + std::to_string(index);
            if (menusIdle(frame, at)) return 1;
            const scene::FrameInputs in = scenetest::frameInputs(frame.at("input"));
            const scene::Frame out = clock.frame(in);
            if (compareAfter(clock, frame.at("expect").at("after"), at)) return 1;
            CHECK(out.field == in.field && out.displayIndex == in.displayIndex);
            const parity::GsFrame gs = parity::fromScene(out, parity::clockLayout(out.width, out.height, out.displayIndex));
            CHECK(gs.passes.size() == out.passes.size());
            if (static_cast<size_t>(index) >= dumpFrames.size()) {
                std::printf("%s: %zu passes; the dump has no frame %d (state compared only)\n", at.c_str(), out.passes.size(), index);
                continue;
            }
            const json& dumpFrame = dumpFrames.at(static_cast<size_t>(index));
            CHECK(dumpFrame.at("frame") == index);
            std::vector<std::string> text;
            if (compareFrame(gs, dumpFrame, out.textAt, text, at)) return 1;
            ++compared;
            passes += gs.passes.size();
            std::printf("%s: %zu scene passes equal; %zu text passes not produced (", at.c_str(), gs.passes.size(), text.size());
            for (size_t t = 0; t < text.size(); ++t) std::printf(t ? ", %s" : "%s", text[t].c_str());
            std::printf(")\n");
        }
        std::printf("geometry: %zu frames carried, %zu scene passes equal to the dump\n", compared, passes);

        EeClock first(scenetest::clockInputs(frames.at(0).at("input"), mesh));
        const scene::Frame zero = first.frame(scenetest::frameInputs(frames.at(0).at("input")));
        std::vector<std::string> text;
        if (writeSceneFixture(parity::fromScene(zero, parity::clockLayout(zero.width, zero.height, zero.displayIndex)), zero.textAt, argv[4], argv[5], text)) return 1;

        scene::Clock<scene::NativeArithmetic> native(scenetest::clockInputs(frames.at(0).at("input"), mesh));
        const scene::Frame plain = native.frame(scenetest::frameInputs(frames.at(0).at("input")));
        CHECK(!plain.passes.empty());
        std::printf("native: frame 0 has %zu passes (Ee %zu)\n", plain.passes.size(), zero.passes.size());

        // The ramps this capture holds still, rising: each frame ticks the grey ramp once (background) and the
        // vignette ramp once (overlay), and the vignette is drawn.
        scene::ClockInputs rising = scenetest::clockInputs(frames.at(0).at("input"), mesh);
        rising.head.greyRamp = {40, 10, 0, 1};
        rising.state.vignetteRamp = {80, 10, 0, 1};
        EeClock ramps(rising);
        const scene::Frame drawn = ramps.frame(scenetest::frameInputs(frames.at(0).at("input")));
        CHECK(ramps.head().greyRamp.counter == 11 && ramps.state().vignetteRamp.counter == 11);
        bool vignette = false;
        for (const scene::Pass& pass : drawn.passes) vignette = vignette || pass.name == "vignette";
        CHECK(vignette);
        std::printf("ramps: grey and vignette ramps ticked once each\n");
        return 0;
    });
}
