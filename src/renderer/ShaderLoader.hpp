#pragma once

#include <vulkan/vulkan.h>
#include <filesystem>
#include <vector>
#include <cstdint>

// Loads SPIR-V bytecode from disk and creates VkShaderModules.
// All modules are owned by the caller and must be destroyed after pipeline creation.
class ShaderLoader {
public:
    // Load raw SPIR-V bytecode from a .spv file
    static std::vector<uint32_t> loadSpirv(const std::filesystem::path& path);

    // Create a VkShaderModule from SPIR-V bytecode
    static VkShaderModule createModule(VkDevice device, const std::vector<uint32_t>& spirv);

    // Convenience: load + create in one call
    static VkShaderModule loadModule(VkDevice device, const std::filesystem::path& path);
};
