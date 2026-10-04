#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "scene/ClockState.hpp"
#include "scene/FrameHead.hpp"
#include "scene/MenuTypes.hpp"
#include "scene/Rods.hpp"

namespace scene {

// References/model/clock_cubes.mjs, HDD OSD 1.10U (the ring of six).
enum class CubeSend { RefractedFar, GrainOffsetFar, GrainPlainFar, EdgeColour, Depth, HalfBuffer, RefractedNear, GrainOffsetNear, GrainPlainNear,
                      LayerClear, LayerReflection, LayerAlpha, Added, ChainShrink, ChainStretch, ToDisplay };

struct CubeDraw {
    CubeSend send = CubeSend::RefractedFar;
    std::string label;
    std::vector<RodFaceDraw> faces;
    std::optional<HeadDraw> rectangle;
    Colour clear{};
};

struct CubeFrameInputs {
    int32_t width = 640, height = 224, field = 0, body = 0;
};

template <class A>
class Cubes {
public:
    explicit Cubes(RodMesh mesh);

    std::vector<CubeDraw> frame(CubeState& cubes, HeadState& head, const ClockState& clock, const CubeFrameInputs& in);

private:
    RodMesh m_mesh;
};

}
