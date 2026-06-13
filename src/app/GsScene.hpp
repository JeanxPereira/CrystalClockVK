#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gs/GsCommandStream.hpp"
#include "gsvk/GsDrawRecipe.hpp"

// Loads a PCSX2 .gs dump into the app and runs it through the gsvk translator,
// producing the per-draw recipes the renderer will consume. The home for the
// scene's render data as W4 grows (vertex buffers, textures attach here later).
struct GsSceneStats {
    bool loaded = false;
    std::string source;
    uint32_t draws = 0;
    uint32_t verts = 0;
    uint32_t blendModes = 0;  // distinct enabled blend recipes
    uint32_t triStrip = 0, sprite = 0, lineStrip = 0, other = 0;
};

class GsScene {
public:
    // Parse a DECOMPRESSED .gs dump and assemble gsvk recipes. Returns false on
    // read/parse failure (stats().loaded stays false).
    bool load(const std::string& dumpPath);

    const GsCommandStream& stream() const { return m_stream; }
    const std::vector<gsvk::GsDrawRecipe>& recipes() const { return m_recipes; }
    const GsSceneStats& stats() const { return m_stats; }

private:
    GsCommandStream m_stream;
    std::vector<gsvk::GsDrawRecipe> m_recipes;
    GsSceneStats m_stats;
};
