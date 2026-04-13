#version 450

// Crystal rod fragment shader — Pass 1/4 (Glass Refraction)
// Ported 1:1 from Raylib's crystal.fs Pass 0 logic.
// Samples tunnel background texture with normal-based UV distortion.

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec2 fragScreenUV;
layout(location = 4) in float fragAlpha;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 viewProj;
    vec4 viewPos;
    vec4 prismColor;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D bgTexture;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 rodColor;
    vec4 screenParams;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(fragNormal);

    // PS2-accurate refraction: offset screen UV by normal.xy
    // Matches Raylib: distortedUV = screenUV + (norm.xy * 0.05)
    vec2 distortedUV = fragScreenUV + N.xy * 0.05;

    // Sample the tunnel background (Vulkan UV is NOT flipped like OpenGL FBOs)
    vec3 bgSample = texture(bgTexture, distortedUV).rgb;

    // Tint with the cycling prism color — matches Raylib: mix(bgSample, prismColor, 0.4)
    vec3 tint = mix(bgSample, ubo.prismColor.rgb, 0.4);

    outColor = vec4(tint, fragAlpha);
}
