#include "CrystalGeometry.hpp"
#include <glm/gtc/constants.hpp>
#include <array>

// ──────────────────────────────────────────────────────────────────────────
// Crystal Rod Mesh — ported 1:1 from Raylib's GenCrystalRodMesh
// Generates a hexagonal prism with a tapered bevel at the top.
// Layout: 3 vertex rings (base, bevel, top) + 2 center caps.
// ──────────────────────────────────────────────────────────────────────────
std::vector<CrystalVertex> CrystalGeometry::generateRodMesh(
    int sides, float baseRadius, float topRadius, float height, float bevelStart) {

    int indexedVertCount = sides * 3 + 2;
    int triCount = sides * 6;
    int indexCount = triCount * 3;

    std::vector<glm::vec3> tV(indexedVertCount);
    std::vector<glm::vec3> tN(indexedVertCount);
    std::vector<glm::vec2> tT(indexedVertCount);
    std::vector<uint16_t> tI(indexCount);

    float angleStep = glm::two_pi<float>() / static_cast<float>(sides);
    int vi = 0, ni = 0, ti = 0;

    for (int ring = 0; ring < 3; ring++) {
        float y, radius;
        if (ring == 0)      { y = 0.0f;       radius = baseRadius; }
        else if (ring == 1) { y = bevelStart;  radius = baseRadius; }
        else                { y = height;      radius = topRadius;  }

        for (int s = 0; s < sides; s++) {
            float a = s * angleStep - glm::half_pi<float>();
            float x = radius * std::cos(a);
            float z = radius * std::sin(a);

            tV[vi] = {x, y, z};

            float nx = std::cos(a);
            float nz = std::sin(a);
            if (ring == 2) {
                float bevelAngle = std::atan2(baseRadius - topRadius, height - bevelStart);
                float ny = std::sin(bevelAngle);
                float horiz = std::cos(bevelAngle);
                tN[ni] = {nx * horiz, ny, nz * horiz};
            } else {
                tN[ni] = {nx, 0.0f, nz};
            }

            tT[ti] = {static_cast<float>(s) / static_cast<float>(sides), y / height};

            vi++; ni++; ti++;
        }
    }

    int bottomCenter = vi;
    tV[vi] = {0.0f, 0.0f, 0.0f};
    tN[ni] = {0.0f, -1.0f, 0.0f};
    tT[ti] = {0.5f, 0.0f};
    vi++; ni++; ti++;

    int topCenter = vi;
    tV[vi] = {0.0f, height, 0.0f};
    tN[ni] = {0.0f, 1.0f, 0.0f};
    tT[ti] = {0.5f, 1.0f};

    int ii = 0;
    // Lateral: ring0 → ring1
    for (int s = 0; s < sides; s++) {
        int s1 = (s + 1) % sides;
        tI[ii++] = s;           tI[ii++] = sides + s;     tI[ii++] = s1;
        tI[ii++] = s1;          tI[ii++] = sides + s;     tI[ii++] = sides + s1;
    }
    // Bevel: ring1 → ring2
    for (int s = 0; s < sides; s++) {
        int s1 = (s + 1) % sides;
        tI[ii++] = sides + s;       tI[ii++] = 2*sides + s;     tI[ii++] = sides + s1;
        tI[ii++] = sides + s1;      tI[ii++] = 2*sides + s;     tI[ii++] = 2*sides + s1;
    }
    // Bottom cap
    for (int s = 0; s < sides; s++) {
        int s1 = (s + 1) % sides;
        tI[ii++] = bottomCenter; tI[ii++] = s1; tI[ii++] = s;
    }
    // Top cap
    for (int s = 0; s < sides; s++) {
        int s1 = (s + 1) % sides;
        tI[ii++] = topCenter; tI[ii++] = 2*sides + s; tI[ii++] = 2*sides + s1;
    }

    std::vector<CrystalVertex> vertices(indexCount);
    for (int i = 0; i < indexCount; i++) {
        int idx = tI[i];
        vertices[i] = { tV[idx], tN[idx], tT[idx] };
    }

    return vertices;
}

// ──────────────────────────────────────────────────────────────────────────
// Tunnel Cylinder Mesh — matches Raylib's GenMeshCylinder
// Simple open-ended cylinder rendered from the inside.
// ──────────────────────────────────────────────────────────────────────────
std::vector<CrystalVertex> CrystalGeometry::generateCylinderMesh(
    float radius, float length, int slices) {

    std::vector<CrystalVertex> vertices;
    vertices.reserve(slices * 6);

    float angleStep = glm::two_pi<float>() / static_cast<float>(slices);

    for (int i = 0; i < slices; i++) {
        float a0 = i * angleStep;
        float a1 = (i + 1) * angleStep;

        float c0 = std::cos(a0), s0 = std::sin(a0);
        float c1 = std::cos(a1), s1 = std::sin(a1);

        float u0 = static_cast<float>(i) / static_cast<float>(slices);
        float u1 = static_cast<float>(i + 1) / static_cast<float>(slices);

        glm::vec3 p00 = {radius * c0, 0.0f,   radius * s0};
        glm::vec3 p01 = {radius * c0, length, radius * s0};
        glm::vec3 p10 = {radius * c1, 0.0f,   radius * s1};
        glm::vec3 p11 = {radius * c1, length, radius * s1};

        glm::vec3 n0 = {-c0, 0.0f, -s0};
        glm::vec3 n1 = {-c1, 0.0f, -s1};

        vertices.push_back({p00, n0, {u0, 0.0f}});
        vertices.push_back({p10, n1, {u1, 0.0f}});
        vertices.push_back({p01, n0, {u0, 1.0f}});

        vertices.push_back({p10, n1, {u1, 0.0f}});
        vertices.push_back({p11, n1, {u1, 1.0f}});
        vertices.push_back({p01, n0, {u0, 1.0f}});
    }

    return vertices;
}

std::vector<CrystalVertex> CrystalGeometry::generateCubeMesh(float size) {
    std::vector<CrystalVertex> vertices;
    vertices.reserve(36);
    float h = size * 0.5f;

    struct Face { glm::vec3 n, u, v; };
    const Face faces[6] = {
        {{ 0,  0,  1}, {1, 0, 0}, {0, 1, 0}},
        {{ 0,  0, -1}, {-1, 0, 0}, {0, 1, 0}},
        {{ 1,  0,  0}, {0, 0, -1}, {0, 1, 0}},
        {{-1,  0,  0}, {0, 0, 1}, {0, 1, 0}},
        {{ 0,  1,  0}, {1, 0, 0}, {0, 0, -1}},
        {{ 0, -1,  0}, {1, 0, 0}, {0, 0, 1}},
    };

    for (const auto& f : faces) {
        glm::vec3 c = f.n * h;
        glm::vec3 p00 = c - f.u * h - f.v * h;
        glm::vec3 p10 = c + f.u * h - f.v * h;
        glm::vec3 p11 = c + f.u * h + f.v * h;
        glm::vec3 p01 = c - f.u * h + f.v * h;

        vertices.push_back({p00, f.n, {0.0f, 0.0f}});
        vertices.push_back({p10, f.n, {1.0f, 0.0f}});
        vertices.push_back({p11, f.n, {1.0f, 1.0f}});

        vertices.push_back({p00, f.n, {0.0f, 0.0f}});
        vertices.push_back({p11, f.n, {1.0f, 1.0f}});
        vertices.push_back({p01, f.n, {0.0f, 1.0f}});
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
