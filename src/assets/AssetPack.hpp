#pragma once
#include <filesystem>
#include <optional>

#include "assets/Resources.hpp"

namespace assets {

// assets.bin, the decoded cache: header {magic "CCVKPACK", u32 version, u32 sources, u32 entries, u32 0, u64 decoder}, the
// sources {u64 size, u64 hash, u32 name length, u32 path length, name, path, padded to 8}, the entries
// {char name[16], u32 kind, u32 width, u32 height, u32 source, u64 offset, u64 size, u64 source hash, u64 0}, then the
// blobs, each at a multiple of 16. Little-endian.
inline constexpr uint32_t kPackVersion = 2;

Bytes packBytes(const AssetSet& set);
// nullopt when the bytes are not a pack of this version.
std::optional<AssetSet> unpack(View bytes);
void writePack(const std::filesystem::path& path, const AssetSet& set);
std::optional<AssetSet> readPack(const std::filesystem::path& path);

struct LoadedAssets {
    AssetSet set;
    bool fromPack = false;
    double milliseconds = 0;
    std::string warning;
};
// What the decode is: changes when the decoder or its constants change, so a pack of another decoder is not used.
uint64_t decoderFingerprint();
// The pack when its sources equal the folder's files (or the folder has none), else the folder decoded and the
// pack written again. nullopt when neither gives anything.
std::optional<LoadedAssets> loadAssets(const std::filesystem::path& folder, const std::filesystem::path& pack);
// %LOCALAPPDATA%/CrystalClockVK (or $XDG_CACHE_HOME, ~/.cache), else ./cache.
std::filesystem::path userDataDirectory();

}
