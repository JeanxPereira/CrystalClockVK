#include "parity/FromScene.hpp"

#include <cstdio>
#include <stdexcept>

namespace parity {

namespace {

std::string blockName(const char* prefix, uint32_t block) {
    char text[16];
    std::snprintf(text, sizeof text, "%s%04x", prefix, block);
    return text;
}

GsBlend blendOf(scene::BlendOp op, uint8_t constant) {
    using T = GsBlendTerm;
    switch (op) {
    case scene::BlendOp::Add: return {T::Source, T::Zero, GsBlendFactor::SourceAlpha, T::Destination, constant};
    case scene::BlendOp::AlphaOver: return {T::Source, T::Destination, GsBlendFactor::SourceAlpha, T::Destination, constant};
    case scene::BlendOp::Subtract: return {T::Zero, T::Source, GsBlendFactor::SourceAlpha, T::Destination, constant};
    case scene::BlendOp::FixedOver: return {T::Source, T::Destination, GsBlendFactor::Fixed, T::Destination, constant};
    case scene::BlendOp::FixedAdd: return {T::Source, T::Zero, GsBlendFactor::Fixed, T::Destination, constant};
    case scene::BlendOp::Opaque: break;
    }
    throw std::runtime_error("an opaque pass has no blend");
}

GsAddress addressOf(const scene::Material& m, int32_t min, int32_t max) {
    switch (m.sampling) {
    case scene::Sampling::Repeat: return {GsAddressMode::Repeat, 0, 0};
    case scene::Sampling::Clamp: return {GsAddressMode::Clamp, 0, 0};
    case scene::Sampling::ClampToRegion: return {GsAddressMode::RegionClamp, min, max};
    }
    throw std::runtime_error("unknown sampling");
}

GsTexture textureOf(const scene::Material& m, const GsFrameLayout& layout) {
    GsTexture t{};
    if (m.source == scene::SourceKind::Target) {
        t.source = layout.targets[static_cast<size_t>(m.sourceTarget)];
        t.sourceIsTarget = true;
        t.width = layout.targetTextureWidth;
        t.height = layout.targetTextureHeight;
    } else {
        const GsTextureImage& image = layout.textures.at(m.texture);
        t.source = image.id;
        t.width = image.width;
        t.height = image.height;
    }
    t.coordinates = m.coordinates == scene::CoordinateKind::Texel ? GsCoordinates::Texel : GsCoordinates::Projective;
    t.addressU = addressOf(m, m.region[0], m.region[1]);
    t.addressV = addressOf(m, m.region[2], m.region[3]);
    t.filter = m.bilinear ? GsFilter::Bilinear : GsFilter::Nearest;
    if (m.colourOnly) t.alpha = {true, 0x7f, true};
    return t;
}

}

// clock_frame.mjs stateWriters: the display drawn is the frame texture of `frameTexture` (block 0 or
// width * height / 64); work buffer 0 at page 3 * width * height / 2048, work buffer 1 at width * height / 512;
// clock texture n after width * height * 5 words, 128 x 128 for texture 1 and 64 x 64 for the others.
GsFrameLayout clockLayout(int32_t width, int32_t height, int32_t displayIndex) {
    GsFrameLayout layout;
    layout.width = static_cast<uint32_t>(width);
    layout.height = static_cast<uint32_t>(height);
    const uint32_t display = displayIndex == 0 ? static_cast<uint32_t>((width * height) >> 6) : 0;
    const uint32_t refraction = static_cast<uint32_t>((3 * width * height) >> 11) * 32, work = static_cast<uint32_t>((width * height) >> 9) * 32;
    layout.targets = {blockName("fb", display), blockName("fb", refraction), blockName("fb", work)};
    layout.targetTextureWidth = 1u << 10;
    layout.targetTextureHeight = 1u << 8;
    int64_t words = int64_t(width) * height * 5;
    for (int32_t n = 0; n < 10; ++n) {
        const int32_t log = n == 1 ? 7 : 6;
        char id[32];
        std::snprintf(id, sizeof id, "t%04x-%d-0-%dx%d", static_cast<uint32_t>(words >> 6), (1 << log) >> 6, log, log);
        layout.textures[n] = {id, 1u << log, 1u << log};
        words += n == 1 ? 128 * 128 : 64 * 64;
    }
    return layout;
}

GsFrame fromScene(const scene::Frame& frame, const GsFrameLayout& layout) {
    GsFrame out;
    out.field = static_cast<uint32_t>(frame.field);
    for (const std::string& id : layout.targets) out.targets.push_back({id, layout.width, layout.height});
    static const GsPrimitive primitives[] = {GsPrimitive::Triangles, GsPrimitive::Lines, GsPrimitive::Sprites};
    static const GsDepthTest tests[] = {GsDepthTest::Always, GsDepthTest::GreaterEqual, GsDepthTest::Greater};
    for (size_t i = 0; i < frame.passes.size(); ++i) {
        const scene::Pass& pass = frame.passes[i];
        const scene::Material& m = pass.material;
        GsPass p{};
        p.index = static_cast<uint32_t>(i);
        p.name = pass.name;
        p.target = layout.targets[static_cast<size_t>(pass.target)];
        p.primitive = primitives[static_cast<size_t>(pass.topology)];
        p.scissor = {0, 0, static_cast<int32_t>(layout.width) - 1, static_cast<int32_t>(layout.height) - 1};
        if (m.blend != scene::BlendOp::Opaque) p.blend = blendOf(m.blend, m.blendConstant);
        p.antialias = pass.edgeSmoothing;
        p.depth = {tests[static_cast<size_t>(m.depthTest)], m.depthWrite};
        if (m.source != scene::SourceKind::None) p.texture = textureOf(m, layout);
        const float shift = pass.halfLine && frame.field ? 0.5f : 0.0f;
        for (const scene::Vertex& v : pass.vertices)
            p.vertices.push_back({v.x, v.y - shift, v.z, float(v.r), float(v.g), float(v.b), float(v.a), v.u, v.v, v.q});
        out.passes.push_back(std::move(p));
    }
    return out;
}

}
