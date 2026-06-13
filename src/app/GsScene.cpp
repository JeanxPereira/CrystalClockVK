#include "app/GsScene.hpp"

#include <cstdio>
#include <fstream>
#include <set>
#include <tuple>

#include "GsDumpParser.hpp"

bool GsScene::load(const std::string& dumpPath) {
    std::ifstream in(dumpPath, std::ios::binary | std::ios::ate);
    if (!in) {
        std::fprintf(stderr, "GsScene: cannot open %s\n", dumpPath.c_str());
        return false;
    }
    const std::streamsize n = in.tellg();
    in.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(n));
    in.read(reinterpret_cast<char*>(buf.data()), n);

    try {
        m_stream = GsDumpParser::parse(buf.data(), buf.size());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "GsScene: parse error: %s\n", e.what());
        return false;
    }

    m_recipes.clear();
    m_recipes.reserve(m_stream.prims.size());
    std::set<std::tuple<int, int, int>> blends;  // (src,dst,op) of enabled blends
    for (const auto& p : m_stream.prims) {
        gsvk::GsDrawRecipe r = gsvk::assembleRecipe(p);
        if (r.blend.enable)
            blends.insert({r.blend.srcFactor, r.blend.dstFactor, r.blend.colorOp});
        switch (p.prim.type) {
            case 4: m_stats.triStrip++; break;
            case 6: m_stats.sprite++; break;
            case 2: m_stats.lineStrip++; break;
            default: m_stats.other++; break;
        }
        m_stats.verts += static_cast<uint32_t>(p.verts.size());
        m_recipes.push_back(r);
    }

    m_stats.loaded = true;
    m_stats.source = dumpPath;
    m_stats.draws = static_cast<uint32_t>(m_stream.prims.size());
    m_stats.blendModes = static_cast<uint32_t>(blends.size());

    std::fprintf(stderr, "GsScene: %u draws, %u verts, %u blend modes from %s\n",
                 m_stats.draws, m_stats.verts, m_stats.blendModes, dumpPath.c_str());
    std::fflush(stderr);
    return true;
}
