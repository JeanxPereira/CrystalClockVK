#pragma once
#include <vector>

#include "assets/Bytes.hpp"

namespace assets {

// The loadable segments of an EE ELF, read by EE address.
class ElfImage {
public:
    explicit ElfImage(View elf);
    View bytes(uint32_t address, uint32_t length) const;

private:
    struct Segment {
        uint32_t offset, address, size;
    };
    View m_elf;
    std::vector<Segment> m_segments;
};

}
