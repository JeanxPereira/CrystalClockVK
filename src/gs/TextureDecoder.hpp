#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>
#include "SwizzleEngine.hpp"
#include "GsRegisterState.hpp"

// Decodes raw binary texture data dumped from PS2 VRAM into linear RGBA8.
// Handles GS swizzle layout and CLUT expansion for indexed formats.
// No Vulkan dependencies — output is ready for ResourceManager::uploadToImage().
class TextureDecoder {
public:
    // Decode a raw binary texture file from PS2 VRAM dump.
    // The file contains raw swizzled pixel data.
    //
    // Returns: linear RGBA8 data (width × height × 4 bytes)
    static std::vector<uint8_t> decodeBinTexture(
        const std::filesystem::path& path,
        int width, int height,
        GsPixelFormat format,
        const uint8_t* clut = nullptr);

    // Decode from a memory buffer (for embedded data or VRAM reads)
    static std::vector<uint8_t> decodeFromMemory(
        const uint8_t* data, size_t dataSize,
        int width, int height,
        GsPixelFormat format,
        const uint8_t* clut = nullptr);

    // Utility: compute expected raw data size for a given format and dimensions
    static size_t expectedSize(int width, int height, GsPixelFormat format);

    // TEXA alpha expansion (GS GSLocalMemory). Input/output u32 are packed
    // R(0..7) G(8..15) B(16..23) A(24..31) — little-endian byte order R,G,B,A.
    // expand24To32: 24-bit RGB (stored alpha ignored) -> RGBA via TEXA.
    static uint32_t expand24To32(uint32_t c, const GsTexa& texa);
    // expand16To32: 16-bit 5551 -> RGBA via TEXA (TA0/TA1).
    static uint32_t expand16To32(uint16_t c, const GsTexa& texa);
};
