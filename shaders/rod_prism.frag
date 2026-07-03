#version 450

// Translucent crystal bar look for the generated dial rods — a shader-only
// approximation of the GS crystal pass (no framebuffer-feedback refraction;
// that needs the tunnel background scene). Additive-blended so overlapping
// glow accumulates like the GS additive pass:
//   - tinted translucent body (vColor.rgb)
//   - a specular streak down the centre of the bar (glass highlight)
//   - a bright Fresnel edge where facets turn from the camera
//   - the lit hour rod thresholds its min/sec fill along the length (vColor.a)
layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec2 vUV;   // u = inner..outer, v = across width

layout(location = 0) out vec4 outColor;

void main() {
    const vec3 viewDir = vec3(0.0, 0.0, 1.0);
    const vec3 lightDir = normalize(vec3(-0.3, 0.5, 0.8));

    vec3 n = normalize(vNormal);
    float diff = 0.55 + 0.45 * max(dot(n, lightDir), 0.0);
    float fres = pow(1.0 - max(dot(n, viewDir), 0.0), 3.0);

    // Specular streak: brightest along the centre line of the width (v=0.5).
    float streak = smoothstep(0.5, 0.0, abs(vUV.y - 0.5)) * 0.6;

    // Min/sec fill: vColor.a is the fill fraction for the lit rod (1.0 for the
    // others = fully present). Below the threshold the crystal is bright-filled,
    // above it dims to a faint ghost of the rod.
    float fillEdge = smoothstep(vColor.a + 0.02, vColor.a - 0.02, vUV.x);
    float body = mix(0.25, 1.0, fillEdge);

    vec3 tint = vColor.rgb;
    vec3 col = tint * diff * body                      // translucent tinted body
             + mix(tint, vec3(1.0), 0.5) * streak * body  // glass specular streak
             + mix(tint, vec3(1.0), 0.5) * fres * 0.4;    // subtle crystal edge

    // Over-blend translucency: solid where filled, faint ghost above the fill.
    float a = mix(0.30, 0.92, body);
    outColor = vec4(col, a);
}
