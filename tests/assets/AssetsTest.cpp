#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <stb_image.h>

#include "../Check.hpp"
#include "assets/AssetPack.hpp"
#include "assets/ClockTextures.hpp"
#include "assets/ElfImage.hpp"
#include "assets/Expand.hpp"
#include "assets/ImageCipher.hpp"
#include "assets/Resources.hpp"
#include "assets/Romdir.hpp"
#include "scene/Rods.hpp"

namespace fs = std::filesystem;
using assets::Bytes;
using assets::View;

namespace {

bool equalBytes(View a, View b, const std::string& what) {
    if (a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size()) == 0) return true;
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
    std::fprintf(stderr, "%s: %zu vs %zu bytes, first difference at %zu\n", what.c_str(), a.size(), b.size(), at);
    return false;
}

// One directory listing and its members' Expand output against the model's (tests/assets/model_outputs.mjs).
int compareDirectory(const nlohmann::json& want, View image, int64_t start, const fs::path& members, size_t& expanded) {
    CHECK(start == want.at("start").get<int64_t>());
    const auto entries = assets::romdirEntries(image, size_t(start));
    CHECK(entries.size() == want.at("entries").size());
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& w = want["entries"][i];
        CHECK(entries[i].name == w.at("name").get<std::string>());
        CHECK(entries[i].size == w.at("size").get<uint32_t>());
        CHECK(entries[i].offset == w.at("offset").get<uint32_t>());
        if (!w.contains("expanded")) continue;
        const assets::Expanded e = assets::expand(image, entries[i].offset);
        CHECK(e.size == w.at("expanded").get<uint32_t>());
        CHECK(e.produced == w.at("produced").get<size_t>());
        CHECK(e.consumed == w.at("consumed").get<size_t>());
        CHECK(equalBytes(e.out, assets::readFile(members / (entries[i].name + ".expanded")), entries[i].name));
        ++expanded;
    }
    return 0;
}

// (a) Expand, the HDD container decrypt and the ROMDIR reader, byte-equal to References/model/sound_data.mjs.
int ports(const fs::path& model, const fs::path& folder, const fs::path& biosPath) {
    std::ifstream in(model / "listing.json");
    const nlohmann::json listing = nlohmann::json::parse(in);
    const Bytes elf = assets::readFile(folder / assets::kProgramName);
    const assets::CipherTables tables = assets::CipherTables::fromProgram(assets::ElfImage(elf));
    char key[32];
    std::snprintf(key, sizeof key, "%llx", static_cast<unsigned long long>(tables.key));
    CHECK(listing.at("key").get<std::string>() == key);
    size_t expanded = 0, decrypted = 0;
    for (const char* name : {"TEXIMAGE", "SNDIMAGE"}) {
        const std::string tag = std::string("hdd-") + name;
        const Bytes image = assets::decryptImage(assets::readFile(folder / name), tables);
        CHECK(equalBytes(image, assets::readFile(model / (tag + ".decrypted")), tag));
        decrypted += image.size();
        CHECK(compareDirectory(listing.at(tag), image, assets::romdirStart(image), model / tag, expanded) == 0);
    }
    const Bytes bios = assets::readFile(biosPath);
    const int64_t start = assets::romdirStart(bios, 0x100000);
    size_t none = 0;
    CHECK(compareDirectory(listing.at("bios"), bios, start, model, none) == 0);
    const auto entries = assets::romdirEntries(bios, size_t(start));
    for (const char* name : {"TEXIMAGE", "SNDIMAGE", "ICOIMAGE"}) {
        const std::string tag = std::string("bios-") + name;
        const View member = assets::romdirMember(bios, entries, name);
        CHECK(equalBytes(member, assets::readFile(model / (tag + ".member")), tag));
        CHECK(compareDirectory(listing.at(tag), member, assets::romdirStart(member), model / tag, expanded) == 0);
    }
    std::printf("ports: key %s, %zu bytes decrypted, 6 directories, %zu members expanded, all equal to sound_data.mjs\n", key, decrypted, expanded);
    return 0;
}

int texturesEqualPngs(const assets::AssetSet& set, const fs::path& pngs, const std::string& what) {
    for (const auto& info : assets::kClockTextures) {
        const assets::Asset* a = set.find(info.name);
        CHECK(a != nullptr);
        CHECK(a->kind == assets::AssetKind::TextureRgba32 && a->width == info.width && a->height == info.height);
        char file[64];
        std::snprintf(file, sizeof file, "tbp-%x-%ux%u.png", info.tbp, info.width, info.height);
        int w = 0, h = 0, channels = 0;
        stbi_uc* pixels = stbi_load((pngs / file).string().c_str(), &w, &h, &channels, 4);
        CHECK(pixels != nullptr);
        const bool same = uint32_t(w) == info.width && uint32_t(h) == info.height && equalBytes(a->data, View(pixels, size_t(w) * h * 4), std::string(info.name));
        stbi_image_free(pixels);
        CHECK(same);
    }
    std::printf("textures from %s: the ten equal References/textures, pixel for pixel\n", what.c_str());
    return 0;
}

// (b) The extractor writes the BIOS's members unchanged; decoding them, and the HDD OSD folder, gives the ten
// textures of References/textures.
int textures(const fs::path& model, const fs::path& folder, const fs::path& biosPath, const fs::path& pngs, const fs::path& meshPath, const fs::path& scratch) {
    fs::remove_all(scratch);
    const fs::path extracted = scratch / "rom-0230a";
    const auto written = assets::extractBios(biosPath, extracted);
    CHECK(written.size() == 3);
    for (const char* name : {"TEXIMAGE", "SNDIMAGE", "ICOIMAGE"}) CHECK(equalBytes(assets::readFile(extracted / name), assets::readFile(model / (std::string("bios-") + name + ".member")), name));
    CHECK(assets::extractBios(biosPath, extracted).size() == 3);
    fs::create_directories(scratch / "other");
    assets::writeFile(scratch / "other" / "TEXIMAGE", Bytes{1, 2, 3});
    bool refused = false;
    try {
        assets::extractBios(biosPath, scratch / "other");
    } catch (const std::exception& e) {
        refused = true;
        std::printf("refused: %s\n", e.what());
    }
    CHECK(refused);

    const assets::AssetSet rom = assets::decodeFolder(extracted);
    CHECK(rom.find("FNTOSD") == nullptr && rom.find("RODMESH") == nullptr);
    CHECK(rom.sourceOf(*rom.find("TEXCFLOW")).name == "TEXIMAGE");
    CHECK(texturesEqualPngs(rom, pngs, "the ROM 2.30 BIOS's TEXIMAGE") == 0);

    const assets::AssetSet hdd = assets::decodeFolder(folder);
    CHECK(texturesEqualPngs(hdd, pngs, "HDD OSD 1.10U's encrypted TEXIMAGE") == 0);
    CHECK(equalBytes(hdd.find("FNTOSD")->data, assets::readFile(folder / "FNTOSD"), "FNTOSD"));
    CHECK(equalBytes(hdd.find("PROGRAM")->data, assets::readFile(folder / assets::kProgramName), "hddosd.elf"));
    CHECK(hdd.sourceOf(*hdd.find("PROGRAM")).name == assets::kProgramName);

    const assets::Asset* mesh = hdd.find("RODMESH");
    CHECK(mesh != nullptr && mesh->kind == assets::AssetKind::Mesh && mesh->width == 16);
    const scene::RodMesh json = scene::loadRodMesh(meshPath);
    std::vector<float> want;
    for (const auto* list : {&json.positions, &json.normals, &json.coordinates})
        for (const scene::Vec4& v : *list) want.insert(want.end(), v.begin(), v.end());
    CHECK(equalBytes(mesh->data, View(reinterpret_cast<const uint8_t*>(want.data()), want.size() * 4), "rod mesh"));
    std::printf("rod mesh from hddosd.elf: %zu floats, equal to facts/data/rod-mesh.json\n", want.size());
    return 0;
}

double millisecondsOf(const std::function<void()>& work) {
    const auto begin = std::chrono::steady_clock::now();
    work();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
}

// (c) The pack round-trips byte for byte; it is used while its sources are unchanged and made again when one changes.
int pack(const fs::path& folder, const fs::path& biosPath, const fs::path& scratch) {
    fs::remove_all(scratch);
    const assets::AssetSet set = assets::decodeFolder(folder);
    const Bytes bytes = assets::packBytes(set);
    const auto back = assets::unpack(bytes);
    CHECK(back.has_value() && *back == set);
    CHECK(equalBytes(assets::packBytes(*back), bytes, "pack written again"));
    const fs::path file = scratch / "assets.bin";
    assets::writePack(file, set);
    CHECK(equalBytes(assets::readFile(file), bytes, "pack file"));
    CHECK(assets::readPack(file) == set);
    Bytes wrongVersion = bytes;
    wrongVersion[8] ^= 0xff;
    CHECK(!assets::unpack(wrongVersion).has_value());
    CHECK(!assets::unpack(View(bytes).first(20)).has_value());
    std::printf("pack: %zu assets from %zu sources, %zu bytes, round trip equal\n", set.assets.size(), set.sources.size(), bytes.size());

    const fs::path cache = scratch / "cache" / "assets.bin";
    std::optional<assets::LoadedAssets> cold, warm;
    const double coldMs = millisecondsOf([&] { cold = assets::loadAssets(folder, cache); });
    const double warmMs = millisecondsOf([&] { warm = assets::loadAssets(folder, cache); });
    CHECK(cold && !cold->fromPack && warm && warm->fromPack && warm->set == cold->set && cold->set == set);
    std::printf("start: cold %.1f ms (decode and write the pack), warm %.1f ms (read the pack)\n", coldMs, warmMs);

    const fs::path rom = scratch / "rom";
    assets::extractBios(biosPath, rom);
    const fs::path romCache = scratch / "rom.bin";
    CHECK(!assets::loadAssets(rom, romCache)->fromPack);
    CHECK(assets::loadAssets(rom, romCache)->fromPack);
    Bytes changed = assets::readFile(rom / "TEXIMAGE");
    changed.push_back(0);
    assets::writeFile(rom / "TEXIMAGE", changed);
    const auto again = assets::loadAssets(rom, romCache);
    CHECK(again && !again->fromPack);
    CHECK(again->set.sources.at(0).hash == assets::hashOf(changed));
    CHECK(assets::loadAssets(scratch / "nothing", romCache)->fromPack);
    CHECK(!assets::loadAssets(scratch / "nothing", scratch / "absent.bin").has_value());
    std::printf("pack: used while its sources are unchanged, made again when TEXIMAGE changed\n");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const std::string mode = argc > 1 ? argv[1] : "";
        if (mode == "ports" && argc == 5) return ports(argv[2], argv[3], argv[4]);
        if (mode == "textures" && argc == 8) return textures(argv[2], argv[3], argv[4], argv[5], argv[6], argv[7]);
        if (mode == "pack" && argc == 5) return pack(argv[2], argv[3], argv[4]);
        std::fprintf(stderr, "usage: AssetsTest ports <model dir> <resource folder> <bios.bin>\n"
                             "       AssetsTest textures <model dir> <resource folder> <bios.bin> <png dir> <rod-mesh.json> <scratch>\n"
                             "       AssetsTest pack <resource folder> <bios.bin> <scratch>\n");
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "exception: %s\n", e.what());
        return 1;
    }
}
