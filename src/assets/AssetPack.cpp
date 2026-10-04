#include "assets/AssetPack.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace fs = std::filesystem;

namespace assets {

namespace {

constexpr char kMagic[8] = {'C', 'C', 'V', 'K', 'P', 'A', 'C', 'K'};
constexpr size_t kHeader = 32, kEntry = 64, kName = 16;

void put32(Bytes& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
}
void put64(Bytes& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(uint8_t(v >> (8 * i)));
}
void pad(Bytes& out, size_t to) { out.resize((out.size() + to - 1) / to * to, 0); }

bool sameSources(const std::vector<SourceFile>& a, const std::vector<SourceFile>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].name != b[i].name || a[i].size != b[i].size || a[i].hash != b[i].hash) return false;
    return true;
}

}  // namespace

Bytes packBytes(const AssetSet& set) {
    Bytes out(kMagic, kMagic + 8);
    put32(out, kPackVersion);
    put32(out, uint32_t(set.sources.size()));
    put32(out, uint32_t(set.assets.size()));
    put32(out, 0);
    put64(out, set.decoder);
    for (const SourceFile& s : set.sources) {
        put64(out, s.size);
        put64(out, s.hash);
        put32(out, uint32_t(s.name.size()));
        put32(out, uint32_t(s.path.size()));
        out.insert(out.end(), s.name.begin(), s.name.end());
        out.insert(out.end(), s.path.begin(), s.path.end());
        pad(out, 8);
    }
    const size_t table = out.size();
    out.resize(table + kEntry * set.assets.size(), 0);
    pad(out, 16);
    for (size_t i = 0; i < set.assets.size(); ++i) {
        const Asset& a = set.assets[i];
        if (a.name.size() > kName) throw std::runtime_error("pack: the name " + a.name + " is longer than 16");
        if (a.source >= set.sources.size()) throw std::runtime_error("pack: " + a.name + " has no source");
        const uint64_t offset = out.size();
        out.insert(out.end(), a.data.begin(), a.data.end());
        pad(out, 16);
        Bytes entry(a.name.begin(), a.name.end());
        entry.resize(kName, 0);
        put32(entry, uint32_t(a.kind));
        put32(entry, a.width);
        put32(entry, a.height);
        put32(entry, a.source);
        put64(entry, offset);
        put64(entry, a.data.size());
        put64(entry, set.sources[a.source].hash);
        put64(entry, 0);
        std::memcpy(out.data() + table + kEntry * i, entry.data(), kEntry);
    }
    return out;
}

std::optional<AssetSet> unpack(View bytes) {
    try {
        if (bytes.size() < kHeader || std::memcmp(bytes.data(), kMagic, 8) != 0 || le32(bytes, 8) != kPackVersion) return std::nullopt;
        const uint32_t sources = le32(bytes, 12), entries = le32(bytes, 16);
        AssetSet set;
        set.decoder = le64(bytes, 24);
        size_t at = kHeader;
        for (uint32_t i = 0; i < sources; ++i) {
            SourceFile s;
            s.size = le64(bytes, at);
            s.hash = le64(bytes, at + 8);
            const uint32_t nameLength = le32(bytes, at + 16), pathLength = le32(bytes, at + 20);
            at += 24;
            if (at + size_t(nameLength) + pathLength > bytes.size()) return std::nullopt;
            s.name.assign(reinterpret_cast<const char*>(bytes.data() + at), nameLength);
            s.path.assign(reinterpret_cast<const char*>(bytes.data() + at + nameLength), pathLength);
            at = (at + nameLength + pathLength + 7) / 8 * 8;
            set.sources.push_back(std::move(s));
        }
        for (uint32_t i = 0; i < entries; ++i, at += kEntry) {
            if (at + kEntry > bytes.size()) return std::nullopt;
            Asset a;
            size_t length = 0;
            while (length < kName && bytes[at + length] != 0) ++length;
            a.name.assign(reinterpret_cast<const char*>(bytes.data() + at), length);
            a.kind = AssetKind(le32(bytes, at + 16));
            a.width = le32(bytes, at + 20);
            a.height = le32(bytes, at + 24);
            a.source = le32(bytes, at + 28);
            const uint64_t offset = le64(bytes, at + 32), size = le64(bytes, at + 40), hash = le64(bytes, at + 48);
            if (a.source >= set.sources.size() || set.sources[a.source].hash != hash || offset > bytes.size() || size > bytes.size() - offset) return std::nullopt;
            a.data.assign(bytes.begin() + std::ptrdiff_t(offset), bytes.begin() + std::ptrdiff_t(offset + size));
            set.assets.push_back(std::move(a));
        }
        return set;
    } catch (const std::runtime_error&) {
        return std::nullopt;
    }
}

void writePack(const fs::path& path, const AssetSet& set) { writeFile(path, packBytes(set)); }

std::optional<AssetSet> readPack(const fs::path& path) {
    std::error_code error;
    if (!fs::is_regular_file(path, error)) return std::nullopt;
    return unpack(readFile(path));
}

std::optional<LoadedAssets> loadAssets(const fs::path& folder, const fs::path& pack) {
    const auto begin = std::chrono::steady_clock::now();
    const auto done = [&](AssetSet set, bool fromPack) {
        return LoadedAssets{std::move(set), fromPack, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count()};
    };
    std::optional<AssetSet> cached = readPack(pack);
    if (cached && cached->decoder != decoderFingerprint()) cached.reset();
    // A folder that cannot be read or decoded falls back to the cache, then to nothing (the caller's loose files).
    try {
        const std::vector<SourceFile> sources = folder.empty() ? std::vector<SourceFile>{} : folderSources(folder);
        if (sources.empty()) return cached ? std::optional(done(std::move(*cached), true)) : std::nullopt;
        if (cached && sameSources(cached->sources, sources)) return done(std::move(*cached), true);
        AssetSet set = decodeFolder(folder);
        try {
            writePack(pack, set);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "assets: the cache %s was not written: %s\n", pack.string().c_str(), e.what());
        }
        return done(std::move(set), false);
    } catch (const std::runtime_error& e) {
        if (!cached) {
            std::fprintf(stderr, "assets: %s: decode failed (%s), and no cache\n", folder.string().c_str(), e.what());
            return std::nullopt;
        }
        LoadedAssets loaded = done(std::move(*cached), true);
        loaded.warning = folder.string() + ": decode failed (" + e.what() + "), the cache " + pack.string() + " is used";
        std::fprintf(stderr, "assets: %s\n", loaded.warning.c_str());
        return loaded;
    }
}

fs::path userDataDirectory() {
    const auto variable = [](const char* name) -> fs::path {
#ifdef _WIN32
        char* value = nullptr;
        size_t length = 0;
        if (_dupenv_s(&value, &length, name) != 0 || !value) return {};
        fs::path path = value;
        std::free(value);
        return path;
#else
        const char* value = std::getenv(name);
        return value ? fs::path(value) : fs::path();
#endif
    };
    if (fs::path base = variable("LOCALAPPDATA"); !base.empty()) return base / "CrystalClockVK";
    if (fs::path base = variable("XDG_CACHE_HOME"); !base.empty()) return base / "CrystalClockVK";
    if (fs::path base = variable("HOME"); !base.empty()) return base / ".cache" / "CrystalClockVK";
    return fs::current_path() / "cache";
}

}
