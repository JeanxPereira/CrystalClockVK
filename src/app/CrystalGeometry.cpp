#include "CrystalGeometry.hpp"
#include <glm/gtc/constants.hpp>
#include <array>

std::vector<CrystalVertex> CrystalGeometry::generateRodMesh(
    int sides, float height, float radius, float bevelWidth, float bevelCut) {

    std::vector<CrystalVertex> vertices;
    vertices.reserve(static_cast<size_t>(sides) * 18);

    float half = height * 0.5f;
    float innerRadius = radius - bevelWidth;
    float angleStep = glm::two_pi<float>() / static_cast<float>(sides);
    float angleOffset = glm::pi<float>() / static_cast<float>(sides);

    // Pre-compute corner positions (matching OSDSYS_Clock.cpp buildHexPrism)
    std::vector<glm::vec3> topOuter(sides), botOuter(sides);
    std::vector<glm::vec3> topInner(sides), botInner(sides);

    for (int i = 0; i < sides; i++) {
        float a = static_cast<float>(i) * angleStep + angleOffset;
        float ca = std::cos(a);
        float sa = std::sin(a);

        topOuter[i] = {ca * radius,       half - bevelCut,  sa * radius};
        botOuter[i] = {ca * radius,      -half + bevelCut,  sa * radius};
        topInner[i] = {ca * innerRadius,  half,             sa * innerRadius};
        botInner[i] = {ca * innerRadius, -half,             sa * innerRadius};
    }

    auto addTri = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 n) {
        float u0 = 0.5f, u1 = 0.5f;
        vertices.push_back({a, n, {u0, 0.5f}});
        vertices.push_back({b, n, {u1, 0.0f}});
        vertices.push_back({c, n, {u0, 1.0f}});
    };

    auto addQuad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n) {
        float u0 = 0.0f, u1 = 1.0f;
        vertices.push_back({a, n, {u0, 0.0f}});
        vertices.push_back({b, n, {u1, 0.0f}});
        vertices.push_back({c, n, {u1, 1.0f}});

        vertices.push_back({a, n, {u0, 0.0f}});
        vertices.push_back({c, n, {u1, 1.0f}});
        vertices.push_back({d, n, {u0, 1.0f}});
    };

    for (int i = 0; i < sides; i++) {
        int j = (i + 1) % sides;

        // Top cap triangles
        addTri(topInner[j], topInner[i], {0, half, 0}, {0, 1, 0});

        // Bottom cap triangles
        addTri(botInner[i], botInner[j], {0, -half, 0}, {0, -1, 0});

        // Top bevel quads
        glm::vec3 topBevelN = glm::normalize(
            glm::cross(topInner[j] - topInner[i], topOuter[i] - topInner[i]));
        addQuad(topInner[i], topInner[j], topOuter[j], topOuter[i], topBevelN);

        // Bottom bevel quads
        glm::vec3 botBevelN = glm::normalize(
            glm::cross(botOuter[i] - botOuter[j], botInner[j] - botOuter[j]));
        addQuad(botInner[j], botInner[i], botOuter[i], botOuter[j], botBevelN);

        // Main body side quads
        glm::vec3 sideN = glm::normalize(glm::vec3(
            topOuter[i].x + topOuter[j].x, 0.0f, topOuter[i].z + topOuter[j].z));
        addQuad(topOuter[i], topOuter[j], botOuter[j], botOuter[i], sideN);
    }

    return vertices;
}

VkVertexInputBindingDescription CrystalGeometry::getBindingDescription() {
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(CrystalVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return binding;
}

std::array<VkVertexInputAttributeDescription, 3> CrystalGeometry::getAttributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 3> attrs{};

    attrs[0].binding = 0;
    attrs[0].location = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = offsetof(CrystalVertex, position);

    attrs[1].binding = 0;
    attrs[1].location = 1;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = offsetof(CrystalVertex, normal);

    attrs[2].binding = 0;
    attrs[2].location = 2;
    attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[2].offset = offsetof(CrystalVertex, uv);

    return attrs;
}
