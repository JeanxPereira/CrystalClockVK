#include <cstdio>
#include <cstring>
#include <filesystem>

#include "../../Check.hpp"
#include "../SceneFixture.hpp"
#include "OpeningFixture.hpp"
#include "scene/Arithmetic.hpp"
#include "scene/opening/Fog.hpp"

namespace {

using scene::EeArithmetic;
using scene::Mat4;
using scene::NativeArithmetic;

uint32_t word(const openingtest::Bytes& bytes, size_t at) {
    uint32_t v;
    std::memcpy(&v, bytes.data() + at, 4);
    return v;
}

Mat4 matrixOf(const openingtest::Bytes& bytes, size_t at) {
    Mat4 m{};
    for (size_t r = 0; r < 4; ++r)
        for (size_t c = 0; c < 4; ++c) m[r][c] = scene::asFloat(word(bytes, at + (r * 4 + c) * 4));
    return m;
}

int run(const std::string& path) {
    const openingtest::OpeningFixture fixture = openingtest::OpeningFixture::load(path);
    scene::opening::Fog<EeArithmetic> fog;
    scene::opening::Fog<NativeArithmetic> native;

    size_t frames = 0, layers = 0, meshValues = 0, offsetsEqual = 0, offsetsTotal = 0;
    int64_t drawn = 0, leftOut = 0;
    bool first = true;
    std::array<float, 6> previous{};
    for (size_t k = 0; k < fixture.frameCount(); ++k) {
        const auto block = fixture.fog(k);
        if (!block) continue;
        const openingtest::Probe& record = block->record;
        CHECK(record.mem.size() == 7 && record.mem[0].size() == 0x2440);
        const openingtest::Bytes& memory = record.mem[0];

        for (size_t n = 0; n < fog.mesh().size(); ++n) {
            for (size_t c = 0; c < 4; ++c) {
                CHECK(scene::floatBits(fog.mesh()[n][c]) == word(memory, 0x20 + n * 16 + c * 4));
                CHECK(static_cast<uint32_t>(fog.brightness()[n][c]) == word(memory, 0x1230 + n * 16 + c * 4));
                meshValues += 2;
            }
        }
        if (first) {
            for (size_t layer = 0; layer < 6; ++layer) CHECK(word(memory, layer * 4) == 0);
            first = false;
        } else {
            for (size_t layer = 0; layer < 6; ++layer) {
                ++offsetsTotal;
                if (scene::floatBits(previous[layer]) == word(memory, layer * 4)) ++offsetsEqual;
            }
        }
        const Mat4 toScreen = matrixOf(record.mem[5], 0);
        std::vector<scene::Pass> passes;
        fog.draw(toScreen, passes);
        CHECK(fog.stats().drawn + fog.stats().leftOut == 6 * 256);
        size_t vertices = 0;
        for (const scene::Pass& p : passes) vertices += p.vertices.size();
        CHECK(vertices == static_cast<size_t>(fog.stats().drawn) * 6);
        CHECK(passes.size() <= 6);
        layers += 6;
        drawn += fog.stats().drawn;
        leftOut += fog.stats().leftOut;
        previous = fog.offsets();

        std::vector<scene::Pass> nativePasses;
        native.draw(toScreen, nativePasses);
        CHECK(native.stats().drawn > 0 || fog.stats().drawn == 0);
        ++frames;
    }
    std::printf("fog: %zu frames, %zu layers, mesh+brightness %zu values equal, offsets carried %zu of %zu, quads drawn %lld, left out %lld\n", frames, layers, meshValues,
                offsetsEqual, offsetsTotal, static_cast<long long>(drawn), static_cast<long long>(leftOut));
    CHECK(frames == 246);
    CHECK(offsetsEqual == offsetsTotal && offsetsTotal == 1470);
    CHECK(drawn == 127136 && leftOut == 250720);
    return 0;
}

}

int main(int argc, char** argv) {
    return scenetest::run(argc, argv, [](int count, char** arguments) {
        if (count < 2) {
            std::fprintf(stderr, "usage: FogTest <opening-full opening.json>\n");
            return 2;
        }
        if (!std::filesystem::exists(arguments[1])) {
            std::fprintf(stderr, "no fixture at %s\n", arguments[1]);
            return 2;
        }
        return run(arguments[1]);
    });
}
