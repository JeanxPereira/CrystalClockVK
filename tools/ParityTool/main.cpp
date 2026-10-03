#include "core/HeadlessContext.hpp"
#include "parity/Compare.hpp"
#include "parity/Fixture.hpp"
#include "renderer/GsParityRenderer.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <stdexcept>

namespace {

std::string groupOf(const scene::Pass& pass) {
    static const char* primitives[] = {"triangles", "sprites", "lines"};
    static const char* tests[] = {"never", "always", "gequal", "greater"};
    static const char* terms[] = {"Cs", "Cd", "0"};
    static const char* factors[] = {"As", "Ad", "FIX"};
    static const char* modes[] = {"repeat", "clamp", "region-clamp", "region-repeat"};
    std::string group = primitives[int(pass.primitive)];
    if (pass.texture) {
        group += std::string(" tex(") + (pass.texture->sourceIsTarget ? "target" : "image") + "," + (pass.texture->coordinates == scene::Coordinates::Texel ? "texel" : "projective") + "," +
                 modes[int(pass.texture->addressU.mode)] + "," + (pass.texture->filter == scene::Filter::Bilinear ? "bilinear" : "nearest") + ")";
    } else {
        group += " flat";
    }
    if (pass.blend) group += std::string(" (") + terms[int(pass.blend->a)] + "-" + terms[int(pass.blend->b)] + ")*" + factors[int(pass.blend->c)] + "+" + terms[int(pass.blend->d)];
    else group += " opaque";
    if (pass.antialias) group += " aa";
    group += std::string(" z-") + tests[int(pass.depth.test)];
    return group;
}

nlohmann::json toJson(const parity::Difference& d) {
    return {{"pixels", d.pixels}, {"differing", d.differing}, {"largest", d.largest}, {"buckets", d.buckets}};
}

void writeRaw(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

// The oracle dumps the top-left w x h corner of each buffer (the draw's bounding box); the rest of the buffer is untouched by the draw.
parity::Image cropOf(const parity::Image& full, uint32_t w, uint32_t h) {
    if (w > full.width || h > full.height) throw std::runtime_error("the oracle rectangle does not fit the buffer");
    parity::Image out{w, h, std::vector<uint8_t>(size_t(w) * h * 4)};
    for (uint32_t y = 0; y < h; y++) std::copy_n(&full.rgba[size_t(y) * full.width * 4], size_t(w) * 4, &out.rgba[size_t(y) * w * 4]);
    return out;
}

parity::DepthImage cropOf(const parity::DepthImage& full, uint32_t w, uint32_t h) {
    if (w > full.width || h > full.height) throw std::runtime_error("the oracle rectangle does not fit the buffer");
    parity::DepthImage out{w, h, std::vector<uint32_t>(size_t(w) * h)};
    for (uint32_t y = 0; y < h; y++) std::copy_n(&full.depth[size_t(y) * full.width], w, &out.depth[size_t(y) * w]);
    return out;
}

void paste(parity::Image& full, const parity::Image& part) {
    if (part.width > full.width || part.height > full.height) throw std::runtime_error("the oracle rectangle does not fit the buffer");
    for (uint32_t y = 0; y < part.height; y++) std::copy_n(&part.rgba[size_t(y) * part.width * 4], size_t(part.width) * 4, &full.rgba[size_t(y) * full.width * 4]);
}

void paste(parity::DepthImage& full, const parity::DepthImage& part) {
    if (part.width > full.width || part.height > full.height) throw std::runtime_error("the oracle rectangle does not fit the buffer");
    for (uint32_t y = 0; y < part.height; y++) std::copy_n(&part.depth[size_t(y) * part.width], part.width, &full.depth[size_t(y) * full.width]);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4 && argc != 5) { std::fprintf(stderr, "usage: ParityTool <fixture dir> <out dir> <shader dir> [budgets.json]\n"); return 1; }
    try {
        const parity::Fixture fixture = parity::loadFixture(argv[1]);
        const std::filesystem::path out = argv[2];
        std::filesystem::create_directories(out);
        HeadlessContext context(true);
        GsParityRenderer renderer(context.gpu(), argv[3]);
        for (const auto& [id, image] : fixture.textures) renderer.setTexture(id, image.width, image.height, image.rgba);

        nlohmann::json report{{"passes", nlohmann::json::array()}, {"images", nlohmann::json::array()}};
        auto keep = [&](const std::string& name, const parity::Image& ours, const parity::Image& oracle) {
            writeRaw(out / (name + ".ours.rgba"), ours.rgba);
            writeRaw(out / (name + ".oracle.rgba"), oracle.rgba);
            writeRaw(out / (name + ".difference.rgba"), parity::differenceImage(ours, oracle, 16).rgba);
            report["images"].push_back({{"name", name}, {"width", ours.width}, {"height", ours.height},
                {"ours", name + ".ours.rgba"}, {"oracle", name + ".oracle.rgba"}, {"difference", name + ".difference.rgba"}});
        };

        // Isolated: each pass starts from the oracle's buffers before it, so a difference belongs to that pass alone.
        std::map<std::string, parity::Image> state = fixture.targetStart;
        parity::DepthImage depth = fixture.startDepth();
        struct Group { uint32_t passes{0}; parity::Difference colour, depth; };
        std::map<std::string, Group> groups;
        size_t worst = 0;
        uint64_t worstDiffering = 0;
        for (size_t i = 0; i < fixture.frame.passes.size(); i++) {
            const scene::Pass& pass = fixture.frame.passes[i];
            const parity::Image oracle = fixture.oracleColour(i);
            const parity::DepthImage oracleDepth = fixture.oracleDepth(i);
            nlohmann::json entry{{"index", pass.index}, {"name", pass.name}, {"group", groupOf(pass)}, {"skipped", pass.skip}};
            if (pass.skip.empty()) {
                for (const auto& [id, image] : state) renderer.setTarget(id, image.width, image.height, image.rgba);
                renderer.setDepth(depth.width, depth.height, depth.depth);
                renderer.draw(pass);
                const parity::Image& before = state.at(pass.target);
                const parity::Image ours = cropOf(parity::Image{before.width, before.height, renderer.readTarget(pass.target)}, oracle.width, oracle.height);
                const parity::DepthImage oursDepth = cropOf(parity::DepthImage{depth.width, depth.height, renderer.readDepth()}, oracleDepth.width, oracleDepth.height);
                const parity::Difference colour = parity::compare(ours, oracle), z = parity::compare(oursDepth, oracleDepth);
                entry["colour"] = toJson(colour);
                entry["depth"] = toJson(z);
                Group& group = groups[groupOf(pass)];
                group.passes++;
                group.colour += colour;
                group.depth += z;
                if (colour.differing > worstDiffering) { worstDiffering = colour.differing; worst = i; }
            }
            report["passes"].push_back(std::move(entry));
            paste(state.at(pass.target), oracle);
            paste(depth, oracleDepth);
        }
        report["groups"] = nlohmann::json::array();
        for (const auto& [name, group] : groups) report["groups"].push_back({{"group", name}, {"passes", group.passes}, {"colour", toJson(group.colour)}, {"depth", toJson(group.depth)}});

        if (worstDiffering) {
            const scene::Pass& pass = fixture.frame.passes[worst];
            std::map<std::string, parity::Image> before = fixture.targetStart;
            parity::DepthImage depthBefore = fixture.startDepth();
            for (size_t i = 0; i < worst; i++) { paste(before.at(fixture.frame.passes[i].target), fixture.oracleColour(i)); paste(depthBefore, fixture.oracleDepth(i)); }
            for (const auto& [id, image] : before) renderer.setTarget(id, image.width, image.height, image.rgba);
            renderer.setDepth(depthBefore.width, depthBefore.height, depthBefore.depth);
            renderer.draw(pass);
            const parity::Image oracle = fixture.oracleColour(worst);
            keep("worst-" + pass.name, cropOf(parity::Image{before.at(pass.target).width, before.at(pass.target).height, renderer.readTarget(pass.target)}, oracle.width, oracle.height), oracle);
        }

        // Chained: the whole frame from the start buffers, as the application will draw it.
        for (const auto& [id, image] : fixture.targetStart) renderer.setTarget(id, image.width, image.height, image.rgba);
        const parity::DepthImage startDepth = fixture.startDepth();
        renderer.setDepth(startDepth.width, startDepth.height, startDepth.depth);
        uint32_t skipped = 0;
        for (size_t i = 0; i < fixture.frame.passes.size(); i++) {
            const scene::Pass& pass = fixture.frame.passes[i];
            if (pass.skip.empty()) { renderer.draw(pass); continue; }
            const parity::Image oracle = fixture.oracleColour(i);
            const parity::DepthImage oracleDepth = fixture.oracleDepth(i);
            const parity::Image& shape = fixture.targetStart.at(pass.target);
            parity::Image merged{shape.width, shape.height, renderer.readTarget(pass.target)};
            paste(merged, oracle);
            renderer.setTarget(pass.target, merged.width, merged.height, merged.rgba);
            parity::DepthImage mergedDepth{startDepth.width, startDepth.height, renderer.readDepth()};
            paste(mergedDepth, oracleDepth);
            renderer.setDepth(mergedDepth.width, mergedDepth.height, mergedDepth.depth);
            skipped++;
        }
        report["chained"] = nlohmann::json::array();
        for (const auto& [id, oracle] : state) {
            const parity::Image ours{oracle.width, oracle.height, renderer.readTarget(id)};
            report["chained"].push_back({{"target", id}, {"colour", toJson(parity::compare(ours, oracle))}});
            keep("chained-" + id, ours, oracle);
        }
        report["chainedDepth"] = toJson(parity::compare(parity::DepthImage{depth.width, depth.height, renderer.readDepth()}, depth));
        {
            std::ifstream frameFile(std::filesystem::path(argv[1]) / "frame.json");
            const nlohmann::json frameJson = nlohmann::json::parse(frameFile);
            report["capture"] = frameJson.at("capture");
            report["frame"] = frameJson.at("frame");
        }
        report["skipped"] = skipped;
        report["validationErrors"] = context.validationErrors();

        std::ofstream(out / "report.json") << report.dump(1);
        std::printf("%-78s %6s %9s %9s %5s %9s\n", "group", "passes", "pixels", "differ", "max", "depth");
        for (const auto& [name, group] : groups) {
            std::printf("%-78s %6u %9llu %9llu %5u %9llu\n", name.c_str(), group.passes, (unsigned long long)group.colour.pixels,
                        (unsigned long long)group.colour.differing, group.colour.largest, (unsigned long long)group.depth.differing);
        }
        for (const auto& chained : report["chained"]) {
            std::printf("chained %s: %llu of %llu pixels differ, largest %u\n", chained["target"].get<std::string>().c_str(),
                        chained["colour"]["differing"].get<unsigned long long>(), chained["colour"]["pixels"].get<unsigned long long>(), chained["colour"]["largest"].get<unsigned>());
        }
        std::printf("skipped passes taken from the oracle: %u; validation errors: %u\n", skipped, context.validationErrors());

        if (argc == 5) {
            std::ifstream in(argv[4]);
            if (!in) throw std::runtime_error(std::string("no budgets at ") + argv[4]);
            const nlohmann::json budgets = nlohmann::json::parse(in);
            uint32_t breaches = 0;
            auto within = [&](const std::string& what, const parity::Difference& d, const nlohmann::json& budget) {
                const double share = d.pixels ? double(d.differing) / double(d.pixels) : 0.0;
                if (share > budget.at("differingShare").get<double>() || d.largest > budget.at("largest").get<uint32_t>()) {
                    std::printf("over budget: %s differs on %.6f of its pixels, largest %u\n", what.c_str(), share, d.largest);
                    breaches++;
                }
            };
            for (const auto& [name, group] : groups) {
                if (!budgets.at("groups").contains(name)) { std::printf("no budget for group: %s\n", name.c_str()); breaches++; continue; }
                within(name, group.colour, budgets.at("groups").at(name));
                within(name + " (depth)", group.depth, budgets.at("depth"));
            }
            for (const auto& [id, oracle] : state) within("chained " + id, parity::compare(parity::Image{oracle.width, oracle.height, renderer.readTarget(id)}, oracle), budgets.at("chained"));
            within("chained depth", parity::compare(parity::DepthImage{depth.width, depth.height, renderer.readDepth()}, depth), budgets.at("depth"));
            if (context.validationErrors()) { std::printf("validation errors: %u\n", context.validationErrors()); breaches++; }
            if (breaches) return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "ParityTool: %s\n", error.what());
        return 1;
    }
}
