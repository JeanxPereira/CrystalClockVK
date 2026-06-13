#include "gsvk/GsBlendTranslator.hpp"

namespace gsvk {

GsBlendRecipe translateBlend(const GsAlpha& a, bool abe) {
    GsBlendRecipe r;
    if (!abe) {
        r.enable = false;  // ABE off: write source color straight through
        return r;
    }
    r.enable = true;

    const bool cFix = (a.c == 2);
    r.coeff = a.c == 0 ? BlendCoeff::SrcAlpha
              : a.c == 1 ? BlendCoeff::DstAlpha
                         : BlendCoeff::Fix;
    r.fix = a.fix;
    r.dualSource = !cFix;  // As/Ad arrive via SRC1; FIX via the blend constant

    // The C/128 coefficient as a VK factor: SRC1 (shader output) or a constant.
    const VkBlendFactor cFac = cFix ? VK_BLEND_FACTOR_CONSTANT_COLOR : VK_BLEND_FACTOR_SRC1_COLOR;
    const VkBlendFactor cInv = cFix ? VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR
                                    : VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;

    // Match the (A,B,D) pattern. The clock enumerates exactly these three; the
    // FIX variants fall out for free via cFac/cInv. (PCSX2 m_blendMap rows
    // 0101 / 0201 / 2001.)
    if (a.a == 0 && a.b == 1 && a.d == 1) {  // src-over: Cs*C + Cd*(1-C)
        r.colorOp = VK_BLEND_OP_ADD;
        r.srcFactor = cFac;
        r.dstFactor = cInv;
    } else if (a.a == 0 && a.b == 2 && a.d == 1) {  // additive: Cs*C + Cd
        r.colorOp = VK_BLEND_OP_ADD;
        r.srcFactor = cFac;
        r.dstFactor = VK_BLEND_FACTOR_ONE;
    } else if (a.a == 2 && a.b == 0 && a.d == 1) {  // subtractive: Cd - Cs*C
        r.colorOp = VK_BLEND_OP_REVERSE_SUBTRACT;
        r.srcFactor = cFac;
        r.dstFactor = VK_BLEND_FACTOR_ONE;
    } else {
        r.feedback = true;  // outside the enumerated set: needs shader-side blend
    }
    return r;
}

}  // namespace gsvk
