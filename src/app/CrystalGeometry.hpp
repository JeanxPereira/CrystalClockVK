#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <cmath>

// Crystal rod vertex format
struct CrystalVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

// Generates the hexagonal prism mesh for a single crystal rod.
// Matches the OSDSYS geometry: 6-sided prism with beveled tip.
//
// The mesh is centered at origin, extending from Y=0 to Y=height.
// Bevel starts at Y = height - tipLength, tapering radius from 'radius' to 'tipRadius'.
//
// Returns triangulated vertex list (no index buffer needed for small geometry).
class CrystalGeometry {
public:
    // Default values from OSDSYS_Clock.cpp reference (PS2 crystal proportions)
    // Mesh is centered on Y=0 (extends from -height/2 to +height/2)
    static std::vector<CrystalVertex> generateRodMesh(
        int sides = 6,
        float height = 1.85f,
        float radius = 0.215f,
        float bevelWidth = 0.035f,
        float bevelCut = 0.025f);

    // Vertex input description for Vulkan pipeline
    static VkVertexInputBindingDescription getBindingDescription();
    static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions();
};
