#include "TextureDecoder.hpp"
#include <fstream>
#include <stdexcept>

std::vector<uint8_t> TextureDecoder::decodeBinTexture(
    const std::filesystem::path& path,
    int width, int height,
    GsPixelFormat format,
    const uint8_t* clut) {

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open texture file: " + path.string());
    }

    auto fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0);

    std::vector<uint8_t> rawData(fileSize);
    file.read(reinterpret_cast<char*>(rawData.data()), static_cast<std::streamsize>(fileSize));

    return decodeFromMemory(rawData.data(), rawData.size(), width, height, format, clut);
}

std::vector<uint8_t> TextureDecoder::decodeFromMemory(
    const uint8_t* data, size_t dataSize,
    int width, int height,
    GsPixelFormat format,
    const uint8_t* clut) {

    // Validate minimum data size
    size_t expected = expectedSize(width, height, format);
    if (dataSize < expected) {
        throw std::runtime_error(
            "Texture data too small: got " + std::to_string(dataSize) +
            " bytes, expected at least " + std::to_string(expected));
    }

    return SwizzleEngine::deswizzle(data, width, height, width, format, clut);
}

// GS GSLocalMemory::Expand24To32: alpha = (AEM==0 || RGB!=0) ? TA0 : 0.
uint32_t TextureDecoder::expand24To32(uint32_t c, const GsTexa& texa) {
    return (((!texa.aem | (c & 0xffffff)) ? texa.ta0 : 0u) << 24) | (c & 0xffffff);
}

// GS GSLocalMemory::Expand16To32: 5551 -> 8888 (no low-bit replication),
// alpha = bit15 ? TA1 : (AEM==0 || c!=0) ? TA0 : 0.
uint32_t TextureDecoder::expand16To32(uint16_t c, const GsTexa& texa) {
    return (((c & 0x8000) ? texa.ta1 : (!texa.aem | c) ? texa.ta0 : 0u) << 24)
        | ((c & 0x7c00) << 9)
        | ((c & 0x03e0) << 6)
        | ((c & 0x001f) << 3);
}

size_t TextureDecoder::expectedSize(int width, int height, GsPixelFormat format) {
    switch (format) {
        case GsPixelFormat::PSMCT32:
            return static_cast<size_t>(width) * height * 4;
        case GsPixelFormat::PSMT8:
            return static_cast<size_t>(width) * height;
        case GsPixelFormat::PSMT4:
            return static_cast<size_t>(width) * height / 2;
        default:
            throw std::runtime_error("Unsupported pixel format for expectedSize");
    }
}
