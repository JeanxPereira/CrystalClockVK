// RodField = the 12-rod clock DIAL (US6693606 2nd embodiment: radial prism rods
// are the clock face). 12 hour positions, evenly spaced; rod 0 at 12 o'clock
// (+Y), going clockwise. (One rod coloured = hour; partial fill = min/sec — later.)

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
bool nearVec(const ps2clock::Vec3& v, float x, float y, float z, float eps) {
    return near(v.x, x, eps) && near(v.y, y, eps) && near(v.z, z, eps);
}

}  // namespace

int main() {
    const ps2clock::RodField field = ps2clock::RodField::Generate();

    // 12-hour clock dial.
    check(field.rods.size() == 12, "dial has 12 rods");

    // Clock layout: rod h points to hour h; 0 = 12 o'clock (up), clockwise.
    check(nearVec(field.rods[0].direction, 0, 1, 0, 1e-5f), "rod 0 = 12 o'clock (+Y)");
    check(nearVec(field.rods[3].direction, 1, 0, 0, 1e-5f), "rod 3 = 3 o'clock (+X)");
    check(nearVec(field.rods[6].direction, 0, -1, 0, 1e-5f), "rod 6 = 6 o'clock (-Y)");
    check(nearVec(field.rods[9].direction, -1, 0, 0, 1e-5f), "rod 9 = 9 o'clock (-X)");

    // Directions are unit, in the dial plane (Z=0), and evenly 30 deg apart.
    const float cos30 = std::cos(30.0f * 3.14159265f / 180.0f);
    for (size_t i = 0; i < field.rods.size(); ++i) {
        const ps2clock::Vec3& d = field.rods[i].direction;
        check(near(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z), 1.0f, 1e-5f),
              "rod " + std::to_string(i) + " unit direction");
        check(near(d.z, 0.0f, 1e-5f), "rod " + std::to_string(i) + " in dial plane");
        const ps2clock::Vec3& n = field.rods[(i + 1) % field.rods.size()].direction;
        check(near(d.x * n.x + d.y * n.y + d.z * n.z, cos30, 1e-4f),
              "rod " + std::to_string(i) + " is 30deg from the next");
    }

    // Each rod's centre sits at the mid radius along its direction.
    const float midR = 0.5f * (field.params.innerRadius + field.params.outerRadius);
    check(near(glm::length(field.rods[0].center), midR, 1e-4f), "rod centre at mid radius");

    // Flat-render mesh: one quad bar per rod (4 verts, 6 indices each).
    const auto mesh = field.buildFlatMesh();
    check(mesh.vertices.size() == field.rods.size() * 4, "mesh = 4 verts/rod");
    check(mesh.indices.size() == field.rods.size() * 6, "mesh = 6 indices/rod");
    check(mesh.indices.size() % 3 == 0, "mesh indices are triangles");

    if (g_fails) { std::printf("rod field: %d FAILURES\n", g_fails); return 1; }
    std::printf("rod field: OK\n");
    return 0;
}
