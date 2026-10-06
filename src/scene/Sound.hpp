#pragma once
#include <cstdint>

namespace scene {

struct SoundCommand {
    uint16_t id = 0, a1 = 0, a2 = 0, a3 = 0;
    bool carriedA2 = false, carriedA3 = false;
    bool operator==(const SoundCommand&) const = default;
};

}
