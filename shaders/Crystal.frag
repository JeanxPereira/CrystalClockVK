#version 450

// Crystal rod fragment shader — Pass 1/4 (Glass Refraction)
// Ported 1:1 from Raylib's crystal.fs Pass 0 logic.
// Samples tunnel background texture with normal-based UV distortion.

layout(location = 0) in vec3 fragPosition;
layout(location = 1) flat in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec2 fragScreenUV;
layout(location = 4) in float fragAlpha;
layout(location = 5) flat in vec3 fragViewNormal;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 viewProj;
    mat4 view;
    vec4 viewPos;
    vec4 prismColor;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D bgTexture;
layout(set = 0, binding = 2) uniform sampler2D normalMap;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 rodColor;
    vec4 screenParams;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(fragViewNormal);
    N.y = -N.y;

    // PS2 reference: rod struct +0xC0 displacement scalars ~0.008..0.03 (live capture).
    vec2 displacement = N.xy * 0.025;
    vec3 bgSample = texture(bgTexture, fragScreenUV + displacement).rgb;

    // Tint = desaturated cool-blue from rod RGBA(45,87,102) at struct +0x90.
    // Modulate bg sample rather than mixing toward bright color — the rod IS the tunnel,
    // just colored by the glass transmission.
    vec3 glassTint = pc.rodColor.rgb;
    vec3 result = bgSample * (vec3(1.0) - (vec3(1.0) - glassTint) * 0.65);

    // Subtle internal-reflection brightening at edges (Fresnel-ish on view normal).
    float edge = pow(1.0 - clamp(abs(N.z), 0.0, 1.0), 2.5);
    result += glassTint * edge * 0.18;

    outColor = vec4(result, fragAlpha);
}
