#version 450

// Crystal rod fragment shader — Pass 1 (Glass Refraction)
// Simulates PS2 GS framebuffer feedback: samples background with normal-based UV distortion.
// For now, uses a simple glass tint; full FB feedback requires input attachment (M5 integration).

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec2 fragScreenUV;
layout(location = 3) in float fragAlpha;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 rodColor;
    vec4 screenParams;  // x=width, y=height, z=time, w=rodAlpha
} pc;

// Feedback loop input
layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput screenBuffer;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(fragNormal);

    // Read the background pixel EXACTLY behind this fragment (Tile Memory read)
    // Note: Due to TBDR limitations of subpassLoad, we cannot offset the UV physically here.
    // Instead, we apply a chromatic distortion and luma shift to simulate refraction.
    vec4 bgColor = subpassLoad(screenBuffer);

    // Apply pseudo-distortion based on normal and time to fake refraction bending
    float time = pc.screenParams.z;
    vec3 distShift = vec3(N.x * 0.2 + sin(time) * 0.05, N.y * 0.2, N.z * 0.2);
    vec3 distortedBg = bgColor.rgb * vec3(1.0 + distShift.x, 1.0 - distShift.y, 1.0 + distShift.z);

    // Glass appearance: Fresnel-like edge brightness
    float fresnel = pow(1.0 - abs(N.z), 3.0);

    // Blend the distorted background with rod color (acts like a thick lens)
    vec3 lensColor = distortedBg * pc.rodColor.rgb * 2.5; // boosted brightness
    
    // Base glass color merging background
    vec3 glassColor = mix(lensColor, pc.rodColor.rgb, fresnel * 0.5);

    // Edge glow (simulates the specular highlight)
    float edgeGlow = pow(1.0 - abs(N.z), 5.0) * 0.8;
    glassColor += vec3(edgeGlow) * pc.rodColor.rgb;

    // Apply the alpha from push constants if needed (for reverse or alpha passes)
    outColor = vec4(glassColor, pc.screenParams.w > 0.0 ? pc.screenParams.w : 1.0);
}

