#include "parity/Fixture.hpp"
#include <nlohmann/json.hpp>
#include <stb_image.h>
#include <fstream>
#include <stdexcept>

namespace parity {
namespace {

template <typename Enum>
Enum pick(const std::string& value, std::initializer_list<std::pair<const char*, Enum>> names, const char* what) {
    for (const auto& [name, e] : names) if (value == name) return e;
    throw std::runtime_error(std::string("unknown ") + what + ": " + value);
}

std::vector<uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

scene::BlendTerm term(const std::string& v) {
    return pick<scene::BlendTerm>(v, {{"Source", scene::BlendTerm::Source}, {"Destination", scene::BlendTerm::Destination}, {"Zero", scene::BlendTerm::Zero}}, "blend term");
}

scene::Address address(const nlohmann::json& j) {
    return {pick<scene::AddressMode>(j.at("mode"), {{"Repeat", scene::AddressMode::Repeat}, {"Clamp", scene::AddressMode::Clamp},
                {"RegionClamp", scene::AddressMode::RegionClamp}, {"RegionRepeat", scene::AddressMode::RegionRepeat}}, "address mode"),
            j.at("min"), j.at("max")};
}

DepthImage depthFromPair(const Image& pair) {
    DepthImage out{pair.width, pair.height, std::vector<uint32_t>(size_t(pair.width) * pair.height)};
    for (size_t i = 0; i < out.depth.size(); i++) {
        const uint8_t* p = &pair.rgba[i * 4];
        out.depth[i] = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    }
    return out;
}

}  // namespace

Image loadPngPair(const std::filesystem::path& prefix) {
    int w = 0, h = 0, n = 0, aw = 0, ah = 0;
    const std::string colourPath = prefix.string() + ".png", alphaPath = prefix.string() + "_alpha.png";
    stbi_uc* colour = stbi_load(colourPath.c_str(), &w, &h, &n, 3);
    if (!colour) throw std::runtime_error("cannot read " + colourPath);
    stbi_uc* alpha = stbi_load(alphaPath.c_str(), &aw, &ah, &n, 1);
    if (!alpha || aw != w || ah != h) { stbi_image_free(colour); throw std::runtime_error("cannot read " + alphaPath + " at the size of its colour file"); }
    Image out{uint32_t(w), uint32_t(h), std::vector<uint8_t>(size_t(w) * h * 4)};
    for (size_t i = 0; i < size_t(w) * h; i++) {
        out.rgba[i * 4 + 0] = colour[i * 3 + 0];
        out.rgba[i * 4 + 1] = colour[i * 3 + 1];
        out.rgba[i * 4 + 2] = colour[i * 3 + 2];
        out.rgba[i * 4 + 3] = alpha[i];
    }
    stbi_image_free(colour);
    stbi_image_free(alpha);
    return out;
}

Image Fixture::oracleColour(size_t pass) const { return loadPngPair(root / oracle.at(pass).colour); }
DepthImage Fixture::oracleDepth(size_t pass) const { return depthFromPair(loadPngPair(root / oracle.at(pass).depth)); }
DepthImage Fixture::startDepth() const { return depthFromPair(loadPngPair(root / depthStart)); }

Fixture loadFixture(const std::filesystem::path& directory) {
    std::ifstream in(directory / "frame.json");
    if (!in) throw std::runtime_error("no frame.json in " + directory.string());
    const nlohmann::json j = nlohmann::json::parse(in);

    Fixture fixture;
    fixture.root = directory;
    fixture.frame.field = j.at("field");
    fixture.depthStart = j.at("depthStart");

    for (const auto& t : j.at("targets")) {
        scene::Target target{t.at("id"), t.at("width"), t.at("height")};
        Image start{target.width, target.height, readFile(directory / t.at("start").get<std::string>())};
        if (start.rgba.size() != size_t(target.width) * target.height * 4) throw std::runtime_error("start buffer of " + target.id + " has the wrong size");
        fixture.targetStart.emplace(target.id, std::move(start));
        fixture.frame.targets.push_back(std::move(target));
    }
    for (const auto& t : j.at("textures")) {
        Image image{t.at("width"), t.at("height"), readFile(directory / t.at("file").get<std::string>())};
        if (image.rgba.size() != size_t(image.width) * image.height * 4) throw std::runtime_error("texture " + t.at("id").get<std::string>() + " has the wrong size");
        fixture.textures.emplace(t.at("id").get<std::string>(), std::move(image));
    }

    for (const auto& p : j.at("passes")) {
        scene::Pass pass{};
        pass.index = p.at("index");
        pass.name = p.at("name");
        pass.target = p.at("target");
        pass.primitive = pick<scene::Primitive>(p.at("primitive"), {{"Triangles", scene::Primitive::Triangles}, {"Sprites", scene::Primitive::Sprites}, {"Lines", scene::Primitive::Lines}}, "primitive");
        const auto& s = p.at("scissor");
        pass.scissor = {s[0], s[1], s[2], s[3]};
        if (!p.at("blend").is_null()) {
            const auto& b = p.at("blend");
            pass.blend = scene::Blend{term(b.at("a")), term(b.at("b")),
                pick<scene::BlendFactor>(b.at("c"), {{"SourceAlpha", scene::BlendFactor::SourceAlpha}, {"DestinationAlpha", scene::BlendFactor::DestinationAlpha}, {"Fixed", scene::BlendFactor::Fixed}}, "blend factor"),
                term(b.at("d")), b.at("fixed")};
        }
        pass.antialias = p.at("antialias");
        pass.depth = {pick<scene::DepthTest>(p.at("depth").at("test"), {{"Never", scene::DepthTest::Never}, {"Always", scene::DepthTest::Always},
                          {"GreaterEqual", scene::DepthTest::GreaterEqual}, {"Greater", scene::DepthTest::Greater}}, "depth test"),
                      p.at("depth").at("write")};
        if (!p.at("texture").is_null()) {
            const auto& t = p.at("texture");
            scene::Texture texture{};
            texture.sourceIsTarget = t.at("source").contains("target");
            texture.source = t.at("source").at(texture.sourceIsTarget ? "target" : "image");
            texture.width = t.at("width");
            texture.height = t.at("height");
            texture.coordinates = pick<scene::Coordinates>(t.at("coordinates"), {{"Texel", scene::Coordinates::Texel}, {"Projective", scene::Coordinates::Projective}}, "coordinates");
            texture.addressU = address(t.at("addressU"));
            texture.addressV = address(t.at("addressV"));
            texture.filter = pick<scene::Filter>(t.at("filter"), {{"Nearest", scene::Filter::Nearest}, {"Bilinear", scene::Filter::Bilinear}}, "filter");
            const auto& a = t.at("alpha");
            if (a.at("mode") == "Constant") texture.alpha = {true, a.at("value"), a.at("zeroWhenBlack")};
            pass.texture = std::move(texture);
        }
        if (!p.at("skip").is_null()) pass.skip = p.at("skip");
        for (const auto& v : p.at("vertices")) pass.vertices.push_back({v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9]});
        fixture.oracle.push_back({p.at("oracle").at("colour"), p.at("oracle").at("depth")});
        fixture.frame.passes.push_back(std::move(pass));
    }
    return fixture;
}

}  // namespace parity
