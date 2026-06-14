#include "clock/RodField.hpp"

#include <cmath>

namespace ps2clock {

namespace {
constexpr int kGroupACount = 8;  // rod-pipeline.md Pass1 skip: i > 7
}

RodField RodField::Generate() {
    RodField f;
    f.rods.reserve(kGroupACount);

    // rod0 pinned to the live capture (runtime-trace.md).
    const Vec3 rod0World(-13.039f, 14.666f, 50.271f);
    const float centerY = rod0World.y;
    const float radius = std::sqrt(rod0World.x * rod0World.x + rod0World.z * rod0World.z);
    const float baseAngle = std::atan2(rod0World.z, rod0World.x);

    for (int i = 0; i < kGroupACount; ++i) {
        const float a = baseAngle + (float(i) * 2.0f * 3.14159265358979f / float(kGroupACount));
        Rod r;
        r.world = (i == 0) ? rod0World
                           : Vec3(radius * std::cos(a), centerY, radius * std::sin(a));
        r.scale = Vec3(1, 1, 1);
        r.angle = a;
        f.rods.push_back(r);
    }
    return f;
}

FlatMesh RodField::buildFlatMesh() const {
    FlatMesh m;
    // A small upright quad per rod (two triangles), vertex-colored white.
    // Geometry is placeholder for the flat sanity draw; the real rod prism mesh
    // is built in a later chunk once projection is locked.
    const float hw = 0.5f;   // half width
    const float hh = 4.0f;   // half height
    const Vec4 white(1, 1, 1, 1);

    for (const Rod& rod : rods) {
        const uint32_t base = static_cast<uint32_t>(m.vertices.size());
        const Vec3& c = rod.world;
        m.vertices.push_back({Vec3(c.x - hw, c.y - hh, c.z), white});
        m.vertices.push_back({Vec3(c.x + hw, c.y - hh, c.z), white});
        m.vertices.push_back({Vec3(c.x + hw, c.y + hh, c.z), white});
        m.vertices.push_back({Vec3(c.x - hw, c.y + hh, c.z), white});
        m.indices.insert(m.indices.end(),
                         {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
    }
    return m;
}

}  // namespace ps2clock
