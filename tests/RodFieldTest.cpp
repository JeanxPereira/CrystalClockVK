// RodField geometry. The rod array @ 0x00375250 is the geometry to reproduce.
// rod0 world (+0x00/04/08) = (-13.039, 14.666, 50.271). Group A has 8 front rods
// (rod-pipeline.md §Pass1: "i > 7 (group A skip)"); this chunk generates group A.

#include <cstdio>
#include <string>
#include <cmath>

#include "clock/ClockMath.hpp"
#include "clock/RodField.hpp"

namespace {

int g_fails = 0;
void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("  FAIL: %s\n", what.c_str()); g_fails++; }
}
bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

}  // namespace

int main() {
    const ps2clock::RodField field = ps2clock::RodField::Generate();

    // Group A is 8 front rods (rod-pipeline.md Pass1 skip condition i > 7).
    check(field.rods.size() == 8, "group A has 8 rods");

    // rod0 world position matches the live trace within 0.05 (capture precision).
    const ps2clock::Rod& r0 = field.rods[0];
    check(near(r0.world.x, -13.039f, 0.05f), "rod0 world X = -13.039");
    check(near(r0.world.y, 14.666f, 0.05f), "rod0 world Y = 14.666");
    check(near(r0.world.z, 50.271f, 0.05f), "rod0 world Z = 50.271");

    // Default scale is unit (trace +0x10/+0x14 = 1.0).
    check(near(r0.scale.x, 1.0f, 1e-4f) && near(r0.scale.y, 1.0f, 1e-4f), "rod0 unit scale");

    // Rods sit on a ring: all share ~constant radius in XZ from a common center.
    const float r0Radius = std::sqrt(r0.world.x * r0.world.x + r0.world.z * r0.world.z);
    for (size_t i = 0; i < field.rods.size(); ++i) {
        const ps2clock::Vec3& w = field.rods[i].world;
        float rad = std::sqrt(w.x * w.x + w.z * w.z);
        check(near(rad, r0Radius, 1.0f), "rod " + std::to_string(i) + " on ring radius");
    }

    // Flat-render mesh: each rod yields vertices + indices (non-empty).
    const auto mesh = field.buildFlatMesh();
    check(!mesh.vertices.empty(), "flat mesh has vertices");
    check(!mesh.indices.empty(), "flat mesh has indices");
    check(mesh.indices.size() % 3 == 0, "flat mesh indices are triangles");

    if (g_fails) { std::printf("rod field: %d FAILURES\n", g_fails); return 1; }
    std::printf("rod field: OK\n");
    return 0;
}
