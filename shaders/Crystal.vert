#version 450

// Crystal rod vertex shader
// Transforms hex prism geometry with per-rod MVP matrix via push constants.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(push_constant) uniform PushConstants {
    mat4 mvp;           // Model-View-Projection matrix
    vec4 rodColor;      // RGBA tint for this rod
    vec4 screenParams;  // x=width, y=height, z=time, w=rodAlpha
} pc;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragUV;
layout(location = 2) out vec2 fragScreenUV;
layout(location = 3) out float fragAlpha;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);

    // Screen-space UV for framebuffer refraction sampling
    vec2 ndc = gl_Position.xy / gl_Position.w;
    fragScreenUV = ndc * 0.5 + 0.5;

    fragNormal = normalize((pc.mvp * vec4(inNormal, 0.0)).xyz);
    fragUV = inUV;
    fragAlpha = pc.screenParams.w;
}
