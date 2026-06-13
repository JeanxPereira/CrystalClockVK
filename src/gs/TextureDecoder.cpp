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
