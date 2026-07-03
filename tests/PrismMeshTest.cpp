// A dial rod is a 3D crystal PRISM bar, not a flat quad: a rectangular box
// running radially [innerR, outerR], `rodWidth` across the dial plane, and
// `rodDepth` thick perpendicular to it (the +Z/-Z front/back faces the live
// rod struct exposes at +0x150). This is the geometric base the crystal shader
// lights. Pure logic, no Vulkan.

#include <cmath>
#include <cstdio>
#include <string>

#include "clock/ClockMath.hpp"
#include "clock/RodField.hpp"

namespace {
int g_fails = 0;
void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("  FAIL: %s\n", what.c_str()); g_fails++; }
}
bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
}  // namespace

int main() {
    using namespace ps2clock;

    DialParams p;  // defaults: 12 rods, inner 2, outer 6, width 0.7
    p.rodDepth = 0.4f;
    RodField field = RodField::Generate(p);
    PrismMesh mesh = field.buildPrismMesh();

    // Flat facets: 6 faces x 4 verts = 24 verts/rod (corners NOT shared, so each
    // face carries its own normal); 6 faces x 2 tris x 3 = 36 indices/rod.
    check(mesh.vertices.size() == 12 * 24, "24 verts per rod box (flat facets)");
    check(mesh.indices.size() == 12u * 36u, "36 indices per box (6 faces x 2 tris)");

    // Rod 0 (12 o'clock, +Y): its box spans X in [-w/2, +w/2], Y in [inner,outer],
    // Z in [-d/2, +d/2]. Check the AABB over all of rod 0's 24 verts.
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (int i = 0; i < 24; ++i) {
        const Vec3& v = mesh.vertices[i].pos;
        minX = std::min(minX, v.x); maxX = std::max(maxX, v.x);
        minY = std::min(minY, v.y); maxY = std::max(maxY, v.y);
        minZ = std::min(minZ, v.z); maxZ = std::max(maxZ, v.z);
    }
    check(near(minX, -0.35f) && near(maxX, 0.35f), "rod0 width = rodWidth across X");
    check(near(minY, 2.0f) && near(maxY, 6.0f), "rod0 spans inner..outer in Y");
    check(near(minZ, -0.2f) && near(maxZ, 0.2f), "rod0 depth = rodDepth across Z");

    // Every vertex carries a unit face normal (flat crystal facets).
    bool allUnit = true;
    for (const auto& v : mesh.vertices) {
        const float len = std::sqrt(v.normal.x*v.normal.x + v.normal.y*v.normal.y + v.normal.z*v.normal.z);
        if (!near(len, 1.0f, 1e-3f)) allUnit = false;
    }
    check(allUnit, "every prism vertex has a unit normal");

    // The two Z-facing faces (front/back) exist: some vertices normal ~ +Z / -Z.
    bool hasFront = false, hasBack = false;
    for (const auto& v : mesh.vertices) {
        if (near(v.normal.z, 1.0f, 1e-3f)) hasFront = true;
        if (near(v.normal.z, -1.0f, 1e-3f)) hasBack = true;
    }
    check(hasFront && hasBack, "prism has front (+Z) and back (-Z) faces");

    if (g_fails) { std::printf("FAILED (%d)\n", g_fails); return 1; }
    std::printf("OK\n");
    return 0;
}
