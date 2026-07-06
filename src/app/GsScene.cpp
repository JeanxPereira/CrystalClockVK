#include "app/GsScene.hpp"

#include <cmath>
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

int GsScene::spinDial(float radians, float localizedMaxPx) {
    // A draw belongs to the dial group when it is a rod (TRI_STRIP=4) or the
    // swirl sphere (LINE_STRIP=2) AND its screen footprint is localized (not a
    // fullscreen tunnel/background quad). The fullscreen tunnel and the text
    // bars stay put; only the rotating crystal geometry moves.
    auto localized = [&](const GsPrimitive& p, float& cx, float& cy) {
        if (p.verts.empty()) return false;
        if (p.prim.type != 4 && p.prim.type != 2) return false;
        float x0 = p.verts[0].x, x1 = x0, y0 = p.verts[0].y, y1 = y0;
        for (const auto& v : p.verts) {
            x0 = std::min(x0, v.x); x1 = std::max(x1, v.x);
            y0 = std::min(y0, v.y); y1 = std::max(y1, v.y);
        }
        const float w = x1 - x0, h = y1 - y0;
        if (std::max(w, h) > localizedMaxPx) return false;  // fullscreen -> skip
        cx = 0.5f * (x0 + x1); cy = 0.5f * (y0 + y1);
        return true;
    };

    // Dial center = centroid of the localized-draw centers (the rods radiate
    // from it; the swirl sits on it).
    double sumX = 0, sumY = 0;
    int groupDraws = 0;
    for (const auto& p : m_stream.prims) {
        float cx, cy;
        if (localized(p, cx, cy)) { sumX += cx; sumY += cy; groupDraws++; }
    }
    if (groupDraws == 0) return 0;
    const float ctrX = static_cast<float>(sumX / groupDraws);
    const float ctrY = static_cast<float>(sumY / groupDraws);

    const float cs = std::cos(radians), sn = std::sin(radians);
    int rotated = 0;
    for (auto& p : m_stream.prims) {
        float cx, cy;
        if (!localized(p, cx, cy)) continue;
        for (auto& v : p.verts) {
            const float dx = v.x - ctrX, dy = v.y - ctrY;
            v.x = ctrX + dx * cs - dy * sn;
            v.y = ctrY + dx * sn + dy * cs;
        }
        rotated++;
    }
    std::fprintf(stderr, "GsScene: spinDial %.3f rad, center (%.1f,%.1f), rotated %d/%d draws\n",
                 radians, ctrX, ctrY, rotated, m_stats.draws);
    std::fflush(stderr);
    return rotated;
}
