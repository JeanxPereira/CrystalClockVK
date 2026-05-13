#version 450

// Crystal rod vertex shader — ported from Raylib's crystal.vs
// Uses per-frame UBO for viewProj and push constants for per-rod model matrix.
// Computes proper normal transform using inverse-transpose of model matrix.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 viewProj;
    mat4 view;
    vec4 viewPos;
    vec4 prismColor;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 rodColor;
    vec4 screenParams;  // x=width, y=height, z=time, w=rodAlpha
} pc;

layout(location = 0) out vec3 fragPosition;
layout(location = 1) flat out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out vec2 fragScreenUV;
layout(location = 4) out float fragAlpha;
layout(location = 5) flat out vec3 fragViewNormal;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = ubo.viewProj * worldPos;

    fragPosition = worldPos.xyz;

    // Proper normal transform: inverse-transpose of upper 3x3 of model matrix
    mat3 normalMatrix = transpose(inverse(mat3(pc.model)));
    fragNormal = normalize(normalMatrix * inNormal);
    fragViewNormal = normalize(mat3(ubo.view) * fragNormal);

    fragUV = inUV * 2.0; // Match Raylib's fragTexCoord = vertexTexCoord * 2.0

    // Screen-space UV for refraction sampling
    vec2 ndc = gl_Position.xy / gl_Position.w;
    fragScreenUV = ndc * 0.5 + 0.5;

    fragAlpha = pc.screenParams.w;
}
