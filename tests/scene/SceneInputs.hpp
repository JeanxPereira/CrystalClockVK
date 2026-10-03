#pragma once
#include <nlohmann/json.hpp>

#include "scene/Clock.hpp"

namespace scenetest {

// A frame's `input` of scene.json as the clock's state, and its external inputs.
scene::ClockInputs clockInputs(const nlohmann::json& input, const scene::RodMesh& mesh);
scene::FrameInputs frameInputs(const nlohmann::json& input);

}
