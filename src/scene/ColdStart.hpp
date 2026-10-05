#pragma once
#include <memory>

#include "scene/Clock.hpp"
#include "scene/ColdTypes.hpp"
#include "scene/Font.hpp"
#include "scene/ProgramImage.hpp"
#include "scene/Rods.hpp"

namespace scene {

struct ColdAssets {
    std::shared_ptr<const ProgramImage> program;
    std::shared_ptr<const Font> font;
    RodMesh mesh;
    RodMesh cubeMesh;
};

struct ColdStartOut {
    ClockInputs clock;
    FrameInputs frame;
};

template <class A>
ColdStartOut coldStart(const ColdAssets& assets, const ColdInputs& inputs);

inline void fillTime(FrameInputs& frame) { frame.timeFilled = 1; }


}
