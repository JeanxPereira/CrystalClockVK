#version 450

// Crystal specular highlight shader — Pass 2/3 (Additive Glow)
// Ported from Raylib's crystal.fs renderPass <= 2 logic.
// Uses proper view-direction specular + rim light.

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

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 rodColor;
    vec4 screenParams;
} pc;

layout(location = 0) out vec4 outColor;

// Matches Raylib's CalcRimLight exactly
vec3 calcRimLight(vec3 viewDir, vec3 N) {
    return vec3(1.0) * smoothstep(0.3, 0.4, pow(max(0.0, 1.0 - dot(viewDir, N)), 2.0));
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 viewDir = normalize(ubo.viewPos.xyz - fragPosition);

    // Specular highlight — matches Raylib: pow(max(dot(viewDir, norm), 0.0), 32.0)
    float specular = pow(max(dot(viewDir, N), 0.0), 32.0);

    // Rim light
    vec3 rim = calcRimLight(viewDir, N);

    // Final glow — matches Raylib: (prismColor * 0.8) + vec3(specular * 0.6) + rim
    vec3 finalGlow = (pc.rodColor.rgb * 0.8) + vec3(specular * 0.6) + rim;

    outColor = vec4(finalGlow * fragAlpha, fragAlpha);
}
