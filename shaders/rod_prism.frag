#version 450

// Refractive crystal bar: the glass shows the tunnel background DISPLACED by the
// facet's tilt (the same tunnel() the bg pass draws, recomputed at an offset =
// refraction without a texture read), tinted, with a specular streak, a subtle
// Fresnel edge, and the lit hour rod's min/sec fill along the length (vColor.a).
layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec2 vUV;   // u = inner..outer, v = across width

layout(location = 0) out vec4 outColor;

// Must match bg_tunnel.frag.
vec3 tunnel(vec2 uv) {
    vec2 c = (uv - 0.5) * vec2(640.0 / 224.0, 1.0);
    float r = length(c);
    vec3 deep  = vec3(0.02, 0.01, 0.04);
    vec3 outer = vec3(0.20, 0.13, 0.30);
    vec3 col = mix(deep, outer, smoothstep(0.15, 0.9, r));
    float m = sin(uv.x * 37.0) * sin(uv.y * 31.0) * 0.02;
    return col + m;
}

void main() {
    const vec3 viewDir = vec3(0.0, 0.0, 1.0);
    const vec3 lightDir = normalize(vec3(-0.3, 0.5, 0.8));

    vec3 n = normalize(vNormal);
    float diff = 0.55 + 0.45 * max(dot(n, lightDir), 0.0);
    float fres = pow(1.0 - max(dot(n, viewDir), 0.0), 3.0);
    float streak = smoothstep(0.5, 0.0, abs(vUV.y - 0.5)) * 0.6;

    // Min/sec fill: below the threshold the crystal is solid, above it a ghost.
    float fillEdge = smoothstep(vColor.a + 0.02, vColor.a - 0.02, vUV.x);
    float body = mix(0.25, 1.0, fillEdge);

    // Refraction: sample the tunnel at this pixel displaced by the facet tilt
    // (normal.xy) — the warped background seen through the glass.
    vec2 screen = gl_FragCoord.xy / vec2(640.0, 224.0);
    vec3 refr = tunnel(screen + n.xy * 0.06);

    vec3 tint = vColor.rgb;
    // Glass = refracted background tinted by the rod colour, plus highlights.
    vec3 col = mix(refr, tint, 0.55) * diff
             + mix(tint, vec3(1.0), 0.5) * streak * body
             + mix(tint, vec3(1.0), 0.5) * fres * 0.4;

    float a = mix(0.45, 0.95, body);
    outColor = vec4(col, a);
}
