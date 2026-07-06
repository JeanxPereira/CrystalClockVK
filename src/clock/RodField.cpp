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

}  // namespace ps2clock
