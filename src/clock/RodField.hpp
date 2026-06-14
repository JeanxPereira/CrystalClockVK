#pragma once

#include <vector>
#include <cstdint>

#include "clock/ClockMath.hpp"

namespace ps2clock {

// The 12-rod clock DIAL (US6693606 2nd embodiment: radial transparent prism
// rods = the clock face). Clean tweakable units in the dial plane (XY, +Y up);
// the camera scales it to the screen. Resolution-independent by construction.
struct DialParams {
    int   count       = 12;    // 12-hour dial (patent: 12 rods, one coloured = hour)
    float innerRadius = 2.0f;  // radius where a bar starts (gap around the centre sphere)
    float outerRadius = 6.0f;  // radius where a bar ends
    float rodWidth    = 0.7f;  // bar thickness (in the dial plane)
};

// One dial rod: a radial prism bar at a clock-hour position.
struct Rod {
    int  hour;        // 0..count-1 dial position (0 = 12 o'clock), clockwise
    Vec3 direction;   // unit radial direction in the dial plane (+Y = up = 12 o'clock)
    Vec3 center;      // world midpoint of the bar (= direction * midRadius)
};

// Flat (vertex-color) vertex for the minimal render: position + RGBA.
struct RodVertex { Vec3 pos; Vec4 color; };
struct FlatMesh  { std::vector<RodVertex> vertices; std::vector<uint32_t> indices; };

class RodField {
public:
    // Generate the dial: `count` bars radiating from the origin, 360/count apart,
    // rod 0 at 12 o'clock (+Y), going clockwise.
    static RodField Generate(const DialParams& params = {});

    // Flat triangle mesh: one quad bar per rod, vertex-coloured white (placeholder
    // until the real prism cross-section + crystal shader land).
    FlatMesh buildFlatMesh() const;

    std::vector<Rod> rods;
    DialParams params;
};

}  // namespace ps2clock
