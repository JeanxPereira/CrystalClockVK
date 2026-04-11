#include "SwizzleEngine.hpp"
#include <cstring>
#include <stdexcept>

// ──────────────────────────────────────────────────────────────────────────────
// GS Block Tables — from PCSX2/GSdx source (GPLv2)
// These define how blocks are arranged within a page for each pixel format.
// ──────────────────────────────────────────────────────────────────────────────

const int SwizzleEngine::s_blockTablePSMCT32[4][8] = {
    {  0,  1,  4,  5, 16, 17, 20, 21 },
    {  2,  3,  6,  7, 18, 19, 22, 23 },
    {  8,  9, 12, 13, 24, 25, 28, 29 },
    { 10, 11, 14, 15, 26, 27, 30, 31 },
};

const int SwizzleEngine::s_blockTablePSMT8[4][8] = {
    {  0,  1,  4,  5, 16, 17, 20, 21 },
    {  2,  3,  6,  7, 18, 19, 22, 23 },
    {  8,  9, 12, 13, 24, 25, 28, 29 },
    { 10, 11, 14, 15, 26, 27, 30, 31 },
};

// Column table for PSMCT32: pixel ordering within an 8×8 block (32-bit pixels)
const int SwizzleEngine::s_columnTablePSMCT32[8][8] = {
    {  0,  1,  4,  5,  8,  9, 12, 13 },
    {  2,  3,  6,  7, 10, 11, 14, 15 },
    { 16, 17, 20, 21, 24, 25, 28, 29 },
    { 18, 19, 22, 23, 26, 27, 30, 31 },
    { 32, 33, 36, 37, 40, 41, 44, 45 },
    { 34, 35, 38, 39, 42, 43, 46, 47 },
    { 48, 49, 52, 53, 56, 57, 60, 61 },
    { 50, 51, 54, 55, 58, 59, 62, 63 },
};

// Column table for PSMT8: pixel ordering within a 16×16 block (8-bit pixels)
const int SwizzleEngine::s_columnTablePSMT8[16][16] = {
    {   0,   4,  16,  20,  32,  36,  48,  52,   2,   6,  18,  22,  34,  38,  50,  54 },
    {   8,  12,  24,  28,  40,  44,  56,  60,  10,  14,  26,  30,  42,  46,  58,  62 },
    {  33,  37,  49,  53,   1,   5,  17,  21,  35,  39,  51,  55,   3,   7,  19,  23 },
    {  41,  45,  57,  61,   9,  13,  25,  29,  43,  47,  59,  63,  11,  15,  27,  31 },
    {  96, 100, 112, 116,  64,  68,  80,  84,  98, 102, 114, 118,  66,  70,  82,  86 },
    { 104, 108, 120, 124,  72,  76,  88,  92, 106, 110, 122, 126,  74,  78,  90,  94 },
    {  65,  69,  81,  85,  97, 101, 113, 117,  67,  71,  83,  87,  99, 103, 115, 119 },
    {  73,  77,  89,  93, 105, 109, 121, 125,  75,  79,  91,  95, 107, 111, 123, 127 },
    { 128, 132, 144, 148, 160, 164, 176, 180, 130, 134, 146, 150, 162, 166, 178, 182 },
    { 136, 140, 152, 156, 168, 172, 184, 188, 138, 142, 154, 158, 170, 174, 186, 190 },
    { 161, 165, 177, 181, 129, 133, 145, 149, 163, 167, 179, 183, 131, 135, 147, 151 },
    { 169, 173, 185, 189, 137, 141, 153, 157, 171, 175, 187, 191, 139, 143, 155, 159 },
    { 224, 228, 240, 244, 192, 196, 208, 212, 226, 230, 242, 246, 194, 198, 210, 214 },
    { 232, 236, 248, 252, 200, 204, 216, 220, 234, 238, 250, 254, 202, 206, 218, 222 },
    { 193, 197, 209, 213, 225, 229, 241, 245, 195, 199, 211, 215, 227, 231, 243, 247 },
    { 201, 205, 217, 221, 233, 237, 249, 253, 203, 207, 219, 223, 235, 239, 251, 255 },
};

// ──────────────────────────────────────────────────────────────────────────────
// Address calculations
// ──────────────────────────────────────────────────────────────────────────────

uint32_t SwizzleEngine::psmct32Address(int x, int y, int bufferWidth) {
    // Buffer width in pages (each page is 64 pixels wide for PSMCT32)
    int pageWidth = bufferWidth / PSMCT32_PAGE_WIDTH;

    // Which page does this pixel fall in?
    int pageX = x / PSMCT32_PAGE_WIDTH;
    int pageY = y / PSMCT32_PAGE_HEIGHT;
    int page = pageY * pageWidth + pageX;

    // Position within the page
    int localX = x % PSMCT32_PAGE_WIDTH;
    int localY = y % PSMCT32_PAGE_HEIGHT;

    // Which block within the page?
    int blockX = localX / PSMCT32_BLOCK_WIDTH;
    int blockY = localY / PSMCT32_BLOCK_HEIGHT;
    int block = s_blockTablePSMCT32[blockY][blockX];

    // Position within the block
    int colX = localX % PSMCT32_BLOCK_WIDTH;
    int colY = localY % PSMCT32_BLOCK_HEIGHT;
    int column = s_columnTablePSMCT32[colY][colX];

    // Final address: (page * 2048 + block * 64 + column) * 4 bytes
    return static_cast<uint32_t>((page * 2048 + block * 64 + column) * 4);
}

uint32_t SwizzleEngine::psmt8Address(int x, int y, int bufferWidth) {
    int pageWidth = bufferWidth / PSMT8_PAGE_WIDTH;

    int pageX = x / PSMT8_PAGE_WIDTH;
    int pageY = y / PSMT8_PAGE_HEIGHT;
    int page = pageY * pageWidth + pageX;

    int localX = x % PSMT8_PAGE_WIDTH;
    int localY = y % PSMT8_PAGE_HEIGHT;

    int blockX = localX / PSMT8_BLOCK_WIDTH;
    int blockY = localY / PSMT8_BLOCK_HEIGHT;
    int block = s_blockTablePSMT8[blockY][blockX];

    int colX = localX % PSMT8_BLOCK_WIDTH;
    int colY = localY % PSMT8_BLOCK_HEIGHT;
    int column = s_columnTablePSMT8[colY][colX];

    // PSMT8: (page * 2048 + block * 256 + column) * 1 byte
    return static_cast<uint32_t>(page * 2048 * 4 + block * 256 + column);
}

uint32_t SwizzleEngine::pixelAddress(int x, int y, int bufferWidth, GsPixelFormat format) {
    switch (format) {
        case GsPixelFormat::PSMCT32: return psmct32Address(x, y, bufferWidth);
        case GsPixelFormat::PSMT8:   return psmt8Address(x, y, bufferWidth);
        default:
            throw std::runtime_error("Unsupported pixel format for address calculation");
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// De-swizzle: GS VRAM layout → linear RGBA8
// ──────────────────────────────────────────────────────────────────────────────

std::vector<uint8_t> SwizzleEngine::deswizzle(
    const uint8_t* src, int width, int height,
    GsPixelFormat format, const uint8_t* clut) {

    std::vector<uint8_t> output(width * height * 4);

    switch (format) {
        case GsPixelFormat::PSMCT32:
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    uint32_t addr = psmct32Address(x, y, width);
                    uint32_t dstIdx = static_cast<uint32_t>((y * width + x) * 4);
                    // Copy RGBA directly (GS stores as RGBA32)
                    std::memcpy(&output[dstIdx], &src[addr], 4);
                }
            }
            break;

        case GsPixelFormat::PSMT8:
            if (!clut) {
                throw std::runtime_error("PSMT8 deswizzle requires a CLUT");
            }
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    uint32_t addr = psmt8Address(x, y, width);
                    uint8_t index = src[addr];
                    uint32_t dstIdx = static_cast<uint32_t>((y * width + x) * 4);
                    // Look up CLUT color (256 entries × 4 bytes each)
                    std::memcpy(&output[dstIdx], &clut[index * 4], 4);
                }
            }
            break;

        default:
            throw std::runtime_error("Unsupported pixel format for deswizzle");
    }

    return output;
}

// ──────────────────────────────────────────────────────────────────────────────
// Swizzle: linear → GS VRAM layout (for testing/encoding)
// ──────────────────────────────────────────────────────────────────────────────

std::vector<uint8_t> SwizzleEngine::swizzle(
    const uint8_t* src, int width, int height,
    GsPixelFormat format) {

    // Calculate output size based on the maximum address we'll write
    size_t maxAddr = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint32_t addr = pixelAddress(x, y, width, format);
            size_t endAddr = addr;
            if (format == GsPixelFormat::PSMCT32) endAddr += 4;
            else endAddr += 1;
            if (endAddr > maxAddr) maxAddr = endAddr;
        }
    }

    std::vector<uint8_t> output(maxAddr, 0);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint32_t addr = pixelAddress(x, y, width, format);
            if (format == GsPixelFormat::PSMCT32) {
                uint32_t srcIdx = static_cast<uint32_t>((y * width + x) * 4);
                std::memcpy(&output[addr], &src[srcIdx], 4);
            } else if (format == GsPixelFormat::PSMT8) {
                uint32_t srcIdx = static_cast<uint32_t>(y * width + x);
                output[addr] = src[srcIdx];
            }
        }
    }

    return output;
}
