#version 450

// The crystal clock's tunnel backdrop: a deep-violet radial gradient darkening
// toward the centre (the black hole the rods radiate from), with faint mottle.
// Shared with rod_prism.frag so the rods can refract THIS exact background.
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

vec3 tunnel(vec2 uv) {
    vec2 c = (uv - 0.5) * vec2(640.0 / 224.0, 1.0);  // aspect-correct, centred
    float r = length(c);
    // Dark centre -> lighter violet edge.
    vec3 deep  = vec3(0.02, 0.01, 0.04);
    vec3 outer = vec3(0.20, 0.13, 0.30);
    vec3 col = mix(deep, outer, smoothstep(0.15, 0.9, r));
    // Faint mottle so the refraction has texture to distort.
    float m = sin(uv.x * 37.0) * sin(uv.y * 31.0) * 0.02;
    return col + m;
}

void main() {
    outColor = vec4(tunnel(vUV), 1.0);
}
