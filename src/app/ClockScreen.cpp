#include "app/ClockScreen.hpp"

#include "app/NativeFrames.hpp"
#include "scene/SceneInputs.hpp"

namespace app {

ClockScreen::ClockScreen(const scene::ClockInputs& inputs, const scene::FrameInputs& frame) : m_clock(inputs), m_inputs(frame) { firstFrame(m_inputs); }

ClockScreen ClockScreen::fromStart(const std::filesystem::path& start, const std::filesystem::path& mesh) {
    const nlohmann::json input = scene::firstInput(start.string());
    return ClockScreen(scene::clockInputs(input, scene::loadRodMesh(mesh)), scene::frameInputs(input));
}

void ClockScreen::setTime(const scene::ClockTime& time, const scene::ClockItems& items) {
    m_inputs.time = time;
    m_inputs.items = items;
}

void ClockScreen::step() {
    m_frame = m_clock.frame(m_inputs);
    m_inputs.timeFilled = 1;
    nextFrame(m_inputs);
}

}
