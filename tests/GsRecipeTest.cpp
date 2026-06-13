// Unit test for the rest of the gsvk translator (Phase 1 W3):
// alpha-test, depth, PRIM topology, texture/sampler keys, vertex transform,
// and full recipe assembly. Values cross-checked against clock_viewer.gs.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "gsvk/GsDrawRecipe.hpp"

namespace {

int g_fails = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("  FAIL: %s\n", what.c_str());
        g_fails++;
    }
}
bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

}  // namespace

int main() {
    using namespace gsvk;

    // GS compare-op mapping.
    check(mapAlphaCompare(6) == VK_COMPARE_OP_GREATER, "ATST GREATER");
    check(mapAlphaCompare(0) == VK_COMPARE_OP_NEVER, "ATST NEVER");
    check(mapDepthCompare(1) == VK_COMPARE_OP_ALWAYS, "ZTST ALWAYS");
    check(mapDepthCompare(2) == VK_COMPARE_OP_GREATER_OR_EQUAL, "ZTST GEQUAL");
    check(mapDepthCompare(3) == VK_COMPARE_OP_GREATER, "ZTST GREATER");

    // Topology + sprite expansion.
    {
        bool sprite = false;
        check(topologyFor(4, sprite) == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP && !sprite, "TRI_STRIP");
        check(topologyFor(2, sprite) == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP && !sprite, "LINE_STRIP");
        check(topologyFor(6, sprite) == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP && sprite, "SPRITE expands");
    }

    // Alpha test: the clock's ATE1 GREATER AREF0.
    {
        GsTest t{};
        t.ate = true; t.atst = 6; t.aref = 0;
        auto a = translateAlphaTest(t);
        check(a.enable && a.pass == VK_COMPARE_OP_GREATER && a.ref == 0, "alpha-test GREATER 0");
    }
    // Depth: ZTE1, ZMSK0 (write on), GEQUAL.
    {
        GsTest t{}; t.zte = true; t.ztst = 2;
        GsZbuf z{}; z.zmsk = false;
        auto d = translateDepth(t, z);
        check(d.testEnable && d.writeEnable && d.compare == VK_COMPARE_OP_GREATER_OR_EQUAL, "depth GEQUAL write");
        z.zmsk = true;
        check(!translateDepth(t, z).writeEnable, "ZMSK disables depth write");
    }

    // Texture + sampler keys.
    {
        GsTex0 t{}; t.tbp0 = 8960; t.tbw = 10; t.psm = 0; t.tw = 10; t.th = 8; t.tcc = 1; t.tfx = 0;
        auto k = textureKey(t);
        check(k.tbp0 == 8960 && k.tbw == 10 && k.psm == 0 && k.tw == 10 && k.th == 8 && k.tcc, "texture key");
        GsTex1 t1{}; t1.mmag = 1; t1.mmin = 1;
        auto s = samplerKey(t1);
        check(s.magLinear && s.minLinear, "bilinear sampler");
    }

    // Vertex transform: native 640x224.
    {
        check(near(toClip(320, 112, 0, 640, 224).x, 0.0f) && near(toClip(320, 112, 0, 640, 224).y, 0.0f), "center -> (0,0)");
        check(near(toClip(0, 0, 0, 640, 224).x, -1.0f) && near(toClip(0, 0, 0, 640, 224).y, -1.0f), "top-left -> (-1,-1)");
        check(near(toClip(640, 224, 0, 640, 224).x, 1.0f) && near(toClip(640, 224, 0, 640, 224).y, 1.0f), "bottom-right -> (1,1)");
    }

    // Full recipe: a textured src-over sprite.
    {
        GsPrimitive p{};
        p.prim.type = 6; p.prim.abe = true; p.prim.tme = true; p.prim.fst = true;
        p.alpha = {0, 1, 0, 1, 128};
        p.test.ate = true; p.test.atst = 6; p.test.zte = true; p.test.ztst = 1;
        p.tex1.mmag = 1; p.tex1.mmin = 1;
        auto r = assembleRecipe(p);
        check(r.spriteExpand, "recipe sprite expand");
        check(r.blend.enable && r.blend.srcFactor == VK_BLEND_FACTOR_SRC1_COLOR, "recipe src-over blend");
        check(r.alphaTest.enable && r.depth.testEnable, "recipe alpha+depth");
        check(r.textured && r.sampler.magLinear, "recipe textured bilinear");
    }

    if (g_fails) {
        std::printf("gsvk recipe test: %d FAILURES\n", g_fails);
        return 1;
    }
    std::printf("gsvk recipe test: OK\n");
    return 0;
}
