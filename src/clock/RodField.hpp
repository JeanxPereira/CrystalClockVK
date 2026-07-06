#pragma once

#include <vector>
#include <cstdint>

#include "clock/ClockMath.hpp"
#include "clock/ClockState.hpp"

namespace ps2clock {

// The 12-rod clock DIAL (US6693606 2nd embodiment: radial transparent prism
// rods = the clock face). Clean tweakable units in the dial plane (XY, +Y up);
// the camera scales it to the screen. Resolution-independent by construction.
struct DialParams {
    int   count       = 12;    // 12-hour dial (patent: 12 rods, one coloured = hour)
    float innerRadius = 2.0f;  // radius where a bar starts (gap around the centre sphere)
    float outerRadius = 6.0f;  // radius where a bar ends
    float rodWidth    = 0.7f;  // bar width (across the dial plane)
    float rodDepth    = 0.4f;  // bar thickness (perpendicular to the dial plane, +/-Z)
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
    // rod 0 at 12 o'clock (+Y), going clockwise. `spinPhase` (radians) rotates
    // the whole dial about its centre (the group spin, ClockState::spinPhase);
    // positive = counter-clockwise in the dial plane.
    static RodField Generate(const DialParams& params = {}, float spinPhase = 0.0f);

    // Flat triangle mesh: one quad bar per rod, vertex-coloured; kept as evidence-grounded 2D
    // geometry for tray-icon dialface rendering.
    FlatMesh buildFlatMesh() const;

    // Flat mesh driven by the clock state: the lit (hour) rod is highlighted in
    // the AM/PM colour (AM blue, PM red), filled `state.fill` of the way out
    // from the inner end; the other 11 rods are dim. The lit rod's dial index
    // is matched against each rod's `hour` field (0..11).
    FlatMesh buildDialMesh(const ClockState& state) const;

    std::vector<Rod> rods;
    DialParams params;
};

}  // namespace ps2clock
