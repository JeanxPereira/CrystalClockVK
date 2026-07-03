#pragma once

#include <vector>

#include "clock/ClockMath.hpp"
#include "clock/ClockState.hpp"

namespace ps2clock {

// The clock's centre: 8 light spots orbiting the hub (US6693606 element 308,
// live array 0x34c830, 8x0x10; FUN_002354c8 updates them) plus the faint swirl
// wireframe. The spots ride a ring inside the dial hole and rotate with a phase;
// each leaves a fading after-image trail (patent's after-image, live +0x50..).
struct SpotVertex { Vec3 pos; glm::vec2 uv; Vec4 color; };  // uv in [-1,1] for radial glow
struct SpotMesh   { std::vector<SpotVertex> vertices; std::vector<uint32_t> indices; };

class ClockOrb {
public:
    static constexpr int kSpots = 8;

    // The 8 light-spot centres at animation `phase` (radians): evenly 45deg
    // apart on a ring of `radius`, rotated by phase. (Quadratic-radius polar
    // spread is a later refinement; a fixed ring reads correctly for now.)
    static std::vector<Vec3> lightSpots(float phase, float radius = 1.25f);

    // Billboard quads for the spots + their fading trails, coloured by the clock
    // state (AM cyan / PM warm). `trail` past positions per spot fade out.
    // `spotSize` is the half-extent of each glow billboard (world units).
    static SpotMesh buildSpotMesh(const ClockState& state, float phase,
                                  int trail = 4, float spotSize = 0.35f,
                                  float radius = 1.25f);
};

}  // namespace ps2clock
