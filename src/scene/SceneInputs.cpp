#include "scene/SceneInputs.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace scene {

namespace {

using nlohmann::json;

float f(const json& hex) { return hexFloat(hex); }
Colour colour(const json& j) { return {j.at(0).get<int32_t>(), j.at(1).get<int32_t>(), j.at(2).get<int32_t>(), j.at(3).get<int32_t>()}; }
Ramp ramp(const json& j) { return {j.at("length"), j.at("counter"), j.at("changed"), j.at("state")}; }

ClockState clockState(const json& in) {
    ClockState s;
    const json& t = in.at("time");
    s.time = {f(t.at("ms")), t.at("seconds"), t.at("minutes"), t.at("hours")};
    const json& e = in.at("eased");
    s.eased = {e.at("secondHand"), e.at("hourHand"), f(e.at("progress")), f(e.at("fraction"))};
    const json& st = in.at("state");
    s.state.currentRod = st.at("currentRod");
    s.state.secondsAngle = st.at("secondsAngle");
    s.state.rodAngle = st.at("rodAngle");
    for (int i = 0; i < 12; ++i) {
        const json& r = st.at("rods").at(i);
        s.state.rods[i] = {f(r.at("appearance")), f(r.at("progress")), colour(r.at("base")), colour(r.at("reflection"))};
    }
    s.state.accent = colour(st.at("accent"));
    s.state.fourth = colour(st.at("fourth"));
    s.state.progressTarget = f(st.at("progressTarget"));
    s.state.accentFrame = colour(st.at("accentFrame"));
    s.state.currentTarget = colour(st.at("currentTarget"));
    s.appearance = ramp(in.at("appearance"));
    const json& c = in.at("colours");
    s.colours = {colour(c.at("base")), colour(c.at("reflection")), colour(c.at("fourth")), colour(c.at("currentReflection"))};
    const json& cc = in.at("cycleCounters");
    s.cycleCounters = {{cc.at("base").at("index"), cc.at("base").at("wait")}, {cc.at("accent").at("index"), cc.at("accent").at("wait")}, cc.at("last")};
    const json& ct = in.at("cycleTables");
    for (int i = 0; i < 6; ++i) s.cycleTables.base[i] = colour(ct.at("base").at(i));
    for (int i = 0; i < 3; ++i) s.cycleTables.accent[i] = colour(ct.at("accent").at(i));
    const json& k = in.at("logicConstants");
    s.logicConstants = {f(k.at("rodEasing")), f(k.at("secondsEasing")), f(k.at("progressStep"))};
    const json& sc = in.at("scene");
    s.scene = {f(sc.at("scale")), sc.at("leaving"), sc.at("field")};
    s.scaleTarget = f(in.at("scaleTarget"));
    s.scaleFactor = f(in.at("scaleFactor"));
    s.timeFilled = in.at("timeFilled");
    s.mode = in.at("mode");
    s.level = in.at("level");
    s.overlayLevel = in.at("overlayLevel");
    s.vignetteRamp = ramp(in.at("vignetteRamp"));
    s.vignetteLength = in.at("vignetteLength");
    s.menuRamp = ramp(in.at("menuRamp"));
    s.tail = in.at("tail");
    s.counter = in.at("counter");
    s.proportions = {f(in.at("proportions").at("ax")), f(in.at("proportions").at("ay"))};
    s.position = hexVec4(in.at("position"));
    s.direction = hexVec4(in.at("direction"));
    s.up = hexVec4(in.at("up"));
    s.rotation = hexVec4(in.at("rotation"));
    s.cameraOffset = f(in.at("cameraOffset"));
    s.zmax = f(in.at("zmax"));
    s.cameraFactor = f(in.at("cameraFactor"));
    return s;
}

Rect rect(const json& j) {
    Rect r;
    r.colour = colour(j.at("colour"));
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

HeadState headState(const json& in) {
    HeadState s;
    s.greyRamp = ramp(in.at("greyRamp"));
    for (int i = 0; i < 3; ++i) s.greys[i] = in.at("greys").at(i);
    s.vignetteRamp = ramp(in.at("vignetteRamp"));
    const json& ring = in.at("ringRecord");
    s.ring = {ring.at("alpha"), ring.at("cx"), ring.at("cy"), ring.at("rx"), ring.at("ry"), ring.at("z")};
    s.tint = rect(in.at("tint"));
    s.blur = rect(in.at("blurRecord"));
    s.copy = rect(in.at("copyRecord"));
    s.fade = rect(in.at("fadeRecord"));
    s.bars = rect(in.at("bars"));
    s.column = rect(in.at("column"));
    return s;
}

RodRecord rodRecord(const json& j) {
    RodRecord r;
    r.number = j.at("number");
    r.faces = j.at("faces");
    r.local = hexMat4(j.at("local"));
    r.sx = f(j.at("sx"));
    r.sy = f(j.at("sy"));
    r.sz = f(j.at("sz"));
    r.base = colour(j.at("base"));
    r.strength = f(j.at("strength"));
    r.textured = colour(j.at("textured"));
    r.pair = {f(j.at("pair").at(0)), f(j.at("pair").at(1))};
    r.refraction = f(j.at("refraction"));
    r.reflection = colour(j.at("reflection"));
    r.extra = colour(j.at("extra"));
    return r;
}

OrbState orbState(const json& in) {
    OrbState s;
    for (int k = 0; k < kOrbCount; ++k) {
        const json& r = in.at("rings").at(k);
        OrbRing& ring = s.rings[k];
        ring.head = r.at("head");
        ring.count = r.at("count");
        ring.full = r.at("full");
        for (int n = 0; n < kRingLength; ++n) {
            const json& e = r.at("entries").at(n);
            ring.entries[n] = {f(e.at("x")), f(e.at("y")), f(e.at("z")), colour(e.at("colour"))};
        }
    }
    s.spriteFade = ramp(in.at("spriteFade"));
    s.fraction = f(in.at("eased").at("fraction"));
    return s;
}

}

uint32_t hexBits(const json& hex) {
    const std::string text = hex.get<std::string>();
    if (text.size() != 10 || text[0] != '0' || text[1] != 'x') throw std::runtime_error("not a float pattern: " + text);
    return static_cast<uint32_t>(std::stoul(text.substr(2), nullptr, 16));
}

float hexFloat(const json& hex) { return asFloat(hexBits(hex)); }

Vec4 hexVec4(const json& hex) { return {hexFloat(hex.at(0)), hexFloat(hex.at(1)), hexFloat(hex.at(2)), hexFloat(hex.at(3))}; }

Mat4 hexMat4(const json& hex) { return {hexVec4(hex.at(0)), hexVec4(hex.at(1)), hexVec4(hex.at(2)), hexVec4(hex.at(3))}; }

ClockInputs clockInputs(const json& in, const RodMesh& mesh) {
    ClockInputs c;
    c.state = clockState(in);
    c.head = headState(in);
    c.rodTemplate = rodRecord(in.at("template"));
    c.orbs = orbState(in);
    c.mesh = mesh;
    c.clearColour = colour(in.at("clearColour"));
    c.firstDisplayClear = colour(in.at("display").at("firstClear"));
    const json& t = in.at("tubeConstants");
    c.tube = {f(t.at("near")), f(t.at("turn")), f(t.at("scroll")), f(t.at("ripple")), f(t.at("scrollEnd")), f(t.at("turnEnd")), f(t.at("far")), f(t.at("radius"))};
    c.minuteFactor = f(in.at("orbConstants").at("minuteFactor"));
    c.fractionEasing = f(in.at("orbConstants").at("fractionEasing"));
    c.orbColour = colour(in.at("orbColour"));
    c.width = in.at("screen").at("width");
    c.height = in.at("screen").at("height");
    return c;
}

json firstInput(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open " + path);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const size_t frames = text.find("\"frames\"");
    if (frames == std::string::npos) return json::parse(text);
    const size_t start = text.find('{', frames);
    if (start == std::string::npos) throw std::runtime_error(path + ": no frames");
    int depth = 0;
    bool quoted = false;
    for (size_t i = start; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            if (c == '\\') ++i;
            else if (c == '"') quoted = false;
        } else if (c == '"') {
            quoted = true;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}' && --depth == 0) {
            return json::parse(text.begin() + static_cast<std::ptrdiff_t>(start), text.begin() + static_cast<std::ptrdiff_t>(i + 1)).at("input");
        }
    }
    throw std::runtime_error(path + ": frame 0 does not end");
}

FrameInputs frameInputs(const json& in) {
    const json& t = in.at("time");
    return {{f(t.at("ms")), t.at("seconds"), t.at("minutes"), t.at("hours")}, in.at("scene").at("field"), in.at("index"), in.at("item0")};
}

}
