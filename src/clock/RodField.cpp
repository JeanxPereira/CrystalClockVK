#include "clock/RodField.hpp"

#include <cmath>

namespace ps2clock {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

RodField RodField::Generate(const DialParams& params, float spinPhase) {
    RodField f;
    f.params = params;
    f.rods.reserve(static_cast<size_t>(params.count));

    const float midRadius = 0.5f * (params.innerRadius + params.outerRadius);
    for (int h = 0; h < params.count; ++h) {
        // Clock layout: hour h at angle h*(2pi/count) measured CLOCKWISE from +Y.
        // dir = (sin a, cos a, 0): h=0 -> (0,1,0)=up=12h; h=count/4 -> (1,0,0)=3h.
        // The group spin subtracts from the clockwise angle (positive spinPhase =
        // counter-clockwise), rotating the whole dial rigidly.
        const float a = static_cast<float>(h) * (2.0f * kPi / static_cast<float>(params.count))
                        - spinPhase;
        const Vec3 dir(std::sin(a), std::cos(a), 0.0f);
        Rod r;
        r.hour = h;
        r.direction = dir;
        r.center = dir * midRadius;
        f.rods.push_back(r);
    }
    return f;
}

FlatMesh RodField::buildFlatMesh() const {
    FlatMesh m;
    const Vec4 white(1.0f, 1.0f, 1.0f, 1.0f);
    const float hw = 0.5f * params.rodWidth;

    for (const Rod& rod : rods) {
        const Vec3& dir = rod.direction;
        const Vec3 perp(-dir.y, dir.x, 0.0f);  // in-plane perpendicular to the bar axis
        const Vec3 inner = dir * params.innerRadius;
        const Vec3 outer = dir * params.outerRadius;

        const uint32_t base = static_cast<uint32_t>(m.vertices.size());
        m.vertices.push_back({inner - perp * hw, white});  // 0 inner-left
        m.vertices.push_back({inner + perp * hw, white});  // 1 inner-right
        m.vertices.push_back({outer + perp * hw, white});  // 2 outer-right
        m.vertices.push_back({outer - perp * hw, white});  // 3 outer-left
        m.indices.insert(m.indices.end(),
                         {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
    }
    return m;
}

FlatMesh RodField::buildDialMesh(const ClockState& state) const {
    FlatMesh m;
    const float hw = 0.5f * params.rodWidth;
    // AM blue / PM red highlight; unlit rods a dim steel blue.
    const Vec4 lit  = (state.amPm == AmPm::PM) ? Vec4(0.95f, 0.25f, 0.20f, 1.0f)
                                               : Vec4(0.35f, 0.70f, 1.0f, 1.0f);
    const Vec4 base = (state.amPm == AmPm::PM) ? Vec4(0.40f, 0.14f, 0.12f, 1.0f)
                                               : Vec4(0.16f, 0.30f, 0.44f, 1.0f);
    const Vec4 dim(0.12f, 0.20f, 0.28f, 1.0f);

    // Emit one bar quad [r0,r1] along `dir`, coloured `col`.
    auto quad = [&](const Vec3& dir, const Vec3& perp, float r0, float r1, const Vec4& col) {
        const Vec3 a = dir * r0, b = dir * r1;
        const uint32_t base = static_cast<uint32_t>(m.vertices.size());
        m.vertices.push_back({a - perp * hw, col});
        m.vertices.push_back({a + perp * hw, col});
        m.vertices.push_back({b + perp * hw, col});
        m.vertices.push_back({b - perp * hw, col});
        m.indices.insert(m.indices.end(),
                         {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
    };

    for (const Rod& rod : rods) {
        const Vec3& dir = rod.direction;
        const Vec3 perp(-dir.y, dir.x, 0.0f);
        if (rod.hour != state.litRod) {
            quad(dir, perp, params.innerRadius, params.outerRadius, dim);
            continue;
        }
        // The hour rod: full body in the AM/PM base tint, plus the min/sec
        // partial-fill [inner, inner+fill*len] in the bright highlight colour.
        const float span = params.outerRadius - params.innerRadius;
        const float fillR = params.innerRadius + state.fill * span;
        quad(dir, perp, params.innerRadius, params.outerRadius, base);
        if (state.fill > 0.0f) quad(dir, perp, params.innerRadius, fillR, lit);
    }
    return m;
}

PrismMesh RodField::buildPrismMesh() const {
    return buildDialPrism(ClockState{-1, 0.0f, AmPm::AM});  // litRod=-1: all white body
}

PrismMesh RodField::buildDialPrism(const ClockState& state) const {
    PrismMesh m;
    const Vec4 lit  = (state.amPm == AmPm::PM) ? Vec4(0.95f, 0.30f, 0.24f, 1.0f)
                                               : Vec4(0.40f, 0.75f, 1.0f, 1.0f);
    const Vec4 dim(0.14f, 0.24f, 0.34f, 1.0f);
    const float hw = 0.5f * params.rodWidth;   // half-width across the dial plane
    const float hd = 0.5f * params.rodDepth;   // half-depth along Z

    for (const Rod& rod : rods) {
        // litRod<0 keeps a plain white body (buildPrismMesh); else colour the
        // hour rod bright and the rest dim. col.a carries the min/sec fill
        // fraction for the lit rod (crystal shader thresholds along uv.u);
        // 1.0 elsewhere = uniformly present.
        Vec4 col;
        if (state.litRod < 0) col = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
        else if (rod.hour == state.litRod) col = Vec4(lit.x, lit.y, lit.z, state.fill);
        else col = Vec4(dim.x, dim.y, dim.z, 1.0f);
        const Vec3 axis = rod.direction;              // radial (length) axis
        const Vec3 perp(-axis.y, axis.x, 0.0f);       // in-plane width axis
        const Vec3 zdir(0.0f, 0.0f, 1.0f);            // depth axis
        const Vec3 c0 = axis * params.innerRadius;    // inner end centre
        const Vec3 c1 = axis * params.outerRadius;    // outer end centre

        // 8 box corners: end (i: 0=inner,1=outer) x width sign x depth sign.
        auto corner = [&](int end, float ws, float ds) {
            return (end ? c1 : c0) + perp * (ws * hw) + zdir * (ds * hd);
        };
        // 6 faces, each a quad (4 corners CCW) with a flat outward normal + uv.
        // uv.u runs inner(0)->outer(1) along the length; uv.v across the width.
        using glm::vec2;
        struct FV { Vec3 p; vec2 uv; };
        struct Face { Vec3 n; FV a, b, c, d; };
        const Face faces[6] = {
            {  zdir, {corner(0,-1,+1),{0,0}}, {corner(1,-1,+1),{1,0}}, {corner(1,+1,+1),{1,1}}, {corner(0,+1,+1),{0,1}} }, // front +Z
            { -zdir, {corner(0,+1,-1),{0,1}}, {corner(1,+1,-1),{1,1}}, {corner(1,-1,-1),{1,0}}, {corner(0,-1,-1),{0,0}} }, // back  -Z
            {  perp, {corner(0,+1,+1),{0,1}}, {corner(1,+1,+1),{1,1}}, {corner(1,+1,-1),{1,1}}, {corner(0,+1,-1),{0,1}} }, // +width
            { -perp, {corner(0,-1,-1),{0,0}}, {corner(1,-1,-1),{1,0}}, {corner(1,-1,+1),{1,0}}, {corner(0,-1,+1),{0,0}} }, // -width
            {  axis, {corner(1,-1,+1),{1,0}}, {corner(1,-1,-1),{1,0}}, {corner(1,+1,-1),{1,1}}, {corner(1,+1,+1),{1,1}} }, // outer cap
            { -axis, {corner(0,+1,+1),{0,1}}, {corner(0,+1,-1),{0,1}}, {corner(0,-1,-1),{0,0}}, {corner(0,-1,+1),{0,0}} }, // inner cap
        };
        for (const Face& f : faces) {
            const uint32_t base = static_cast<uint32_t>(m.vertices.size());
            m.vertices.push_back({f.a.p, f.n, col, f.a.uv});
            m.vertices.push_back({f.b.p, f.n, col, f.b.uv});
            m.vertices.push_back({f.c.p, f.n, col, f.c.uv});
            m.vertices.push_back({f.d.p, f.n, col, f.d.uv});
            m.indices.insert(m.indices.end(),
                             {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
        }
    }
    return m;
}

}  // namespace ps2clock
