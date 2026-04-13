#version 450

// Tunnel vertex shader — 3D cylinder mesh (replaces fullscreen triangle)
// Ported from Raylib's tunnel.vs

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 viewProj;
    vec4 viewPos;
    vec4 prismColor;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 rodColor;
    vec4 screenParams;
} pc;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragPosition;
layout(location = 2) out vec3 fragNormal;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    fragPosition = worldPos.xyz;

    // Normal transform using inverse-transpose of model
    mat3 normalMatrix = transpose(inverse(mat3(pc.model)));
    fragNormal = normalize(normalMatrix * inNormal);

    fragTexCoord = inUV;
    gl_Position = ubo.viewProj * worldPos;
}
