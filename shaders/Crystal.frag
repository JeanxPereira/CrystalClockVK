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

    vec2 displacement = N.xy * 0.07;
    vec3 bgSample = texture(bgTexture, fragScreenUV + displacement).rgb;

    vec3 prismTint = ubo.prismColor.rgb * 1.4 + pc.rodColor.rgb * 0.4;
    vec3 tint = mix(bgSample * 1.3, prismTint, 0.22);

    outColor = vec4(tint, fragAlpha);
}
