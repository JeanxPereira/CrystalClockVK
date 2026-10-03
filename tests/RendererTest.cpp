#include "Check.hpp"
#include "core/HeadlessContext.hpp"
#include "renderer/GsParityRenderer.hpp"
#include <tuple>

namespace {
constexpr uint32_t W = 64, H = 32;

scene::Pass pass(scene::Primitive primitive, std::vector<scene::Vertex> vertices) {
    scene::Pass p{};
    p.index = 1; p.name = "test"; p.target = "t"; p.primitive = primitive;
    p.scissor = {0, 0, int32_t(W) - 1, int32_t(H) - 1};
    p.depth = {scene::DepthTest::Always, true};
    p.vertices = std::move(vertices);
    return p;
}
scene::Vertex at(float x, float y, uint32_t depth, float r, float g, float b, float a) { return {x, y, depth, r, g, b, a, 0, 0, 1}; }
const uint8_t* px(const std::vector<uint8_t>& image, uint32_t x, uint32_t y) { return &image[(y * W + x) * 4]; }
const scene::Blend Add{scene::BlendTerm::Source, scene::BlendTerm::Zero, scene::BlendFactor::SourceAlpha, scene::BlendTerm::Destination, 0};
}  // namespace

int main(int argc, char** argv) {
    CHECK(argc == 2);
    HeadlessContext context(true);
    GsParityRenderer renderer(context.gpu(), argv[1]);
    const std::vector<uint8_t> black(W * H * 4, 0);
    const std::vector<uint32_t> depth100(W * H, 100);

    // A sprite covers [x0, x1) x [y0, y1) at integer sample points; fractional corners round up.
    renderer.setTarget("t", W, H, black);
    renderer.setDepth(W, H, depth100);
    renderer.draw(pass(scene::Primitive::Sprites, {at(10, 20, 7, 0, 0, 0, 0), at(13, 22, 7, 10, 20, 30, 128), at(20.5f, 4, 7, 0, 0, 0, 0), at(22.5f, 5, 7, 1, 2, 3, 4)}));
    std::vector<uint8_t> image = renderer.readTarget("t");
    uint32_t lit = 0;
    for (uint32_t y = 0; y < H; y++) for (uint32_t x = 0; x < W; x++) {
        const bool first = x >= 10 && x < 13 && y >= 20 && y < 22, second = x >= 21 && x < 23 && y == 4;
        const uint8_t* p = px(image, x, y);
        if (first) CHECK(p[0] == 10 && p[1] == 20 && p[2] == 30 && p[3] == 128);
        else if (second) CHECK(p[0] == 1 && p[1] == 2 && p[2] == 3 && p[3] == 4);
        else CHECK(p[0] == 0 && p[3] == 0);
        lit += first || second;
    }
    CHECK(lit == 8);
    std::vector<uint32_t> depth = renderer.readDepth();
    CHECK(depth[20 * W + 10] == 7 && depth[0] == 100);

    // Sixty-four overlapping additive sprites in one pass: each must see the one before it.
    renderer.setTarget("t", W, H, black);
    scene::Pass stack = pass(scene::Primitive::Sprites, {});
    for (int i = 0; i < 64; i++) { stack.vertices.push_back(at(0, 0, 0, 0, 0, 0, 0)); stack.vertices.push_back(at(8, 8, 0, 1, 2, 3, 128)); }
    stack.blend = Add;
    renderer.draw(stack);
    image = renderer.readTarget("t");
    for (uint32_t y = 0; y < 8; y++) for (uint32_t x = 0; x < 8; x++) CHECK(px(image, x, y)[0] == 64 && px(image, x, y)[1] == 128 && px(image, x, y)[2] == 192);
    CHECK(px(image, 8, 0)[0] == 0);

    // Two triangles sharing an edge cover every pixel of their square exactly once.
    renderer.setTarget("t", W, H, black);
    scene::Pass quad = pass(scene::Primitive::Triangles, {at(3.25f, 2.5f, 0, 1, 1, 1, 128), at(40.75f, 5.125f, 0, 1, 1, 1, 128), at(9.5f, 29.0625f, 0, 1, 1, 1, 128),
                                                           at(40.75f, 5.125f, 0, 1, 1, 1, 128), at(9.5f, 29.0625f, 0, 1, 1, 1, 128), at(50.0f, 27.5f, 0, 1, 1, 1, 128)});
    quad.blend = Add;
    renderer.draw(quad);
    image = renderer.readTarget("t");
    uint32_t covered = 0;
    for (uint32_t i = 0; i < W * H; i++) { CHECK(image[i * 4] <= 1); covered += image[i * 4]; }
    CHECK(covered > 500);

    // Depth tests against a buffer holding 100.
    const std::tuple<scene::DepthTest, uint32_t, bool> depthCases[] = {
        {scene::DepthTest::Greater, 100u, false}, {scene::DepthTest::Greater, 101u, true},
        {scene::DepthTest::GreaterEqual, 100u, true}, {scene::DepthTest::GreaterEqual, 99u, false}, {scene::DepthTest::Never, 500u, false}};
    for (const auto& [test, z, drawn] : depthCases) {
        renderer.setTarget("t", W, H, black);
        renderer.setDepth(W, H, depth100);
        scene::Pass p = pass(scene::Primitive::Sprites, {at(0, 0, z, 0, 0, 0, 0), at(4, 4, z, 9, 9, 9, 9)});
        p.depth = {test, true};
        renderer.draw(p);
        CHECK((renderer.readTarget("t")[0] == 9) == drawn);
        CHECK(renderer.readDepth()[0] == (drawn ? z : 100u));
    }

    // Subtraction clamps at zero; a scissor cuts.
    renderer.setTarget("t", W, H, std::vector<uint8_t>(W * H * 4, 5));
    scene::Pass sub = pass(scene::Primitive::Sprites, {at(0, 0, 0, 0, 0, 0, 0), at(8, 8, 0, 9, 3, 0, 128)});
    sub.blend = scene::Blend{scene::BlendTerm::Zero, scene::BlendTerm::Source, scene::BlendFactor::SourceAlpha, scene::BlendTerm::Destination, 0};
    sub.scissor = {2, 2, 5, 5};
    renderer.draw(sub);
    image = renderer.readTarget("t");
    CHECK(px(image, 2, 2)[0] == 0 && px(image, 2, 2)[1] == 2 && px(image, 2, 2)[2] == 5);
    CHECK(px(image, 1, 2)[0] == 5 && px(image, 6, 5)[0] == 5);

    // A texture that is the pass's own target is refused.
    scene::Pass self = pass(scene::Primitive::Sprites, {at(0, 0, 0, 0, 0, 0, 0), at(4, 4, 0, 128, 128, 128, 128)});
    self.texture = scene::Texture{"t", true, 64, 32, scene::Coordinates::Texel, {}, {}, scene::Filter::Nearest, {}};
    bool threw = false;
    try { renderer.draw(self); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    // Nearest and bilinear on a 2x2 texture, texel coordinates: the centre of the four texels is their mean.
    const std::vector<uint8_t> four{0, 0, 0, 128, 64, 0, 0, 128, 0, 64, 0, 128, 64, 64, 0, 128};
    renderer.setTexture("four", 2, 2, four);
    for (const auto filter : {scene::Filter::Nearest, scene::Filter::Bilinear}) {
        renderer.setTarget("t", W, H, black);
        scene::Pass textured = pass(scene::Primitive::Sprites, {{0, 0, 0, 128, 128, 128, 128, 1.0f, 1.0f, 1}, {1, 1, 0, 128, 128, 128, 128, 1.0f, 1.0f, 1}});
        textured.texture = scene::Texture{"four", false, 2, 2, scene::Coordinates::Texel, {scene::AddressMode::Clamp, 0, 0}, {scene::AddressMode::Clamp, 0, 0}, filter, {}};
        renderer.draw(textured);
        const uint8_t* p = px(image = renderer.readTarget("t"), 0, 0);
        if (filter == scene::Filter::Nearest) CHECK(p[0] == 64 && p[1] == 64);
        else CHECK(p[0] == 32 && p[1] == 32);
    }

    // A copy sprite: corners 0,0 and 640,224 with UV 0.5 and 640.5; bilinear shifts UV by half a texel, so pixel x reads
    // texel x with weight zero on its neighbours. A 24-bit texture's alpha is TA0, or zero on a black texel.
    {
        constexpr uint32_t CW = 640, CH = 224;
        std::vector<uint8_t> source(CW * CH * 4);
        for (uint32_t y = 0; y < CH; y++) for (uint32_t x = 0; x < CW; x++) {
            uint8_t* s = &source[(y * CW + x) * 4];
            s[0] = uint8_t(x * 37 + y); s[1] = uint8_t(y * 11 + x * 3); s[2] = uint8_t(x ^ y); s[3] = 0;
        }
        renderer.setTarget("copy", CW, CH, std::vector<uint8_t>(CW * CH * 4, 0));
        renderer.setDepth(CW, CH, std::vector<uint32_t>(CW * CH, 0));
        renderer.setTexture("source", CW, CH, source);
        scene::Pass copy = pass(scene::Primitive::Sprites, {{0, 0, 0, 128, 128, 128, 128, 0.5f, 0.5f, 1}, {640, 224, 0, 128, 128, 128, 128, 640.5f, 224.5f, 1}});
        copy.target = "copy";
        copy.scissor = {0, 0, 639, 223};
        copy.texture = scene::Texture{"source", false, 1024, 256, scene::Coordinates::Texel, {scene::AddressMode::RegionClamp, 0, 639}, {scene::AddressMode::RegionClamp, 0, 223},
                                      scene::Filter::Bilinear, {true, 127, true}};
        renderer.draw(copy);
        const std::vector<uint8_t> copied = renderer.readTarget("copy");
        uint32_t wrong = 0;
        for (uint32_t i = 0; i < CW * CH; i++) {
            const bool black = source[i * 4] == 0 && source[i * 4 + 1] == 0 && source[i * 4 + 2] == 0;
            const uint8_t alpha = black ? 0 : 127;
            const bool bad = copied[i * 4] != source[i * 4] || copied[i * 4 + 1] != source[i * 4 + 1] || copied[i * 4 + 2] != source[i * 4 + 2] || copied[i * 4 + 3] != alpha;
            wrong += bad;
        }
        if (wrong) std::fprintf(stderr, "copy: %u pixels differ from their texel\n", wrong);
        CHECK(wrong == 0);

        // The oracle's software renderer is built for SSE4.1: U steps in blocks of four pixels. Sprite x 278.625..339.4375,
        // U 0..63 bilinear: step 67893.40625, start -7307.97 at x 279 (skip 3). At x 296: -7307 + int(step * -3) + 5 * int(step * 4)
        // = -7307 - 203680 + 5 * 271573 = 1146878, texel 17, weight 7; red 4 * texel gives 68 + (4 * 7 >> 4) = 69 (blocks of eight give 70).
        std::vector<uint8_t> ramp(64 * 64 * 4, 0);
        for (uint32_t i = 0; i < 64 * 64; i++) ramp[i * 4] = uint8_t((i % 64) * 4);
        renderer.setTexture("ramp", 64, 64, ramp);
        scene::Pass stepped = pass(scene::Primitive::Sprites, {{278.625f, 0, 0, 128, 128, 128, 128, 0, 0, 1}, {339.4375f, 4, 0, 128, 128, 128, 128, 63, 63, 1}});
        stepped.target = "copy";
        stepped.scissor = {0, 0, 639, 223};
        stepped.texture = scene::Texture{"ramp", false, 64, 64, scene::Coordinates::Texel, {scene::AddressMode::Clamp, 0, 0}, {scene::AddressMode::Clamp, 0, 0}, scene::Filter::Bilinear, {}};
        renderer.draw(stepped);
        const std::vector<uint8_t> steps = renderer.readTarget("copy");
        if (steps[296 * 4] != 69) std::fprintf(stderr, "stepped: x 296 red %d\n", steps[296 * 4]);
        CHECK(steps[296 * 4] == 69);
    }

    // Gouraud colour keeps seven fraction bits (c << 7) and modulates as (texel << 2) * c7 >> 16. Red runs 100..132 over
    // x 0..64, half a unit per pixel: at x 1 c7 = 100.5 * 128 = 12864, 255 * 12864 >> 14 = 200; at x 3, 101.5 gives 202.
    {
        const std::vector<uint8_t> white{255, 255, 255, 128};
        renderer.setTexture("white", 1, 1, white);
        renderer.setTarget("t", W, H, black);
        scene::Pass shaded = pass(scene::Primitive::Triangles, {{0, 0, 0, 100, 0, 0, 128, 0, 0, 1}, {64, 0, 0, 132, 0, 0, 128, 0, 0, 1}, {0, 32, 0, 100, 0, 0, 128, 0, 0, 1}});
        shaded.texture = scene::Texture{"white", false, 1, 1, scene::Coordinates::Texel, {scene::AddressMode::Clamp, 0, 0}, {scene::AddressMode::Clamp, 0, 0}, scene::Filter::Nearest, {}};
        renderer.draw(shaded);
        image = renderer.readTarget("t");
        if (px(image, 1, 1)[0] != 200 || px(image, 3, 1)[0] != 202) std::fprintf(stderr, "gouraud: %d %d\n", px(image, 1, 1)[0], px(image, 3, 1)[0]);
        CHECK(px(image, 1, 1)[0] == 200 && px(image, 3, 1)[0] == 202);
    }

    CHECK(context.validationErrors() == 0);
    return 0;
}
