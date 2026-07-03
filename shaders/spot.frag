#version 450

// Additive radial glow for a light spot: a bright hot core fading to nothing at
// the billboard edge (the PS2 clock's after-image light points, patent 308).
layout(location = 0) in vec2 vUV;    // [-1,1] within the quad
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

void main() {
    float r = length(vUV);
    float glow = 1.0 - smoothstep(0.0, 1.0, r);  // soft radial falloff
    float core = pow(glow, 3.0);                  // hot centre
    vec3 col = vColor.rgb * (glow * 0.6 + core * 1.2);
    // Additive blend: alpha carries the intensity, colour is pre-scaled.
    outColor = vec4(col * vColor.a, glow * vColor.a);
}
