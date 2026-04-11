#pragma once

#include <cstdint>
#include <vector>

// PS2 GS pixel storage formats used by OSDSYS Crystal Clock
enum class GsPixelFormat {
    PSMCT32,  // 32-bit RGBA (page: 64×32, block: 8×8)
    PSMT8,    // 8-bit indexed (page: 128×64, block: 16×16)
    PSMT4,    // 4-bit indexed (page: 128×128, block: 32×16)
};

// PS2 GS texture swizzle engine.
// The GS stores textures in a page/block/column layout optimized for 2D locality.
// This engine converts between swizzled GS addresses and linear (x,y) coordinates.
//
// Based on the PCSX2/GSdx reference implementation of the GS memory layout.
// No Vulkan dependencies — pure math, fully unit-testable.
class SwizzleEngine {
public:
    // Convert (x, y) pixel coordinates to a linear byte offset in VRAM
    // for a given pixel format and buffer width (in pixels).
    static uint32_t pixelAddress(int x, int y, int bufferWidth, GsPixelFormat format);

    // Specific format implementations
    static uint32_t psmct32Address(int x, int y, int bufferWidth);
    static uint32_t psmt8Address(int x, int y, int bufferWidth);

    // De-swizzle an entire texture from GS VRAM layout to linear RGBA.
    // Input: raw swizzled bytes from VRAM
    // Output: linear RGBA8 pixel data (width × height × 4 bytes)
    //
    // For indexed formats (PSMT8), a CLUT (Color Lookup Table) must be provided.
    // The CLUT is expected as 256 × 4 bytes (RGBA32).
    static std::vector<uint8_t> deswizzle(
        const uint8_t* src,
        int width, int height,
        GsPixelFormat format,
        const uint8_t* clut = nullptr);

    // Swizzle linear pixel data into GS VRAM layout (for encoding/testing).
    static std::vector<uint8_t> swizzle(
        const uint8_t* src,
        int width, int height,
        GsPixelFormat format);

private:
    // GS page/block geometry constants
    // PSMCT32: page = 64×32 pixels, block = 8×8 pixels, 32 blocks/page
    // PSMT8:   page = 128×64 pixels, block = 16×16 pixels, 32 blocks/page

    // Block arrangement within a page (PSMCT32: 8×4 blocks)
    static constexpr int PSMCT32_PAGE_WIDTH = 64;
    static constexpr int PSMCT32_PAGE_HEIGHT = 32;
    static constexpr int PSMCT32_BLOCK_WIDTH = 8;
    static constexpr int PSMCT32_BLOCK_HEIGHT = 8;

    // Block arrangement within a page (PSMT8: 8×4 blocks of 16×16)
    static constexpr int PSMT8_PAGE_WIDTH = 128;
    static constexpr int PSMT8_PAGE_HEIGHT = 64;
    static constexpr int PSMT8_BLOCK_WIDTH = 16;
    static constexpr int PSMT8_BLOCK_HEIGHT = 16;

    // PSMCT32 block table (GSdx reference: block arrangement within a page)
    static const int s_blockTablePSMCT32[4][8];

    // PSMT8 block table
    static const int s_blockTablePSMT8[4][8];

    // Column tables for sub-block pixel ordering
    static const int s_columnTablePSMCT32[8][8];
    static const int s_columnTablePSMT8[16][16];
};
