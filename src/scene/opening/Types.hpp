#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace scene::opening {

// facts/opening.md section 5.2: the play history, 21 entries of 22 bytes (a name, a count, a mask of the entry's cells, the main cell).
struct HistoryEntry {
    std::array<char, 16> name{};
    uint8_t count = 0, mask = 0, mainCell = 0;
    bool operator==(const HistoryEntry&) const = default;
};
using History = std::array<HistoryEntry, 21>;

// The disc state at module counter 1 and from counter 2 on (no disc: 0x65, then 0x64).
struct DiscSchedule {
    uint32_t first = 0x65, later = 0x64;
    bool operator==(const DiscSchedule&) const = default;
};

// The intro is a closed system fed by these (the design's input table).
struct BootOptions {
    DiscSchedule disc;
    uint32_t lightsPhase = 0xD80;
    std::optional<History> history;
    bool clockForced = false, hddReady = false;
    int32_t hddExec = 0;
    bool pal = false;
};

// facts/opening.md section 3: the sound commands the scene reports; the app drops them.
struct SoundEvent {
    uint32_t id = 0;
    int32_t argument = 0, counter = 0;
    bool operator==(const SoundEvent&) const = default;
};

// facts/opening.md section 8: the module the hand-off starts, the execute-app type, whether the previous module was the opening.
struct HandOff {
    int32_t module = 2, executeAppType = -1;
    bool previousWasOpening = true;
    bool operator==(const HandOff&) const = default;
};

}
