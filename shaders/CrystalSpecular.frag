#version 450

// Crystal specular highlight shader — Pass 2/3 (Additive Glow)
// Ported from Raylib's crystal.fs renderPass <= 2 logic.
// Uses proper view-direction specular + rim light.

layout(location = 0) in vec3 fragPosition;
layout(location = 1) flat in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec2 fragScreenUV;
layout(location = 4) in float fragAlpha;

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

vec3 calcRimLight(vec3 viewDir, vec3 N) {
    float r = smoothstep(0.72, 0.98, pow(max(0.0, 1.0 - dot(viewDir, N)), 2.0));
    return vec3(r);
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 viewDir = normalize(ubo.viewPos.xyz - fragPosition);

    float specHard = pow(max(dot(viewDir, N), 0.0), 80.0);
    vec3 rim = calcRimLight(viewDir, N);

    vec3 finalGlow = (pc.rodColor.rgb * 0.45) + vec3(specHard * 0.9) + rim * 0.85;
    finalGlow = min(finalGlow, vec3(1.4));

    outColor = vec4(finalGlow * fragAlpha, fragAlpha);
}
