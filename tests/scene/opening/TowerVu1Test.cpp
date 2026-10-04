#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "../../Check.hpp"
#include "../SceneFixture.hpp"
#include "OpeningFixture.hpp"
#include "TowerCalls.hpp"
#include "scene/Arithmetic.hpp"
#include "scene/opening/TowerVu1.hpp"

namespace {

using namespace scene::opening;
using scene::EeArithmetic;
using scene::NativeArithmetic;
using Row = std::array<float, 10>;

// The strips as triangles the way the GS draws them: vertex i of a strip completes a triangle when it is kicked.
void expand(const TowerVertices& v, std::vector<Row>& out) {
    for (int face = 0; face < 6; ++face)
        for (int i = 2; i < 4; ++i) {
            if (!v.kicked[face * 4 + i]) continue;
            for (int k = i - 2; k <= i; ++k) {
                const scene::Vertex& p = v.vertices[face * 4 + k];
                out.push_back({p.x, p.y, static_cast<float>(p.z), float(p.r), float(p.g), float(p.b), float(p.a), p.u, p.v, p.q});
            }
        }
}

struct Tally {
    size_t calls = 0, chains = 0, vertices = 0, hidden = 0, frames = 0, triangles = 0;
};

int chainsOf(const std::string& openingPath, const std::string& passesPath, long expectedHidden, Tally& tally) {
    const openingtest::OpeningFixture fixture = openingtest::OpeningFixture::load(openingPath);
    const std::vector<openingtest::TowerCall> calls = openingtest::towerCalls(fixture);
    CHECK(!calls.empty());
    std::vector<std::vector<Row>> expansions;
    size_t hidden = 0, vertices = 0;
    for (const openingtest::TowerCall& call : calls) {
        std::vector<Row> rows;
        for (const openingtest::Bytes& bytes : call.chains) {
            const TowerChain chain = openingtest::chainOf(bytes);
            const TowerVertices ee = TowerVu1<EeArithmetic>::run(chain);
            CHECK(ee.alpha == 0x8000000044ull);
            for (bool kicked : ee.kicked) {
                ++vertices;
                if (!kicked) ++hidden;
            }
            expand(ee, rows);
            ++tally.chains;
        }
        expansions.push_back(std::move(rows));
        ++tally.calls;
    }
    tally.vertices += vertices;
    tally.hidden += hidden;
    std::printf("%s: %zu calls, %zu vertices, %zu not kicked\n", fixture.capture().c_str(), calls.size(), vertices, hidden);
    if (expectedHidden >= 0) CHECK(static_cast<long>(hidden) == expectedHidden);

    if (passesPath == "-") return 0;
    std::ifstream in(passesPath, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "no passes at %s\n", passesPath.c_str());
        return 1;
    }
    const nlohmann::json passes = nlohmann::json::parse(in);
    size_t next = 0;
    for (const nlohmann::json& frame : passes.at("frames")) {
        if (frame.is_null()) continue;
        for (const nlohmann::json& pass : frame.at("passes")) {
            if (pass.at("primitive") != "Triangles" || pass.at("texture").is_null() || pass.at("texture").at("source").value("image", "") != "t2d40-4-2-8x8") continue;
            const nlohmann::json& list = pass.at("vertices");
            bool found = false;
            for (size_t c = next; c < expansions.size() && !found; ++c) {
                const std::vector<Row>& rows = expansions[c];
                if (rows.size() != list.size()) continue;
                bool same = true;
                for (size_t n = 0; n < rows.size() && same; ++n) {
                    const nlohmann::json& v = list.at(n);
                    const float x = v.at(0), y = static_cast<float>(v.at(1).get<double>() + 0.5 * frame.at("field").get<int>()), z = v.at(2);
                    same = x == rows[n][0] && y == rows[n][1] && z == rows[n][2];
                    for (size_t k = 3; k < 10 && same; ++k) same = static_cast<float>(v.at(k).get<double>()) == rows[n][k];
                }
                if (same) {
                    found = true;
                    next = c + 1;
                    tally.triangles += rows.size() / 3;
                }
            }
            if (!found) {
                std::fprintf(stderr, "%s: dump frame %d draw %d: %zu vertices equal no call from %zu\n", fixture.capture().c_str(), frame.at("frame").get<int>(),
                             pass.at("index").get<int>(), list.size(), next);
                for (size_t c = next; c < expansions.size(); ++c) {
                    if (expansions[c].size() != list.size()) continue;
                    size_t shown = 0;
                    for (size_t n = 0; n < list.size() && shown < 8; ++n)
                        for (size_t k = 0; k < 10; ++k)
                            if (static_cast<float>(list.at(n).at(k).get<double>()) != expansions[c][n][k]) {
                                ++shown;
                                std::fprintf(stderr, "  call %zu vertex %zu field %zu: dump %.9g, computed %.9g\n", c, n, k, list.at(n).at(k).get<double>(), expansions[c][n][k]);
                            }
                    break;
                }
                return 1;
            }
            ++tally.frames;
        }
    }
    return 0;
}

int entryTwentyOne(const std::string& openingPath) {
    const openingtest::OpeningFixture fixture = openingtest::OpeningFixture::load(openingPath);
    const std::vector<openingtest::TowerCall> calls = openingtest::towerCalls(fixture);
    CHECK(!calls.empty() && !calls.front().chains.empty());
    TowerChain chain = openingtest::chainOf(calls.front().chains.front());
    const TowerVertices zero = TowerVu1<NativeArithmetic>::run(chain);
    CHECK(zero.alpha == 0x8000000044ull);
    chain.bytes[0x2a5040 - kTowerChainAddress] = 1;
    bool refused = false;
    try {
        TowerVu1<NativeArithmetic>::run(chain);
    } catch (const std::exception&) {
        refused = true;
    }
    CHECK(refused);
    return 0;
}

}

int main(int argc, char** argv) {
    return scenetest::run(argc, argv, [](int count, char** arguments) {
        if (count < 3) {
            std::fprintf(stderr, "usage: TowerVu1Test (<opening.json> <passes.json> [--hidden=N])...\n");
            return 2;
        }
        Tally tally;
        bool twentyOne = false;
        for (int i = 1; i + 1 < count;) {
            long hidden = -1;
            const std::string opening = arguments[i], passes = arguments[i + 1];
            i += 2;
            if (i < count && std::strncmp(arguments[i], "--hidden=", 9) == 0) hidden = std::atol(arguments[i++] + 9);
            if (!std::filesystem::exists(opening) || (passes != "-" && !std::filesystem::exists(passes))) {
                std::fprintf(stderr, "missing %s or %s\n", opening.c_str(), passes.c_str());
                return 1;
            }
            if (int failed = chainsOf(opening, passes, hidden, tally)) return failed;
            if (!twentyOne) {
                if (int failed = entryTwentyOne(opening)) return failed;
                twentyOne = true;
            }
        }
        CHECK(tally.frames > 0);
        std::printf("towervu1: %zu calls, %zu chains, %zu vertices (%zu not kicked), %zu dump frames, %zu triangles equal\n", tally.calls, tally.chains, tally.vertices,
                    tally.hidden, tally.frames, tally.triangles);
        return 0;
    });
}
