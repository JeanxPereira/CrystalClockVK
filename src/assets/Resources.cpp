#include "assets/Resources.hpp"

#include <algorithm>
#include <cstring>
#include <optional>
#include <utility>

#include "assets/ClockTextures.hpp"
#include "assets/ElfImage.hpp"
#include "assets/Expand.hpp"
#include "assets/ImageCipher.hpp"
#include "assets/Romdir.hpp"

namespace fs = std::filesystem;

namespace assets {

namespace {

// The files the decode reads, in this order.
constexpr std::array<std::string_view, 3> kDecoded{"TEXIMAGE", "FNTOSD", kProgramName};

// The rod mesh in HDD OSD 1.10U's data (facts/data/rod-mesh.json): 64 positions, 16 normals, 64 texture coordinates.
constexpr uint32_t kFaces = 16, kPositions = 0x002b4b90, kNormals = 0x002b5390, kCoordinates = 0x002b4f90;

struct ReadFile {
    SourceFile source;
    Bytes data;
};

std::vector<ReadFile> readSources(const fs::path& folder) {
    std::vector<ReadFile> files;
    std::error_code error;
    for (std::string_view name : kDecoded) {
        const fs::path path = folder / name;
        if (!fs::is_regular_file(path, error)) continue;
        Bytes data = readFile(path);
        SourceFile source{std::string(name), fs::absolute(path).generic_string(), data.size(), hashOf(data)};
        files.push_back({std::move(source), std::move(data)});
    }
    return files;
}

}  // namespace

const Asset* AssetSet::find(std::string_view name) const {
    for (const Asset& a : assets)
        if (a.name == name) return &a;
    return nullptr;
}

std::vector<SourceFile> folderSources(const fs::path& folder) {
    std::vector<SourceFile> sources;
    for (ReadFile& f : readSources(folder)) sources.push_back(std::move(f.source));
    return sources;
}

AssetSet decodeFolder(const fs::path& folder) {
    const std::vector<ReadFile> files = readSources(folder);
    AssetSet set;
    const auto index = [&](std::string_view name) -> std::optional<uint32_t> {
        for (uint32_t i = 0; i < files.size(); ++i)
            if (files[i].source.name == name) return i;
        return std::nullopt;
    };
    for (const ReadFile& f : files) set.sources.push_back(f.source);
    const auto texImage = index("TEXIMAGE");
    if (!texImage) throw std::runtime_error("no TEXIMAGE in " + folder.string());
    const auto program = index(kProgramName);
    std::optional<ElfImage> elf;
    if (program) elf.emplace(files[*program].data);

    // A ROM's TEXIMAGE is a plain ROMDIR archive; an installed HDD OSD's is encrypted (facts/hddosd-boot.md).
    Bytes container = files[*texImage].data;
    if (romdirStart(container) < 0) {
        if (!elf) throw std::runtime_error("TEXIMAGE is encrypted: its hddosd.elf is needed beside it");
        container = decryptImage(container, CipherTables::fromProgram(*elf));
        if (romdirStart(container) < 0) throw std::runtime_error("TEXIMAGE holds no directory, plain or decrypted with the tables of hddosd.elf");
    }
    const auto entries = romdirEntries(container, size_t(romdirStart(container)));
    for (const ClockTextureInfo& info : kClockTextures) {
        const Expanded raw = expand(romdirMember(container, entries, std::string(info.name)));
        set.assets.push_back({std::string(info.name), AssetKind::TextureRgba32, info.width, info.height, *texImage, convertTexture(raw.out, info.width, info.height, info.form)});
    }
    if (const auto font = index("FNTOSD")) set.assets.push_back({"FNTOSD", AssetKind::Font, 0, 0, *font, files[*font].data});
    if (program) {
        set.assets.push_back({"PROGRAM", AssetKind::Program, 0, 0, *program, files[*program].data});
        Bytes mesh;
        for (const auto& [address, count] : {std::pair{kPositions, kFaces * 4}, std::pair{kNormals, kFaces}, std::pair{kCoordinates, kFaces * 4}}) {
            const View v = elf->bytes(address, count * 16);
            mesh.insert(mesh.end(), v.begin(), v.end());
        }
        set.assets.push_back({"RODMESH", AssetKind::Mesh, kFaces, 0, *program, std::move(mesh)});
    }
    return set;
}

std::vector<std::string> extractBios(const fs::path& rom, const fs::path& folder) {
    const Bytes image = readFile(rom);
    const int64_t start = romdirStart(image, 0x100000);
    if (start < 0) throw std::runtime_error(rom.string() + " is not a BIOS ROM image: no ROMDIR");
    const auto entries = romdirEntries(image, size_t(start));
    std::vector<std::string> names;
    for (std::string_view name : kResourceNames) {
        if (!romdirFind(entries, std::string(name))) continue;
        const View member = romdirMember(image, entries, std::string(name));
        const fs::path path = folder / name;
        if (fs::exists(path)) {
            const Bytes there = readFile(path);
            if (there.size() != member.size() || !std::equal(member.begin(), member.end(), there.begin()))
                throw std::runtime_error(path.string() + " exists with other bytes: not replaced");
        } else {
            writeFile(path, member);
        }
        names.emplace_back(name);
    }
    if (names.empty()) throw std::runtime_error(rom.string() + " holds none of the OSD's resource files");
    return names;
}

}
