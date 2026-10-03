#pragma once
#include <cstdint>

#include "scene/ClockState.hpp"
#include "scene/Frame.hpp"
#include "scene/FrameHead.hpp"
#include "scene/Orbs.hpp"
#include "scene/Rods.hpp"

namespace scene {

// The clock's state at a frame's entry: what the logic, the head, the rods and the orbs carry,
// and what stays constant (clear colours, tube constants, orb constants and colour, screen size).
// ClockState owns eased.fraction and vignetteRamp; the copies in OrbState and HeadState are not read.
// The display clear (clock_frame.mjs stateWriters display): the clock writes `clearColour` into the clear of
// display environment 1 only; a frame drawn to buffer 0 is cleared with environment 0's own colour, set when
// the buffers were set up (`firstDisplayClear`).
struct ClockInputs {
    ClockState state;
    HeadState head;
    RodRecord rodTemplate;
    OrbState orbs;
    RodMesh mesh;
    Colour clearColour{};
    Colour firstDisplayClear{};
    TubeConstants tube;
    float minuteFactor = 0;
    float fractionEasing = 0;
    Colour orbColour{};
    int32_t width = 640, height = 224;
};

// What comes from outside the clock each frame: the time keeper, the field flag, the display buffer
// drawn to, and configuration item 0.
struct FrameInputs {
    ClockTime time;
    int32_t field = 0;
    int32_t displayIndex = 0;
    int32_t item0 = 0;
};

// facts/clock-frame.md "Order of a frame", the clock screen's parts: camera; head; rods, orbs and the
// two extra passes; overlay; trips after the rods; bars; (text); column; then the state step.
// The menus, the cubes and the menu ramp step are not part of the clock screen.
template <class A>
class Clock {
public:
    explicit Clock(const ClockInputs& inputs);
    Frame frame(const FrameInputs& in);

    const ClockState& state() const { return m_state; }
    const HeadState& head() const { return m_head.state(); }
    const OrbState& orbs() const { return m_orbs; }
    const RodRecord& rodTemplate() const { return m_rods.rodTemplate(); }

private:
    ClockState m_state;
    FrameHead<A> m_head;
    Rods<A> m_rods;
    OrbState m_orbs;
    Colour m_clearColour, m_firstDisplayClear;
    TubeConstants m_tube;
    float m_minuteFactor, m_fractionEasing;
    Colour m_orbColour;
    int32_t m_width, m_height;
};

extern template class Clock<EeArithmetic>;
extern template class Clock<NativeArithmetic>;

}
