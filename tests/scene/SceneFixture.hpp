#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "scene/Matrix.hpp"

namespace scenetest {

// Runs a test body; an exception is a failure with its message printed.
int run(int argc, char** argv, const std::function<int(int, char**)>& body);

nlohmann::json loadScene(const std::string& path);

uint32_t hexBits(const nlohmann::json& hex);
float hexFloat(const nlohmann::json& hex);
std::string bitsText(uint32_t bits);

bool sameBits(float value, uint32_t expected, std::string_view what);
bool sameBits(float value, const nlohmann::json& hex, std::string_view what);
bool sameBits(const scene::Vec4& value, const nlohmann::json& hex, std::string_view what);
bool sameBits(const scene::Mat4& value, const nlohmann::json& hex, std::string_view what);

scene::Vec4 hexVec4(const nlohmann::json& hex);
scene::Mat4 hexMat4(const nlohmann::json& hex);

uint32_t fnv(const float* values, size_t count);

}
