#include "scene/Menus.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

#include "scene/ClockDate.hpp"

namespace scene {

namespace {

constexpr int32_t kStep = 3000;
constexpr uint32_t kAspectConfirm = 0x00227d30, kAdjustEnter = 0x00226fd0, kAdjustConfirm = 0x00227b90, kAdjustCancel = 0x00227be8;
constexpr uint32_t kEditor = 0x00227ad0, kAdjustString = 0x00227420, kItemString = 0x00228470, kConfigEntries = 0x002b2bf0;

enum class Slot { Enter, Confirm, Cancel };

// func_00239018 / func_00238FB8: the orbs' sprite ramp turns round where it is when moving.
void spritesHide(MenuWorld& w) {
    Ramp& r = w.spriteFade;
    if (r.state == 2) Menus::hide(r);
    else if (r.state == 1) r.state = 3;
}
void spritesShow(MenuWorld& w) {
    Ramp& r = w.spriteFade;
    if (r.state == 0) Menus::show(r);
    else if (r.state == 3) r.state = 1;
}

// func_0022F078 / func_0022F110: the rods' appearance ramp, every rod's length cleared.
void appearance(MenuWorld& w, bool up) {
    Ramp& ramp = w.clock.appearance;
    if (ramp.state != (up ? 0 : 2)) return;
    if (up) {
        ramp.counter = 0;
        ramp.changed = 0;
        ramp.state = 0;
        Menus::show(ramp);
    } else Menus::hide(ramp);
    auto& state = w.clock.state;
    for (int32_t i = 0; i < 12; ++i) state.rods[static_cast<size_t>((i + state.currentRod) % 12)].appearance = 0;
}

int32_t aspectOf(const MenuExternals& ext) {
    if (!ext.mechaconParam) throw std::runtime_error("mechaconParam is not in the inputs");
    const int32_t ratio = static_cast<int32_t>((ext.mechaconParam->at(0) >> 1) & 3);
    return ratio < 3 ? ratio : 0;
}

void reloadItem0(MenuWorld& w, const MenuExternals& ext) {
    if (w.menus.configGate && ext.mechaconParam && *w.menus.configGate == 1) w.items[0] = aspectOf(ext);
}

bool dirty(const MenuExternals& ext) {
    if (!ext.configDirty) throw std::runtime_error("configDirty is not in the inputs");
    const auto& d = *ext.configDirty;
    return d[0] != 0 || d[1] != 0 || d[2] != 0;
}

void setGate(MenuWorld& w, int32_t value) {
    if (w.menus.configGate) w.menus.configGate = value;
}

ConfigEntry& entryOf(MenuWorld& w) {
    const int64_t index = (int64_t(w.menus.page.entries) - kConfigEntries) / 0x38 + w.menus.page.selected;
    if (index < 0 || index > 8) throw std::runtime_error("the selected entry is outside the list");
    return w.menus.entries[static_cast<size_t>(index)];
}

// func_002358F8: the time record from configuration items 6 to 0xB, milliseconds 0; not yet filled.
void timeFromItems(MenuWorld& w) {
    w.clock.time.milliseconds = 0;
    w.clock.time.seconds = w.items[0xb];
    w.clock.time.minutes = w.items[0xa];
    w.clock.time.hours = w.items[9];
    w.clock.timeFilled = 0;
}

// func_00235848 then module_clock_set_anim_offset: the console's clock moved to the configured zone.
void timeFromClock(MenuWorld& w, const MenuExternals& ext, std::vector<std::string>& notes) {
    if (!ext.rtcMirror || !ext.mechaconParam) {
        notes.push_back("the console clock is not in the capture: the time after a cancel is not modelled");
        return;
    }
    const auto& mirror = *ext.rtcMirror;
    const uint32_t param = ext.mechaconParam->at(0);
    const int32_t offset = static_cast<int32_t>(param << 12) >> 21;
    const int32_t summer = static_cast<int32_t>((param >> 29) & 1);
    const auto moved = dateOf(secondsOf(mirror[0], mirror[1], mirror[2], mirror[3], mirror[4], mirror[5]) + int64_t(((offset - kBaseZone) + summer * 60) * 60));
    w.clock.time.milliseconds = 0;
    w.clock.time.seconds = moved[5];
    w.clock.time.minutes = moved[4];
    w.clock.time.hours = moved[3];
    w.clock.timeFilled = 0;
}

// func_00226E68: the first three fields in the order of the date format.
void orderFields(MenuWorld& w, const MenuExternals& ext) {
    if (!ext.mechaconParam) return;
    struct Field {
        int32_t item, low, high;
    };
    static constexpr Field year{6, 2000, 2099}, month{7, 1, 12}, day{8, 1, 31};
    const uint32_t format = ext.mechaconParam->at(1) & 3;
    const Field* order[3];
    if (format == 1) { order[0] = &month; order[1] = &day; order[2] = &year; }
    else if (format == 2) { order[0] = &day; order[1] = &month; order[2] = &year; }
    else if (format == 0) { order[0] = &year; order[1] = &month; order[2] = &day; }
    else return;
    for (size_t k = 0; k < 3; ++k) w.menus.adjustFields[k] = {order[k]->item, order[k]->low, order[k]->high};
}

// func_00227488 on the items, and the fields' ranges written back.
void checkDate(MenuWorld& w, const MenuExternals& ext, const ConfigEntry& entry) {
    if (!ext.mechaconParam) return;
    const std::array<int32_t, 6> now{w.items[6], w.items[7], w.items[8], w.items[9], w.items[10], w.items[11]};
    const DateCheck check = dateCheck(now, ext.mechaconParam->at(0));
    for (size_t i = 0; i < 6; ++i) w.items[6 + i] = check.items[i];
    for (int32_t k = 0; k < entry.valueCount; ++k) {
        AdjustField& field = w.menus.adjustFields.at(static_cast<size_t>(k));
        if (field.item < 6 || field.item > 11) continue;
        field.lowest = check.ranges[static_cast<size_t>(field.item - 6)][0];
        field.highest = check.ranges[static_cast<size_t>(field.item - 6)][1];
    }
}

// D_00226FD0: Clock Adjustment opens with the seconds at 0.
void adjustmentOpens(MenuWorld& w, const MenuExternals& ext, const ConfigEntry& entry) {
    orderFields(w, ext);
    w.clock.scaleTarget = 0;
    spritesHide(w);
    w.items[0xb] = 0;
    timeFromItems(w);
    checkDate(w, ext, entry);
}

void callback(MenuWorld& w, const MenuExternals& ext, Slot slot, std::vector<std::string>& notes) {
    ConfigEntry& entry = entryOf(w);
    const uint32_t address = slot == Slot::Enter ? entry.enter : slot == Slot::Confirm ? entry.confirm : entry.cancel;
    switch (address) {
    case kAspectConfirm: break;
    case kAdjustEnter: adjustmentOpens(w, ext, entry); break;
    case kAdjustConfirm:
        w.clock.scaleTarget = 1;
        spritesShow(w);
        break;
    case kAdjustCancel:
        timeFromClock(w, ext, notes);
        w.clock.scaleTarget = 1;
        spritesShow(w);
        break;
    default: break;
    }
}

// D_00227AD0, every frame while Clock Adjustment is entered.
void adjustmentFrame(MenuWorld& w, const MenuExternals& ext) {
    ConfigEntry& entry = entryOf(w);
    if (entry.frameCallback != kEditor) return;
    if (w.clock.menuRamp.state == 0) {
        const uint32_t pressed = ext.pad.pressed, repeat = ext.pad.repeating;
        const int32_t count = entry.valueCount;
        int32_t cursor = entry.valueIndex;
        if (pressed & pad::Left) {
            cursor -= 1;
            if (cursor < 0) cursor += count;
        }
        if (pressed & pad::Right) {
            cursor += 1;
            if (cursor == count) cursor = 0;
        }
        entry.valueIndex = cursor;
        const AdjustField field = w.menus.adjustFields.at(static_cast<size_t>(cursor));
        int32_t& value = w.items.at(static_cast<size_t>(field.item));
        if (repeat & pad::Up) {
            const int32_t next = value + 1;
            value = next > field.highest ? field.lowest : next;
            checkDate(w, ext, entry);
        }
        if (repeat & pad::Down) {
            const int32_t next = value - 1;
            value = next < field.lowest ? field.highest : next;
            checkDate(w, ext, entry);
        }
    }
    timeFromItems(w);
}

// func_002283C0: the entry's value index where its item sits in its value table.
void valueIndex(ConfigEntry& entry, int32_t item) {
    constexpr int32_t values[3] = {0, 1, 2};
    const int32_t count = entry.valueCount;
    if (item == values[entry.valueIndex] || count <= 0) return;
    entry.valueIndex = 0;
    for (int32_t n = 0; n < count; ++n)
        if (item == values[n]) {
            entry.valueIndex = n;
            return;
        }
}

void stringCallback(MenuWorld& w, const MenuExternals& ext) {
    ConfigEntry& entry = entryOf(w);
    if (entry.stringCallback == kAdjustString) orderFields(w, ext);
    else if (entry.stringCallback == kItemString && entry.item == 0) valueIndex(entry, w.items[0]);
}

// func_00230860: ask the cubes' list to move one place.
void listMove(MenuWorld& w, int32_t direction) {
    CubeList& list = w.cubes.list;
    const bool pal = w.menus.videoMode == 2;
    const int32_t rate = pal ? 50 : static_cast<int32_t>(w.menus.listConstants.rate);
    const int32_t half = (rate + static_cast<int32_t>(static_cast<uint32_t>(rate) >> 31)) >> 1;
    if (half == 0) throw std::runtime_error("the list's rate is zero");
    const int32_t left = list.left + direction * kStep;
    list.left = left;
    const int32_t speed = static_cast<int32_t>((int64_t(std::abs(left)) * 2) / half);
    list.speed = speed;
    list.slowing = speed / half;
}

int32_t clampTo(int32_t x, int32_t tail) { return x < 0 ? 0 : std::min(x, tail); }

// func_00230E10: the list's alpha from the page ramp and the menu ramp.
int32_t listAlpha(MenuWorld& w) {
    const int32_t tail = w.clock.tail;
    if (tail == 0) throw std::runtime_error("the tail length is zero");
    const int32_t a = (clampTo(w.menus.page.ramp.counter - (w.menus.body + w.menus.menuLengths[0]), tail) << 7) / tail;
    return static_cast<int32_t>((int64_t(a) * clampTo(tail - w.clock.menuRamp.counter, tail)) / tail);
}

// func_00230C28: the menu ramp's part above the body, out of 128.
int32_t menuAlpha(MenuWorld& w) {
    const int32_t tail = w.clock.tail;
    if (tail == 0) throw std::runtime_error("the tail length is zero");
    return (clampTo(w.clock.menuRamp.counter - w.menus.body, tail) << 7) / tail;
}

// HDD: the cubes' ramp up.
void cubesUp(MenuWorld& w) { Menus::show(w.cubes.ramp); }

void crossfadeStart(ListFade& fade, int32_t selected) {
    fade.secondIndex = selected;
    fade.second = fade.first;
    fade.first = 0;
}

// func_002316B8: the list itself.
void listInput(MenuWorld& w, const MenuExternals& ext, std::vector<std::string>& notes) {
    ConfigPage& page = w.menus.page;
    const uint32_t pressed = ext.pad.pressed;
    const int32_t count = page.count;
    if (pressed & pad::Up) {
        listMove(w, 1);
        crossfadeStart(w.menus.listFade, page.selected);
        const int32_t selected = page.selected - 1;
        page.selected = selected < 0 ? count - 1 : selected;
    } else if (pressed & pad::Down) {
        listMove(w, -1);
        crossfadeStart(w.menus.listFade, page.selected);
        const int32_t selected = page.selected + 1;
        page.selected = selected < count ? selected : 0;
    } else if (pressed & pad::Cross) {
        callback(w, ext, Slot::Enter, notes);
        page.level = 1;
        setGate(w, page.selected == 0 ? -1 : 0);
    } else if (pressed & pad::Square) {
        Menus::show(w.clock.menuRamp);
    } else if (pressed & pad::Circle) {
        Menus::hide(page.ramp);
    } else if (pressed & pad::Triangle) {
        notes.push_back("the Options dialog of System Configuration (triangle) is not modelled");
    }
}

// func_00231B38: inside an entry, confirm or cancel.
void entryInput(MenuWorld& w, const MenuExternals& ext, std::vector<std::string>& notes) {
    ConfigPage& page = w.menus.page;
    const uint32_t pressed = ext.pad.pressed;
    if (pressed & pad::Cross) {
        CubeList& list = w.cubes.list;
        list.pulse = w.menus.listConstants.pulse;
        list.pulsed = (list.position + list.left) / kStep;
        callback(w, ext, Slot::Confirm, notes);
        page.glow = 0;
        page.level = dirty(ext) ? 2 : 0;
        setGate(w, 1);
    } else if (pressed & pad::Circle) {
        callback(w, ext, Slot::Cancel, notes);
        page.level = 0;
        setGate(w, 1);
        reloadItem0(w, ext);
        page.glow = 0;
    }
}

// module_clock_231E48: the page's ramp, its schedule (func_00230FD8) and its input (func_00231C50).
void configPage(MenuWorld& w, const MenuExternals& ext, std::vector<std::string>& notes) {
    ConfigPage& page = w.menus.page;
    Ramp& ramp = page.ramp;
    tickRamp(ramp);
    const int32_t first = w.menus.menuLengths[0], third = w.menus.menuLengths[2], tail = w.clock.tail, body = w.menus.body;
    if (ramp.state == 1) {
        if (ramp.counter == first) cubesUp(w);
    } else if (ramp.state == 3) {
        if (ramp.length - ramp.counter == tail) Menus::hide(w.cubes.ramp);
        if (w.menus.dialogRamp.state == 0) {
            if (ramp.counter == first + body) Menus::show(w.clock.vignetteRamp);
            else if (ramp.counter == first) Menus::hide(w.head.greyRamp);
            else if (ramp.counter == third) appearance(w, false);
        }
    } else if (ramp.state == 2 && page.level == 2) {
        if (!dirty(ext)) page.level = 0;
    }
    const int32_t n = static_cast<int32_t>((int64_t(w.menus.videoMode == 2 ? 50 : 60) * 0x7aa8) / 60);
    const int32_t glow = page.glow + 0x136;
    page.glow = glow < n ? glow : glow - 2 * n;

    ListFade& fade = w.menus.listFade;
    fade.first = std::clamp(fade.first + 8, 0, 128);
    fade.second = std::clamp(fade.second - 8, 0, 128);

    if (ramp.state != 0 && w.clock.menuRamp.state != 2) {
        if (page.level == 1) adjustmentFrame(w, ext);
        else stringCallback(w, ext);
    }

    int32_t& active = w.menus.entryActive;
    if (ramp.state == 1) {
        if (listAlpha(w) != 0 && active == 0) active = 1;
    } else if (ramp.state == 3) {
        if (listAlpha(w) == 0 && active != 0) active = 0;
    } else if (ramp.state == 2) {
        const Ramp& menu = w.clock.menuRamp;
        if (menu.state == 1) {
            if (menuAlpha(w) != 0 && active != 0) active = 0;
        } else if (menu.state == 3) {
            if (menuAlpha(w) == 0 && active == 0) active = 1;
        }
        if (menu.state == 0) {
            if (page.level == 0) listInput(w, ext, notes);
            else if (page.level == 1) entryInput(w, ext, notes);
        }
    }
}

// func_00232020: no other page in the way.
bool nothingElse(const MenuWorld& w) {
    const MenusState& m = w.menus;
    if (m.page.ramp.state != 0 || m.versionRamp.state != 0 || m.dialogRamp.state != 0 || m.firstRunRamp.state != 0) return false;
    return m.pagePointers[4] == 0;
}

// StartSysConfig.
void startSystemConfiguration(MenuWorld& w) {
    Ramp& ramp = w.menus.page.ramp;
    if (ramp.state != 0) return;
    ramp.length = w.menus.menuLengths[0] + w.menus.body + w.clock.tail;
    Menus::show(ramp);
    Menus::hide(w.clock.vignetteRamp);
    Menus::show(w.head.greyRamp);
    appearance(w, true);
}

// func_00232408: the main menu's ramp, its appearing and leaving, its input.
void mainMenu(MenuWorld& w, const MenuExternals& ext, std::vector<std::string>& notes, bool browserEnters) {
    MainMenu& menu = w.menus.mainMenu;
    Ramp& ramp = menu.ramp;
    tickRamp(ramp);
    const int32_t weight = w.clock.overlayLevel, mode = w.clock.mode;
    if (nothingElse(w)) {
        if (ramp.state == 0) {
            if (mode == 2 && weight == 0x80 - w.clock.tail) Menus::show(ramp);
        } else if (ramp.state == 2 && mode == 3 && weight == 0x80) Menus::hide(ramp);
    }
    if (ramp.state != 2 || !nothingElse(w) || w.clock.scene.leaving != 0) return;
    const uint32_t pressed = ext.pad.pressed;
    if (pressed & pad::Up) {
        const int32_t s = menu.selected - 1;
        if (s >= 0) menu.selected = s;
    } else if (pressed & pad::Down) {
        const int32_t s = menu.selected + 1;
        if (s < menu.count) menu.selected = s;
    } else if (pressed & pad::Cross) {
        if (menu.selected == 0 && mode == 0) {
            if (browserEnters) Menus::hide(ramp);
        } else if (menu.selected == 1) startSystemConfiguration(w);
    } else if (pressed & pad::Triangle) {
        notes.push_back("the version page (triangle on the main menu) is not modelled");
    }
}

}

void Menus::show(Ramp& ramp) {
    if (ramp.state == 0) {
        ramp.counter = 0;
        ramp.state = 1;
        ramp.changed = 1;
    }
}

void Menus::hide(Ramp& ramp) {
    if (ramp.state == 2) {
        ramp.changed = 1;
        ramp.state = 3;
        ramp.counter = ramp.length;
    }
}

void Menus::setMode(MenuWorld& w, int32_t mode) {
    Rect& fade = w.head.fade;
    w.clock.mode = mode;
    fade.y1 = w.height << 4;
    fade.x1 = w.width << 4;
    if (mode == 1) {
        w.clock.overlayLevel = 0;
        fade.colour[0] = fade.colour[1] = fade.colour[2] = 0xff;
    } else if (mode == 3) {
        fade.colour[0] = fade.colour[1] = fade.colour[2] = 0;
        w.clock.overlayLevel = 0x80;
        hide(w.clock.vignetteRamp);
    } else if (mode == 2 || mode == 4) {
        fade.colour[0] = fade.colour[1] = fade.colour[2] = 0;
        w.clock.overlayLevel = 0;
    }
}

void Menus::endOfFrame(MenuWorld& w, const MenuExternals& ext) {
    if (ext.configDirty && dirty(ext)) return;
    reloadItem0(w, ext);
}

void Menus::menuStep(MenuWorld& w, const MenuExternals& ext) {
    Ramp& ramp = w.clock.menuRamp;
    Ramp& cubes = w.cubes.ramp;
    tickRamp(ramp);
    if (ramp.state == 1) {
        if (ramp.counter == w.clock.tail) hide(cubes);
    } else if (ramp.state == 2 && (ext.pad.pressed & pad::Square)) {
        hide(ramp);
        show(cubes);
    }
}

void Menus::step(MenuWorld& w, const MenuExternals& ext, std::vector<std::string>& notes) const {
    if (w.menus.versionRamp.state != 0) notes.push_back("versionRamp is not hidden: that page is not modelled");
    if (w.menus.dialogRamp.state != 0) notes.push_back("dialogRamp is not hidden: that page is not modelled");
    if (w.menus.firstRunRamp.state != 0) notes.push_back("firstRunRamp is not hidden: that page is not modelled");
    tickRamp(w.menus.versionRamp);
    configPage(w, ext, notes);
    mainMenu(w, ext, notes, m_options.browserEnters);
    tickRamp(w.menus.firstRunRamp);
    tickRamp(w.menus.dialogRamp);
}

void Menus::between(MenuWorld& w, const MenuExternals& ext, std::vector<std::string>& notes) {
    if (w.clock.scene.leaving != 0 && w.menus.firstRunRamp.state == 0) {
        if (w.clock.mode == 0) setMode(w, 3);
        return;
    }
    if (w.menus.screenCode == ext.disc) {
        const Ramp& ramp = w.menus.mainMenu.ramp;
        if (ramp.state == 3 && ramp.changed != 0) {
            if (w.menus.screenCode != 0x74) w.menus.screenCode = 9999;
            w.clock.scene.leaving = 1;
        }
    } else notes.push_back("the disc state changed: the thread branches for a disc are not modelled");
}

}
