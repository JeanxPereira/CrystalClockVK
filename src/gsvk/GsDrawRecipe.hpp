#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include "gs/GsCommandStream.hpp"
#include "gsvk/GsBlendTranslator.hpp"

namespace gsvk {

// Alpha test (GS has no VK fixed-function equivalent -> fragment-shader discard).
// AFAIL is KEEP for the clock (full discard); other modes are not yet handled.
struct GsAlphaTest {
    bool enable = false;
    VkCompareOp pass = VK_COMPARE_OP_ALWAYS;  // keep fragment when (alpha `pass` ref)
    uint8_t ref = 0;
};

// Depth state. GS z is "larger = nearer", so the renderer clears depth to 0 and
// uses the GS compare op directly (GEQUAL/GREATER) — no z inversion needed.
struct GsDepthState {
    bool testEnable = false;
    bool writeEnable = false;
    VkCompareOp compare = VK_COMPARE_OP_ALWAYS;
};

// Identifies a texture to decode from VRAM (gs/SwizzleEngine + TextureDecoder).
struct GsTextureKey {
    uint32_t tbp0 = 0, tbw = 0, psm = 0, tw = 0, th = 0;
    uint32_t cbp = 0, cpsm = 0, csa = 0;  // CLUT (indexed PSMs)
    bool tcc = false;
    uint8_t tfx = 0;
};

struct GsSamplerKey {
    bool magLinear = false;
    bool minLinear = false;
};

// Vulkan clip-space position from a screen-pixel GS vertex.
struct ClipPos {
    float x, y, z, w;
};

VkCompareOp mapAlphaCompare(uint8_t atst);  // GS ATST (0..7) -> VkCompareOp
VkCompareOp mapDepthCompare(uint8_t ztst);  // GS ZTST (0..3) -> VkCompareOp
VkPrimitiveTopology topologyFor(uint8_t primType, bool& spriteExpand);

GsAlphaTest translateAlphaTest(const GsTest& t);
GsDepthState translateDepth(const GsTest& t, const GsZbuf& z);
GsTextureKey textureKey(const GsTex0& t);
GsSamplerKey samplerKey(const GsTex1& t);

// Screen pixel (x,y) + 32-bit GS z -> Vulkan clip space for a native fbW x fbH
// target. Vulkan clip-Y points down, matching GS screen-Y, so no flip.
ClipPos toClip(float screenX, float screenY, uint32_t z, uint32_t fbW, uint32_t fbH);

// Everything needed to issue one GS draw in Vulkan (minus the live VkImage /
// VkSampler / pipeline handles, which the renderer resolves from the keys).
struct GsDrawRecipe {
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    bool spriteExpand = false;  // GS SPRITE: expand 2 corner verts -> a quad
    GsBlendRecipe blend;
    GsAlphaTest alphaTest;
    GsDepthState depth;
    bool textured = false;
    GsTextureKey texture;
    GsSamplerKey sampler;
};

GsDrawRecipe assembleRecipe(const GsPrimitive& p);

}  // namespace gsvk
