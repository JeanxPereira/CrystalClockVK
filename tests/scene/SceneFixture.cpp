#include "SceneFixture.hpp"

#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace scenetest {

nlohmann::json loadScene(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    return nlohmann::json::parse(in);
}

uint32_t hexBits(const nlohmann::json& hex) {
    const std::string text = hex.get<std::string>();
    if (text.size() != 10 || text[0] != '0' || text[1] != 'x') throw std::runtime_error("not a float pattern: " + text);
    return static_cast<uint32_t>(std::stoul(text.substr(2), nullptr, 16));
}

float hexFloat(const nlohmann::json& hex) { return scene::asFloat(hexBits(hex)); }

std::string bitsText(uint32_t bits) {
    char text[11];
    std::snprintf(text, sizeof text, "0x%08x", bits);
    return text;
}

bool sameBits(float value, uint32_t expected, std::string_view what) {
    const uint32_t got = scene::floatBits(value);
    if (got == expected) return true;
    std::fprintf(stderr, "%.*s: got %s, expected %s\n", static_cast<int>(what.size()), what.data(), bitsText(got).c_str(), bitsText(expected).c_str());
    return false;
}

bool sameBits(float value, const nlohmann::json& hex, std::string_view what) { return sameBits(value, hexBits(hex), what); }

bool sameBits(const scene::Vec4& value, const nlohmann::json& hex, std::string_view what) {
    bool same = hex.size() == 4;
    for (size_t i = 0; same && i < 4; ++i) same = sameBits(value[i], hex[i], std::string(what) + "[" + std::to_string(i) + "]");
    return same;
}

bool sameBits(const scene::Mat4& value, const nlohmann::json& hex, std::string_view what) {
    bool same = hex.size() == 4;
    for (size_t r = 0; same && r < 4; ++r) same = sameBits(value[r], hex[r], std::string(what) + "[" + std::to_string(r) + "]");
    return same;
}

scene::Vec4 hexVec4(const nlohmann::json& hex) { return {hexFloat(hex[0]), hexFloat(hex[1]), hexFloat(hex[2]), hexFloat(hex[3])}; }

scene::Mat4 hexMat4(const nlohmann::json& hex) { return {hexVec4(hex[0]), hexVec4(hex[1]), hexVec4(hex[2]), hexVec4(hex[3])}; }

uint32_t fnv(const float* values, size_t count) {
    uint32_t h = 0x811c9dc5u;
    for (size_t i = 0; i < count; ++i) {
        uint32_t u = scene::floatBits(values[i]);
        for (int k = 0; k < 4; ++k) {
            h ^= u & 0xffu;
            h *= 0x01000193u;
            u >>= 8;
        }
    }
    return h;
}

}
