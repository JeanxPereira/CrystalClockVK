#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "gs/GsCommandStream.hpp"

// Parses a DECOMPRESSED PCSX2 .gs dump into a GsCommandStream.
// Format grounded in pcsx2-ref GSDump.cpp::AddHeader (writer) + GSLzma.cpp (reader).
// zstd decompression is out of scope: feed an already-decompressed buffer.
class GsDumpParser {
public:
    struct ParseError : std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    // Parses the whole dump. Throws ParseError on a malformed stream.
    static GsCommandStream parse(const uint8_t* data, size_t size);
};
