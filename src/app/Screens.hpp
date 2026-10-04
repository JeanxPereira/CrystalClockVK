#pragma once

#include "scene/MenuTypes.hpp"
#include "scene/Ramp.hpp"

namespace app {
enum class Screen { MainMenu, OpeningConfiguration, Configuration, InsideEntry, ClosingConfiguration, HidingMenu, ClockAlone, ShowingMenu };
Screen screenOf(const scene::MenusState& menus, const scene::Ramp& menuRamp);
const char* screenName(Screen screen);
}
