#pragma once
#include "scene/FrameDescription.hpp"
#include <filesystem>
#include <map>

namespace parity {
struct Image { uint32_t width{0}, height{0}; std::vector<uint8_t> rgba; };
struct DepthImage { uint32_t width{0}, height{0}; std::vector<uint32_t> depth; };
struct OracleStep { std::string colour, depth; };
struct Fixture {
    std::filesystem::path root;
    scene::FrameDescription frame;
    std::map<std::string, Image> targetStart, textures;
    std::vector<OracleStep> oracle;
    std::string depthStart;
    Image oracleColour(size_t pass) const;
    DepthImage oracleDepth(size_t pass) const;
    DepthImage startDepth() const;
};
Fixture loadFixture(const std::filesystem::path& directory);
Image loadPngPair(const std::filesystem::path& prefix);
}
