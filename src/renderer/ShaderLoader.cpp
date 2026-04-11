#include "ShaderLoader.hpp"
#include <fstream>
#include <stdexcept>

std::vector<uint32_t> ShaderLoader::loadSpirv(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + path.string());
    }

    auto fileSize = static_cast<size_t>(file.tellg());
    if (fileSize == 0 || fileSize % sizeof(uint32_t) != 0) {
        throw std::runtime_error("Invalid SPIR-V file size: " + path.string());
    }

    std::vector<uint32_t> spirv(fileSize / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(spirv.data()), static_cast<std::streamsize>(fileSize));

    return spirv;
}

VkShaderModule ShaderLoader::createModule(VkDevice device, const std::vector<uint32_t>& spirv) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirv.size() * sizeof(uint32_t);
    createInfo.pCode = spirv.data();

    VkShaderModule module;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module");
    }

    return module;
}

VkShaderModule ShaderLoader::loadModule(VkDevice device, const std::filesystem::path& path) {
    auto spirv = loadSpirv(path);
    return createModule(device, spirv);
}
