#include "Check.hpp"
#include "parity/Fixture.hpp"
#include <algorithm>
#include <set>

int main(int argc, char** argv) {
    CHECK(argc == 2);
    const parity::Fixture fixture = parity::loadFixture(argv[1]);
    const auto& frame = fixture.frame;

    CHECK(frame.passes.size() == 188);
    CHECK(fixture.oracle.size() == 188);
    CHECK(frame.targets.size() == 3);
    for (const auto& target : frame.targets) {
        CHECK(target.width == 640 && target.height == 224);
        CHECK(fixture.targetStart.at(target.id).rgba.size() == 640u * 224u * 4u);
    }

    const parity::GsPass& copy = frame.passes[2];
    CHECK(copy.primitive == parity::GsPrimitive::Sprites);
    CHECK(copy.target == "fb1a40");
    CHECK(copy.texture && copy.texture->sourceIsTarget && copy.texture->source == "fb0000");
    CHECK(copy.texture->alpha.constant && copy.texture->alpha.value == 127 && copy.texture->alpha.zeroWhenBlack);
    CHECK(copy.texture->addressU.mode == parity::GsAddressMode::RegionClamp && copy.texture->addressU.max == 639);
    CHECK(copy.texture->filter == parity::GsFilter::Bilinear);
    CHECK(!copy.blend);
    CHECK(copy.vertices.size() == 2 && copy.vertices[1].x == 640.0f && copy.vertices[1].y == 224.0f);

    CHECK(std::count_if(frame.passes.begin(), frame.passes.end(), [](const parity::GsPass& p) { return !p.skip.empty(); }) == 26);

    const parity::Image afterCopy = fixture.oracleColour(2);
    CHECK(afterCopy.width == 640 && afterCopy.height == 224);
    std::set<uint8_t> alphas;
    for (size_t i = 3; i < afterCopy.rgba.size(); i += 4) alphas.insert(afterCopy.rgba[i]);
    for (uint8_t a : alphas) CHECK(a == 0 || a == 127);

    const parity::DepthImage depth = fixture.oracleDepth(1);
    CHECK(depth.depth.size() == 640u * 224u);
    const uint32_t deepest = *std::max_element(depth.depth.begin(), depth.depth.end());
    CHECK(deepest > 0 && deepest < (1u << 24));
    return 0;
}
