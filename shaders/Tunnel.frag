#version 450

// PS2 OSDSYS-style animated tunnel background.
// Radial depth tunnel with rotating rings in dark blue/teal palette.

layout(location = 0) in vec2 fragUV;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 rodColor;
    vec4 screenParams; // x=width, y=height, z=time, w=unused
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    float time = pc.screenParams.z;
    float aspect = pc.screenParams.x / pc.screenParams.y;

    vec2 uv = fragUV * 2.0 - 1.0;
    uv.x *= aspect;

    float r = length(uv);
    float angle = atan(uv.y, uv.x);

    // Depth tunnel: inverse radius for infinite-depth illusion
    float tunnel = 1.0 / (r + 0.001);
    float tunnelSpeed = time * 0.4;

    // Rotating rings with depth parallax
    float rings = sin(tunnel * 3.0 - tunnelSpeed) * 0.5 + 0.5;

    // Angular rotation for swirl effect
    float swirl = sin(angle * 4.0 + tunnel * 0.5 + time * 0.2) * 0.5 + 0.5;

    // Combine patterns
    float pattern = rings * 0.7 + swirl * 0.3;

    // PS2 OSDSYS dark blue/teal palette
    vec3 deepBlue = vec3(0.02, 0.03, 0.08);
    vec3 midTeal  = vec3(0.05, 0.12, 0.18);
    vec3 highlight = vec3(0.08, 0.20, 0.30);

    vec3 color = mix(deepBlue, midTeal, pattern * 0.6);
    color = mix(color, highlight, swirl * rings * 0.4);

    // Vignette: darken edges, brighten center subtly
    float vignette = 1.0 - smoothstep(0.3, 1.8, r);
    color *= vignette;

    // Subtle pulsing glow at center
    float centerGlow = exp(-r * r * 4.0) * 0.15;
    color += vec3(0.04, 0.08, 0.15) * centerGlow * (sin(time * 0.8) * 0.3 + 0.7);

    outColor = vec4(color, 1.0);
}
