#pragma once
#include "scene/Clock.hpp"
#include "scene/ColdTypes.hpp"
#include "scene/ProgramImage.hpp"

namespace scene {

template <class A>
ClockInputs coldStart(const ProgramImage& program, const ColdInputs& inputs);

}
