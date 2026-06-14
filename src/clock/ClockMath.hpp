#pragma once

#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>

namespace clock {

using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using Mat4 = glm::mat4;  // column-major, matching PS2 VU0 column-major storage

// GS hardware encodes screen-space XY as 12.4 fixed-point: trunc(value * 16).
// Evidence: runtime-trace.md rod0 +0x30/+0x34 (0x77b3 = 1915.19*16, 0x8463 = 2118.19*16).
struct Fixed124 {
    static int32_t encode(float value) {
        return static_cast<int32_t>(value * 16.0f);  // truncation toward zero (FTOI4)
    }
    static float decode(int32_t fixed) {
        return static_cast<float>(fixed) / 16.0f;  // VITOF4
    }
};

}  // namespace clock
