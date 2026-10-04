#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "../Check.hpp"
#include "SceneFixture.hpp"
#include "scene/SceneInputs.hpp"
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

// The text's passes: glyphs (triangles of the glyph cache, the paletted texture t2f04, PSM 0x14) and the hint's picture
// (the sprite of clock texture 9, t2ec0).
struct TextCount {
    size_t font = 0, hint = 0;
    void take(const parity::GsPass& pass) {
        if (!pass.texture || pass.texture->sourceIsTarget) return;
        if (pass.primitive == parity::GsPrimitive::Triangles && pass.texture->source.ends_with("-20-9x9")) font += 1;
        if (pass.primitive == parity::GsPrimitive::Sprites && pass.texture->source == "t2ec0-1-0-6x6") hint += 1;
    }
};

// frame_passes.mjs marks the paletted texture as a format the parity rule's fixtures do not carry; the scene's pass
// draws it from the glyph cache image instead.
parity::GsPass dumpPass(const json& p) {
    parity::GsPass pass = parity::readPass(p);
    if (pass.skip == "texture format 0x14") pass.skip.clear();
    return pass;
}

// The scene's passes against the dump's, in order: every dump pass is a scene pass, state and every vertex equal.
int compareFrame(const parity::GsFrame& ours, const json& dump, TextCount& count, const std::string& at) {
    size_t j = 0;
    for (const json& p : dump.at("passes")) {
        const parity::GsPass theirs = dumpPass(p);
        if (j >= ours.passes.size()) {
            std::fprintf(stderr, "%s: dump %s after the last scene pass\n", at.c_str(), passLabel(theirs).c_str());
            return 1;
        }
        const std::string difference = passDifference(ours.passes[j], theirs);
        if (!difference.empty()) {
            std::fprintf(stderr, "%s: scene pass %zu (%s) differs from dump %s: %s\n", at.c_str(), j, ours.passes[j].name.c_str(), passLabel(theirs).c_str(), difference.c_str());
            return 1;
        }
        count.take(ours.passes[j]);
        ++j;
    }
    if (j != ours.passes.size()) {
        std::fprintf(stderr, "%s: scene pass %zu (%s) is not in the dump\n", at.c_str(), j, ours.passes[j].name.c_str());
        return 1;
    }
    return 0;
}

// The strings the clock handed the font code, with the program's font state at each call, against the capture's
// (expect.text: the date and time, then the button hint).
bool sameState(const scene::FontState& ours, const json& theirs, const std::string& at) {
    using scenetest::sameBits;
    const auto integer = [&](int32_t v, const char* key) {
        if (v == theirs.at(key).get<int32_t>()) return true;
        std::fprintf(stderr, "%s: %s %d vs %d\n", at.c_str(), key, v, theirs.at(key).get<int32_t>());
        return false;
    };
    bool ok = integer(ours.lineHeight, "lineHeight") && integer(ours.fixed, "fixed") && integer(ours.percent, "percent") && integer(ours.pitch, "pitch") &&
              integer(ours.decoration, "decoration") && integer(ours.clip, "clip") && integer(ours.blank, "blank") && integer(ours.ascent, "ascent") &&
              integer(ours.dirty, "dirty") && sameBits(ours.tv, theirs.at("tv"), at + " tv") && sameBits(ours.ratio, theirs.at("ratio"), at + " ratio");
    for (int i = 0; ok && i < 2; ++i) ok = sameBits(ours.locate[i], theirs.at("locate").at(i), at + " locate");
    for (int i = 0; ok && i < 4; ++i) ok = sameBits(ours.colour[i], theirs.at("colour").at(i), at + " colour");
    return ok && sameBits(ours.matrix, theirs.at("matrix"), at + " matrix");
}

int compareStrings(const std::vector<scene::StringRun>& ours, const json& expect, const std::string& at) {
    std::vector<const json*> theirs;
    for (const char* part : {"text", "hint"})
        for (const json& s : expect.at(part)) theirs.push_back(&s);
    if (ours.size() != theirs.size()) {
        std::fprintf(stderr, "%s: %zu strings, the capture %zu\n", at.c_str(), ours.size(), theirs.size());
        return 1;
    }
    for (size_t i = 0; i < ours.size(); ++i) {
        const json& s = *theirs[i];
        std::string text;
        for (const json& c : s.at("text")) text.push_back(static_cast<char>(c.get<int32_t>()));
        const std::string where = at + " string " + std::to_string(i) + " \"" + text + "\"";
        if (ours[i].text != text || ours[i].measuring != s.at("measuring").get<bool>()) {
            std::fprintf(stderr, "%s: the clock handed \"%s\" (%s)\n", where.c_str(), ours[i].text.c_str(), ours[i].measuring ? "measured" : "drawn");
            return 1;
        }
        if (!sameState(ours[i].own, s.at("own"), where)) return 1;
    }
    return 0;
}

// The glyph cache the text leaves, against the library's at the next frame's first character.
int compareCache(const scene::FontCache& ours, const json& theirs, const std::string& at) {
    const json& list = theirs.at("list");
    if (ours.list.size() != list.size()) {
        std::fprintf(stderr, "%s: cache of %zu entries, the library's %zu\n", at.c_str(), ours.list.size(), list.size());
        return 1;
    }
    for (size_t i = 0; i < list.size(); ++i) {
        const scene::FontCacheEntry& e = ours.list[i];
        const json& t = list[i];
        if (e.code != t.at("code") || e.loaded != t.at("loaded") || e.cell != t.at("cell") || e.block != t.at("block").get<uint32_t>()) {
            std::fprintf(stderr, "%s: cache entry %zu is %x/%d/%d, the library's %x/%d/%d\n", at.c_str(), i, e.code, e.cell, e.loaded, t.at("code").get<int32_t>(),
                         t.at("cell").get<int32_t>(), t.at("loaded").get<int32_t>());
            return 1;
        }
    }
    if (ours.block != theirs.at("block").get<uint32_t>() || ours.cellW != theirs.at("cellW") || ours.cellH != theirs.at("cellH") || ours.width != theirs.at("width")) {
        std::fprintf(stderr, "%s: the cache's layout differs from the library's\n", at.c_str());
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

// The clock alone runs no menus, so the capture must hold them idle: no button pressed, the menu and cube ramps still,
// no entry entered. System Configuration may stand open behind Square (its ramp full), its selected entry's glow moving.
int menusIdle(const json& frame, const std::string& at) {
    const json& input = frame.at("input");
    const json& after = frame.at("expect").at("after");
    const auto busy = [&](const char* why) {
        std::fprintf(stderr, "%s: %s: the menus are not idle\n", at.c_str(), why);
        return 1;
    };
    if (input.contains("pad") && input.at("pad").at("pressed") != 0) return busy("a button is pressed");
    if (after.at("menuRamp") != input.at("menuRamp") || after.at("cubeRamp") != input.at("cubeRamp")) return busy("the menu or cube ramp moved");
    if (input.contains("configPage")) {
        json page = input.at("configPage"), pageAfter = after.at("configPage");
        if (page.at("level") != 0 || pageAfter.at("level") != 0 || input.at("entryActive") != 0 || after.at("entryActive") != 0) return busy("an entry is entered");
        page.erase("glow");
        pageAfter.erase("glow");
        if (page != pageAfter) return busy("System Configuration's page moved");
        if (after.at("mainMenu") != input.at("mainMenu") || after.at("screenCode") != input.at("screenCode")) return busy("the main menu or the screen moved");
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

// Frame 0 as a fixture for ParityTool: the scene's passes, the text's among them, with the oracle's results of the
// draws they match (a pass the oracle culled whole takes the results before it), and the glyph cache's image.
int writeSceneFixture(const parity::GsFrame& ours, const scene::Frame& frame, const scene::Font& font, const std::filesystem::path& f0, const std::filesystem::path& out) {
    std::ifstream in(f0 / "frame.json");
    if (!in) { std::fprintf(stderr, "no frame.json in %s\n", f0.string().c_str()); return 1; }
    json fixture = json::parse(in);
    const auto absolute = [&](const json& relative) { return (f0 / relative.get<std::string>()).generic_string(); };
    for (json& t : fixture.at("targets")) t["start"] = absolute(t.at("start"));
    for (json& t : fixture.at("textures")) t["file"] = absolute(t.at("file"));
    fixture["depthStart"] = absolute(fixture.at("depthStart"));
    const json& theirs = fixture.at("passes");

    // The glyph cache as the frame's text samples it, from the font file and the cells the scene says it holds.
    std::filesystem::create_directories(out / "textures");
    const std::string glyphs = parity::glyphTextureId(frame.glyphs);
    const std::vector<uint8_t> image = scene::glyphCacheImage(font, frame.glyphs);
    const std::filesystem::path glyphFile = out / "textures" / (glyphs + ".rgba");
    std::ofstream(glyphFile, std::ios::binary).write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    fixture.at("textures").push_back({{"id", glyphs}, {"width", 1 << frame.glyphs.logWidth}, {"height", 1 << frame.glyphs.logHeight}, {"file", glyphFile.generic_string()}});

    json passes = json::array();
    std::map<std::string, json> lastColour;
    json lastDepth;
    size_t i = 0, j = 0, culled = 0;
    const auto matchesLater = [&](const parity::GsPass& pass) {
        for (size_t k = i; k < theirs.size(); ++k)
            if (primitivesMatch(pass, parity::readPass(theirs[k]))) return true;
        return false;
    };
    while (i < theirs.size()) {
        const json* oracle = i < theirs.size() ? &theirs[i] : nullptr;
        if (j < ours.passes.size() && oracle && primitivesMatch(ours.passes[j], dumpPass(*oracle))) {
            json p = parity::writePass(ours.passes[j]);
            p["index"] = oracle->at("index");
            p["name"] = oracle->at("name");
            p["oracle"] = {{"colour", absolute(oracle->at("oracle").at("colour"))}, {"depth", absolute(oracle->at("oracle").at("depth"))}};
            lastColour[ours.passes[j].target] = p["oracle"]["colour"];
            lastDepth = p["oracle"]["depth"];
            passes.push_back(std::move(p));
            ++i, ++j;
        } else if (j < ours.passes.size() && !matchesLater(ours.passes[j])) {
            if (!lastColour.contains(ours.passes[j].target) || lastDepth.is_null()) { std::fprintf(stderr, "scene pass %zu is culled before any oracle result of its target\n", j); return 1; }
            json p = parity::writePass(ours.passes[j]);
            p["index"] = 1000 + j;
            p["name"] = "scene-" + std::to_string(j);
            p["oracle"] = {{"colour", lastColour.at(ours.passes[j].target)}, {"depth", lastDepth}};
            passes.push_back(std::move(p));
            ++j, ++culled;
        } else {
            std::fprintf(stderr, "frame 0: oracle %s has no scene pass (scene pass %zu)\n", oracle ? oracle->at("name").get<std::string>().c_str() : "(none)", j);
            return 1;
        }
    }
    // The oracle's frame ends with its last draw: what the scene draws after it has no result to compare.
    std::string beyond;
    for (; j < ours.passes.size(); ++j) beyond += (beyond.empty() ? "" : ", ") + ours.passes[j].name;
    fixture["passes"] = std::move(passes);
    std::filesystem::create_directories(out);
    std::ofstream(out / "frame.json") << fixture.dump();
    std::printf("scene fixture: %zu scene passes (%zu culled whole by the oracle; after the oracle's last draw: %s), glyph cache %s, in %s\n",
                ours.passes.size(), culled, beyond.empty() ? "none" : beyond.c_str(), glyphs.c_str(), out.string().c_str());
    return 0;
}

}

int main(int argc, char** argv) {
    return scenetest::run(argc, argv, [](int argc, char** argv) {
        if (argc != 8) {
            std::fprintf(stderr, "usage: ClockParityTest <scene.json> <rod-mesh.json> <passes.json> <parity fixture f0> <scene fixture out> <FNTOSD> <hddosd.elf>\n");
            return 1;
        }
        const json sceneJson = scenetest::loadScene(argv[1]);
        const scene::RodMesh mesh = scene::loadRodMesh(argv[2]);
        const json dump = scenetest::loadScene(argv[3]);
        const json& frames = sceneJson.at("frames");
        const json& dumpFrames = dump.at("frames");
        CHECK(!frames.empty());
        const auto font = std::make_shared<const scene::Font>(scene::Font::load(argv[6]));
        const auto program = std::make_shared<const scene::ProgramImage>(scene::ProgramImage::load(argv[7]));
        const auto inputs = [&](const json& input) {
            scene::ClockInputs c = scene::clockInputs(input, mesh);
            c.font = font;
            c.program = program;
            return c;
        };

        EeClock clock(inputs(frames.at(0).at("input")));
        size_t compared = 0, passes = 0, strings = 0;
        for (const json& frame : frames) {
            const int index = frame.at("index");
            const std::string at = "frame " + std::to_string(index);
            if (menusIdle(frame, at)) return 1;
            const json& input = frame.at("input");
            // The menus' words the text's alpha reads stand still, and items 9 to 0xB are the time record's hour,
            // minute and second.
            CHECK(input.at("textRamps") == frames.at(0).at("input").at("textRamps"));
            const json& items = input.at("configItems");
            CHECK(items.at(9) == input.at("time").at("hours") && items.at(10) == input.at("time").at("minutes") && items.at(11) == input.at("time").at("seconds"));
            CHECK(input.at("textRamps").at("weight") == input.at("overlayLevel") && input.at("textRamps").at("tail") == input.at("tail") &&
                  input.at("textRamps").at("menu") == input.at("menuRamp") && input.at("textRamps").at("body") == input.at("body"));
            if (index > 0 && compareCache(clock.text()->cache(), input.at("font"), at + " start")) return 1;
            const scene::FrameInputs in = scene::frameInputs(input);
            const scene::Frame out = clock.frame(in);
            if (compareAfter(clock, frame.at("expect").at("after"), at)) return 1;
            if (compareStrings(clock.strings(), frame.at("expect").at("text"), at)) return 1;
            strings += clock.strings().size();
            CHECK(out.field == in.field && out.displayIndex == in.displayIndex);
            const parity::GsFrame gs = parity::fromScene(out, parity::clockLayout(out.width, out.height, out.displayIndex));
            CHECK(gs.passes.size() == out.passes.size());
            if (static_cast<size_t>(index) >= dumpFrames.size()) {
                std::printf("%s: %zu passes; the dump has no frame %d (state and strings compared only)\n", at.c_str(), out.passes.size(), index);
                continue;
            }
            const json& dumpFrame = dumpFrames.at(static_cast<size_t>(index));
            CHECK(dumpFrame.at("frame") == index);
            TextCount count;
            if (compareFrame(gs, dumpFrame, count, at)) return 1;
            CHECK(count.font == 26 && count.hint == 1);
            ++compared;
            passes += gs.passes.size();
            std::printf("%s: %zu scene passes equal to the dump's, %zu of them glyphs and %zu the hint's picture\n", at.c_str(), gs.passes.size(), count.font, count.hint);
        }
        std::printf("geometry: %zu frames carried, %zu scene passes equal to the dump, every dump pass a scene pass; %zu strings equal\n", compared, passes, strings);

        EeClock first(inputs(frames.at(0).at("input")));
        const scene::Frame zero = first.frame(scene::frameInputs(frames.at(0).at("input")));
        if (writeSceneFixture(parity::fromScene(zero, parity::clockLayout(zero.width, zero.height, zero.displayIndex)), zero, *font, argv[4], argv[5])) return 1;

        scene::Clock<scene::NativeArithmetic> native(inputs(frames.at(0).at("input")));
        const scene::Frame plain = native.frame(scene::frameInputs(frames.at(0).at("input")));
        CHECK(!plain.passes.empty());
        CHECK(plain.glyphs == zero.glyphs);
        std::printf("native: frame 0 has %zu passes (Ee %zu)\n", plain.passes.size(), zero.passes.size());

        // The ramps this capture holds still, rising: each frame ticks the grey ramp once (background) and the
        // vignette ramp once (overlay), and the vignette is drawn.
        scene::ClockInputs rising = inputs(frames.at(0).at("input"));
        rising.head.greyRamp = {40, 10, 0, 1};
        rising.state.vignetteRamp = {80, 10, 0, 1};
        EeClock ramps(rising);
        const scene::Frame drawn = ramps.frame(scene::frameInputs(frames.at(0).at("input")));
        CHECK(ramps.head().greyRamp.counter == 11 && ramps.state().vignetteRamp.counter == 11);
        bool vignette = false;
        for (const scene::Pass& pass : drawn.passes) vignette = vignette || pass.name == "vignette";
        CHECK(vignette);
        std::printf("ramps: grey and vignette ramps ticked once each\n");
        return 0;
    });
}
