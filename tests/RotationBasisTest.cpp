// rotation_build (FUN_002732d8): direct orthonormal basis from two direction
// vectors via two cross products. right = normalize(cross(fwd, up)),
// upOrtho = cross(right, fwd), columns (right, upOrtho, fwd, (0,0,0,1)).
// NOT Euler, NOT Rodrigues. Evidence: vu0-math-pipeline.md §"Rotation Build".

#include <cstdio>
#include <string>

#include <glm/gtc/matrix_access.hpp>

#include "clock/ClockMath.hpp"
#include "clock/Projection.hpp"

namespace {

int g_fails = 0;
void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("  FAIL: %s\n", what.c_str()); g_fails++; }
}
bool near(float a, float b, float eps = 1e-5f) { return (a - b < eps) && (b - a < eps); }

void assertOrthonormal(const clock::Mat4& m, const std::string& tag) {
    clock::Vec3 c0 = clock::Vec3(glm::column(m, 0));
    clock::Vec3 c1 = clock::Vec3(glm::column(m, 1));
    clock::Vec3 c2 = clock::Vec3(glm::column(m, 2));

    check(near(glm::length(c0), 1.0f), tag + " col0 unit");
    check(near(glm::length(c1), 1.0f), tag + " col1 unit");
    check(near(glm::length(c2), 1.0f), tag + " col2 unit");

    check(near(glm::dot(c0, c1), 0.0f), tag + " col0.col1 perpendicular");
    check(near(glm::dot(c1, c2), 0.0f), tag + " col1.col2 perpendicular");
    check(near(glm::dot(c0, c2), 0.0f), tag + " col0.col2 perpendicular");

    check(near(glm::determinant(clock::Mat4(clock::Vec4(c0, 0), clock::Vec4(c1, 0),
                                            clock::Vec4(c2, 0), clock::Vec4(0, 0, 0, 1))),
               1.0f),
          tag + " right-handed det=+1");

    // W column is (0,0,0,1) — no translation in the rotation matrix.
    clock::Vec4 c3 = glm::column(m, 3);
    check(near(c3.x, 0) && near(c3.y, 0) && near(c3.z, 0) && near(c3.w, 1), tag + " no translation");
}

}  // namespace

int main() {
    // Axis-aligned: forward = -Z, up = +Y → identity-like basis.
    assertOrthonormal(clock::BuildRotationMatrix(clock::Vec3(0, 0, -1), clock::Vec3(0, 1, 0)),
                      "forward=-Z up=+Y");

    // Skewed inputs (up not perpendicular to forward) must still orthonormalize.
    assertOrthonormal(clock::BuildRotationMatrix(clock::Vec3(1, 0.2f, -0.3f), clock::Vec3(0.1f, 1, 0)),
                      "skewed");

    // Arbitrary rod-ish direction.
    assertOrthonormal(clock::BuildRotationMatrix(clock::Vec3(-13.0f, 14.0f, 50.0f), clock::Vec3(0, 1, 0)),
                      "rod0 direction");

    // NaN guard (bc1t -> memclr): degenerate input (forward parallel to up) zeroes the matrix.
    clock::Mat4 z = clock::BuildRotationMatrix(clock::Vec3(0, 1, 0), clock::Vec3(0, 1, 0));
    check(near(glm::column(z, 0).x, 0) && near(glm::column(z, 0).y, 0) && near(glm::column(z, 0).z, 0),
          "degenerate input zeroes matrix");

    if (g_fails) { std::printf("rotation basis: %d FAILURES\n", g_fails); return 1; }
    std::printf("rotation basis: OK\n");
    return 0;
}
