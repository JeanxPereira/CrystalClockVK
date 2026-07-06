#include "clock/ClockOrb.hpp"

#include <cmath>

namespace ps2clock {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

std::vector<Vec3> ClockOrb::lightSpots(float phase, float radius) {
    std::vector<Vec3> spots;
    spots.reserve(kSpots);
    for (int i = 0; i < kSpots; ++i) {
        const float a = phase + static_cast<float>(i) * (2.0f * kPi / kSpots);
        spots.push_back(Vec3(std::sin(a) * radius, std::cos(a) * radius, 0.0f));
    }
    return spots;
}

}  // namespace ps2clock
