#version 450

// GS fragment: texture MODULATE + dual-source blend factor (SRC1 = As/128) +
// alpha test (shader discard, since GS alpha-test has no VK fixed-function form).
layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUV;

layout(location = 0, index = 0) out vec4 outColor;  // src color (Cs)
layout(location = 0, index = 1) out vec4 outBlend;  // SRC1 factor = As/128 per channel

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(push_constant) uniform PC {
    float alphaRef;   // AREF / 255
    int   alphaEnable;
    int   alphaGreater;  // 1 = GREATER (the clock's only ATST); else treat as pass
    int   textured;
} pc;

// GS divides the (A-B)*C product by 128; tex MODULATE also uses >>7. Vertex/tex
// bytes are read as /255 unorm, so the GS /128 scale is *255/128.
const float GS = 255.0 / 128.0;

void main() {
    vec4 c = vColor;
    if (pc.textured == 1) {
        vec4 t = texture(tex, vUV);
        c.rgb = t.rgb * vColor.rgb * GS;  // MODULATE
        c.a   = t.a   * vColor.a;
    }

    // GS blend coefficient As/128 (As is the 8-bit fragment alpha).
    float as = clamp(c.a * GS, 0.0, 1.0);

    if (pc.alphaEnable == 1 && pc.alphaGreater == 1) {
        if (!(c.a > pc.alphaRef)) discard;  // ATST GREATER
    }

    outColor = vec4(c.rgb, 1.0);
    outBlend = vec4(vec3(as), 1.0);
}
