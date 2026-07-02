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
    int   texaExpand24;  // 1 = PSMCT24 framebuffer feedback: alpha from TEXA, not stored
    float texaTA0;       // TA0 / 255
    int   texaAEM;       // AEM: when 1, RGB==0 texels get alpha 0 (transparent)
} pc;

// GS divides the (A-B)*C product by 128; tex MODULATE also uses >>7. Vertex/tex
// bytes are read as /255 unorm, so the GS /128 scale is *255/128.
const float GS = 255.0 / 128.0;

void main() {
    vec4 c = vColor;
    if (pc.textured == 1) {
        vec4 t = texture(tex, vUV);
        // PSMCT24 has no stored alpha: the GS texture read expands it via TEXA
        // (Expand24To32). AEM=1 makes black texels transparent. Applied after the
        // bilinear fetch (PCSX2 expands per-texel pre-filter; close enough here).
        if (pc.texaExpand24 == 1) {
            bool rgbNonZero = any(greaterThan(t.rgb, vec3(0.0)));
            t.a = (pc.texaAEM == 0 || rgbNonZero) ? pc.texaTA0 : 0.0;
        }
        c.rgb = t.rgb * vColor.rgb * GS;  // MODULATE
        c.a   = t.a   * vColor.a;
    }

    // GS blend coefficient As/128 (As is the 8-bit fragment alpha).
    float as = clamp(c.a * GS, 0.0, 1.0);

    if (pc.alphaEnable == 1 && pc.alphaGreater == 1) {
        if (!(c.a > pc.alphaRef)) discard;  // ATST GREATER
    }

    // GS stores the fragment alpha in the framebuffer (FBA=0); feedback reads
    // depend on it (0x80 = fully opaque in GS 0..128 semantics).
    outColor = vec4(c.rgb, c.a);
    outBlend = vec4(vec3(as), 1.0);
}
