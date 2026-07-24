#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <cmath>

struct CrystalVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

// Generates PS2-accurate hexagonal prism geometry.
// Also generates a cylinder mesh for the tunnel background.
class CrystalGeometry {
public:
    // Crystal rod mesh — matches Raylib's GenCrystalRodMesh dimensions
    static std::vector<CrystalVertex> generateRodMesh(
        int sides = 6,
        float baseRadius = 0.866025f,   // cos(30°) — hexagon inscribed
        float topRadius = 0.766026f,    // tapered top
        float height = 10.0f,
        float bevelStart = 9.85f);      // where taper begins

    // Tunnel cylinder mesh — matches Raylib's GenMeshCylinder(20, 100, 30)
    static std::vector<CrystalVertex> generateCylinderMesh(
        float radius = 20.0f,
        float length = 100.0f,
        int slices = 30);

    static std::vector<CrystalVertex> generateCubeMesh(float size = 1.0f);

    static VkVertexInputBindingDescription getBindingDescription();
    static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions();
};
