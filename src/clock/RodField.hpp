#pragma once

#include <vector>
#include <cstdint>

#include "clock/ClockMath.hpp"

namespace ps2clock {

// One crystal rod. Mirrors the on-PS2 ROD record fields this chunk needs
// (rod-pipeline.md ROD struct 0x160; runtime-trace.md +0x00 world, +0x10 scale).
struct Rod {
    Vec3 world;          // +0x00/04/08
    Vec3 scale{1, 1, 1}; // +0x10/14
    float angle{0.0f};   // per-frame rotation angle (+0x04 per render loop)
};

// Flat (vertex-color) vertex for the minimal render: world pos + RGBA color.
struct RodVertex {
    Vec3 pos;
    Vec4 color;
};

struct FlatMesh {
    std::vector<RodVertex> vertices;
    std::vector<uint32_t> indices;
};

class RodField {
public:
    // Generate group A (the 8 front rods of the ring) at their captured ring
    // positions. rod0 is pinned to the trace; the rest are distributed on the
    // ring. (Full radial parameterization is fitted in a later chunk.)
    static RodField Generate();

    // Build a flat triangle mesh (a small quad/prism per rod) for the sanity draw.
    FlatMesh buildFlatMesh() const;

    std::vector<Rod> rods;
};

}  // namespace ps2clock
