#pragma once
#include <cstdint>
#include <memory>
#include <optional>

#include "scene/ClockState.hpp"
#include "scene/Frame.hpp"
#include "scene/FrameHead.hpp"
#include "scene/Orbs.hpp"
#include "scene/Rods.hpp"
#include "scene/Text.hpp"

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
    // facts/opening.md section 8, clock_frame.mjs orbEntry: the orbs' entry motion and colours (overlay modes 2 and 3).
    int32_t wide = 0;
    std::array<int32_t, kOrbCount> orbRandom{};
    std::array<Colour, kOrbCount> orbColours{};
    int32_t width = 640, height = 224;
    // The text (facts/text.md): the font file FNTOSD and the program's ELF, and the font state carried. Without the
    // two files the frame has no text.
    std::shared_ptr<const Font> font;
    std::shared_ptr<const ProgramImage> program;
    TextInputs text;
};

// What comes from outside the clock each frame: the time keeper, the field flag, the display buffer
// drawn to, configuration item 0, and configuration items 6 to 0xB (the date and time the text shows).
struct FrameInputs {
    ClockTime time;
    int32_t field = 0;
    int32_t displayIndex = 0;
    int32_t item0 = 0;
    ClockItems items;
    // 0 until the time keeper has read the clock once (the scene scale stays zero); the clock entered from the opening starts so.
    int32_t timeFilled = 1;
};

// facts/clock-frame.md "Order of a frame", the clock screen's parts: camera; head; rods, orbs and the
// two extra passes; overlay; trips after the rods; bars; text (date and time, button hint); column; then the state step.
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
    const Text<A>* text() const { return m_text ? &*m_text : nullptr; }
    // The strings of the last frame, as handed to the font code.
    const std::vector<StringRun>& strings() const { return m_strings; }

private:
    ClockState m_state;
    FrameHead<A> m_head;
    Rods<A> m_rods;
    OrbState m_orbs;
    Colour m_clearColour, m_firstDisplayClear;
    TubeConstants m_tube;
    float m_minuteFactor, m_fractionEasing;
    Colour m_orbColour;
    int32_t m_wide;
    std::array<int32_t, kOrbCount> m_orbRandom;
    std::array<Colour, kOrbCount> m_orbColours;
    int32_t m_width, m_height;
    std::optional<Text<A>> m_text;
    std::vector<StringRun> m_strings;
};

extern template class Clock<EeArithmetic>;
extern template class Clock<NativeArithmetic>;

}
