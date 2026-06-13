#include "gsvk/GsDrawRecipe.hpp"

namespace gsvk {

VkCompareOp mapAlphaCompare(uint8_t atst) {
    switch (atst & 7) {
        case 0: return VK_COMPARE_OP_NEVER;
        case 1: return VK_COMPARE_OP_ALWAYS;
        case 2: return VK_COMPARE_OP_LESS;
        case 3: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case 4: return VK_COMPARE_OP_EQUAL;
        case 5: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case 6: return VK_COMPARE_OP_GREATER;
        case 7: return VK_COMPARE_OP_NOT_EQUAL;
    }
    return VK_COMPARE_OP_ALWAYS;
}

VkCompareOp mapDepthCompare(uint8_t ztst) {
    switch (ztst & 3) {
        case 0: return VK_COMPARE_OP_NEVER;
        case 1: return VK_COMPARE_OP_ALWAYS;
        case 2: return VK_COMPARE_OP_GREATER_OR_EQUAL;  // GS GEQUAL (larger z = nearer)
        case 3: return VK_COMPARE_OP_GREATER;           // GS GREATER
    }
    return VK_COMPARE_OP_ALWAYS;
}

VkPrimitiveTopology topologyFor(uint8_t primType, bool& spriteExpand) {
    spriteExpand = false;
    switch (primType & 7) {
        case 0: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case 1: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case 2: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case 3: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case 4: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case 5: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
        case 6:  // SPRITE: 2 corner verts -> an axis-aligned quad
            spriteExpand = true;
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
}

GsAlphaTest translateAlphaTest(const GsTest& t) {
    GsAlphaTest a;
    a.enable = t.ate;
    a.pass = mapAlphaCompare(t.atst);
    a.ref = t.aref;
    return a;
}

GsDepthState translateDepth(const GsTest& t, const GsZbuf& z) {
    GsDepthState d;
    d.testEnable = t.zte;
    d.writeEnable = !z.zmsk;  // ZMSK=1 disables writes
    d.compare = mapDepthCompare(t.ztst);
    return d;
}

GsTextureKey textureKey(const GsTex0& t) {
    return {t.tbp0, t.tbw, t.psm, t.tw, t.th, t.cbp, t.cpsm, t.csa, bool(t.tcc), uint8_t(t.tfx)};
}

GsSamplerKey samplerKey(const GsTex1& t) {
    // GS MMAG/MMIN: 0=NEAREST, 1=LINEAR (LINEAR is the only mip-free min value used).
    return {bool(t.mmag), t.mmin == 1};
}

ClipPos toClip(float screenX, float screenY, uint32_t z, uint32_t fbW, uint32_t fbH) {
    ClipPos c;
    c.x = screenX * 2.0f / static_cast<float>(fbW) - 1.0f;
    c.y = screenY * 2.0f / static_cast<float>(fbH) - 1.0f;  // clip-Y down == screen-Y down
    c.z = static_cast<float>(z) / 4294967295.0f;  // PSMZ32 -> [0,1]
    c.w = 1.0f;
    return c;
}

GsDrawRecipe assembleRecipe(const GsPrimitive& p) {
    GsDrawRecipe r;
    r.topology = topologyFor(p.prim.type, r.spriteExpand);
    r.blend = translateBlend(p.alpha, p.prim.abe);
    r.alphaTest = translateAlphaTest(p.test);
    r.depth = translateDepth(p.test, p.zbuf);
    r.textured = p.prim.tme;
    if (r.textured) {
        r.texture = textureKey(p.tex0);
        r.sampler = samplerKey(p.tex1);
    }
    return r;
}

}  // namespace gsvk
