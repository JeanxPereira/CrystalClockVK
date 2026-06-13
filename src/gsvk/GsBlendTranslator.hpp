#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include "gs/GsRegisterState.hpp"

// gsvk: the GS->VK translator. The only layer where GS and VK meet.
namespace gsvk {

// Source of the GS "C" blend coefficient (the *.../128 multiplier).
enum class BlendCoeff : uint8_t { SrcAlpha, DstAlpha, Fix };

// One GS ALPHA config translated to a Vulkan blend setup.
//
// GS blends as Cv = ((A - B) * C) / 128 + D, with A,B,D in {Cs,Cd,0} and
// C in {As,Ad,FIX} (range 0..128). The exact VK mapping uses dual-source
// blending: the fragment shader emits SRC1 = C/128 as a per-channel factor,
// so the /128 scale is applied in-shader (not via VK's /255 factors). The FIX
// case instead uses a blend constant color = FIX/128. Anchored on PCSX2
// GSDevice::m_blendMap + GSDeviceVK (blend constant = AFIX/128).
struct GsBlendRecipe {
    bool enable = false;  // false => opaque (ABE off): no blending
    VkBlendOp colorOp = VK_BLEND_OP_ADD;
    VkBlendFactor srcFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstFactor = VK_BLEND_FACTOR_ZERO;
    BlendCoeff coeff = BlendCoeff::SrcAlpha;  // what feeds SRC1 / the constant
    uint8_t fix = 0;          // when coeff==Fix: blend constant = fix/128
    bool dualSource = false;  // needs a fragment-shader SRC1 output (C = As/Ad)
    bool feedback = false;    // true => not fixed-function mappable; shader blend
};

// Translate a GS ALPHA register + the PRIM ABE bit into a VK blend recipe.
// Configs outside the enumerated fixed-function set are flagged feedback=true.
GsBlendRecipe translateBlend(const GsAlpha& alpha, bool abe);

}  // namespace gsvk
