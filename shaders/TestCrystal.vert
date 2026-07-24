#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 viewProj;
    mat4 view;
    vec4 viewPos;
    vec4 prismColor;
    vec4 refractA;
    vec4 refractB;
    vec4 tintA;
    vec4 tintB;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 rodColor;
    vec4 screenParams;
} pc;

layout(location = 0) out vec3 fragPosition;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out vec3 fragScreenPos;
layout(location = 4) out vec3 fragViewDir;
layout(location = 5) out float fragHeight;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = ubo.viewProj * worldPos;

    fragPosition = worldPos.xyz;
    mat3 normalMatrix = transpose(inverse(mat3(pc.model)));
    fragNormal = normalize(normalMatrix * inNormal);
    fragUV = inUV;
    fragScreenPos = gl_Position.xyw;
    fragViewDir = ubo.viewPos.xyz - worldPos.xyz;
    fragHeight = inPosition.y;
}
