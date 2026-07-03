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

SpotMesh ClockOrb::buildSpotMesh(const ClockState& state, float phase,
                                 int trail, float spotSize, float radius) {
    SpotMesh m;
    // Bright warm-white spots; a faint AM cyan / PM warm tint mixed in.
    const Vec4 tint = (state.amPm == AmPm::PM) ? Vec4(1.0f, 0.85f, 0.75f, 1.0f)
                                               : Vec4(0.80f, 0.95f, 1.0f, 1.0f);

    // The trail lags the spot's motion: sample past phases (the ring rotates
    // clockwise as phase grows), each dimmer and smaller.
    const float dPhase = 0.09f;  // trail spacing along the orbit
    for (int t = 0; t <= trail; ++t) {
        const float ph = phase - static_cast<float>(t) * dPhase;
        const float fade = 1.0f - static_cast<float>(t) / static_cast<float>(trail + 1);
        const float sz = spotSize * (0.5f + 0.5f * fade);
        const Vec4 col = tint * (fade * fade);  // quadratic fade (after-image)
        for (const Vec3& c : lightSpots(ph, radius)) {
            const uint32_t base = static_cast<uint32_t>(m.vertices.size());
            // Screen-aligned quad in the dial plane (XY); uv in [-1,1] for the
            // radial glow falloff in the fragment shader.
            m.vertices.push_back({c + Vec3(-sz, -sz, 0), {-1.0f, -1.0f}, col});
            m.vertices.push_back({c + Vec3( sz, -sz, 0), { 1.0f, -1.0f}, col});
            m.vertices.push_back({c + Vec3( sz,  sz, 0), { 1.0f,  1.0f}, col});
            m.vertices.push_back({c + Vec3(-sz,  sz, 0), {-1.0f,  1.0f}, col});
            m.indices.insert(m.indices.end(),
                             {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
        }
    }
    return m;
}

}  // namespace ps2clock
