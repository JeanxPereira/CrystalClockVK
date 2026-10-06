#pragma once
#include <cstdint>
#include <vector>

namespace scene {

struct SoundCommand {
    uint16_t id = 0, a1 = 0, a2 = 0, a3 = 0;
    bool carriedA2 = false, carriedA3 = false;
    bool operator==(const SoundCommand&) const = default;
};

// The a3 a sender does not set is a register the facts record and never predict: a value a capture measured at the site where there is one, else this placeholder.
constexpr uint16_t kPlaceholderA3 = 0;

// module_clock_thread_proc 0x00225D30, jal 0x00225D50.
inline void clockThreadStart(std::vector<SoundCommand>& out) { out.push_back({0x6150, 1, 0, 0}); }

// func_002324C8 (jal 0x00225E48 from module_clock_init_resources): queued when the word 0x002AD22C is nonzero; the second a2 and a3 are the ring temporaries.
inline void clockSetUp(std::vector<SoundCommand>& out) {
    out.push_back({0x6150, 6, 0, 0xF});
    out.push_back({0x6140, 2, 0, 0, true, true});
}

}
