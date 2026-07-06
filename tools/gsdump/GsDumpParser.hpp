#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

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

    // Decodes raw GIF packet payload bytes (a sequence of GIFtags + register
    // data, as found in dump TRANSFER packets) into `stream`, continuing from
    // stream's current register state (stream.decodeState). Used by the SP1
    // interpreter capture to decode reconstructed GIF packets that never went
    // through a .gs dump.
    static void decodeGifData(GsCommandStream& stream, const uint8_t* data, size_t size);

    // Writes the decoded draws as JSON in the schema used by `gsdump --json`
    // and consumed by tools/vdiff/vdiff.mjs: a bare array of draws, each
    // {idx, PRIM, ALPHA, TEST, TEX0, CLAMP, FRAME, SCISSOR, ..., verts:[...]}.
    // Shared so both `gsdump --json` and `eerun --decode --json` produce
    // byte-for-byte comparable output.
    static void writeJson(const GsCommandStream& stream, const std::string& path);
};
