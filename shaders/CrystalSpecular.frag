#version 450

// Crystal specular highlight shader — Pass 2/3 (Additive Glow)
// Pure additive edge highlight, matching GS ALPHA (2,1,2) — Cs*FIX + Cd

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec2 fragScreenUV;
layout(location = 3) in float fragAlpha;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 rodColor;
    vec4 screenParams;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(fragNormal);

    // Strong Fresnel edge detection for specular highlight
    float edge = pow(1.0 - abs(N.z), 4.0);

    // Pure additive color — blended with BlendMode::Additive in pipeline
    vec3 specular = pc.rodColor.rgb * edge * 1.2;

    outColor = vec4(specular, edge);
}
