#pragma once

#include <cstdint>
#include <map>

#include <SDL3/SDL.h>

namespace app::padbits {
constexpr uint32_t Triangle = 0x10, Cross = 0x20, Circle = 0x40, Square = 0x80;
constexpr uint32_t Up = 0x1000, Right = 0x2000, Down = 0x4000, Left = 0x8000;

struct PadWords {
    uint32_t held = 0, pressed = 0, released = 0, repeating = 0;
};
}

namespace scene {
using PadWords = app::padbits::PadWords;
}

namespace app {
enum class PadButton { Up, Down, Left, Right, Cross, Circle, Square, Triangle };
uint32_t bitOf(PadButton button);

class PadReader {
public:
    void down(uint32_t source, uint32_t bit);
    void up(uint32_t source, uint32_t bit);
    void dropSource(uint32_t source);
    void clear();
    scene::PadWords frame();
private:
    uint32_t held() const;
    std::map<uint32_t, uint32_t> m_held;
    uint32_t m_previous = 0, m_pressed = 0, m_released = 0;
};

struct Bindings {
    std::map<SDL_Scancode, PadButton> keys;
    std::map<SDL_GamepadButton, PadButton> buttons;
    static Bindings defaults();
    void bindKey(SDL_Scancode key, PadButton button);
    void bindButton(SDL_GamepadButton pad, PadButton button);
};

void feed(PadReader& reader, const Bindings& bindings, const SDL_Event& event);
}
