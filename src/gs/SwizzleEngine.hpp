#pragma once

#include <cstdint>
#include <vector>

#include "GsRegisterState.hpp"  // GsTexa

// PS2 GS pixel storage formats used by OSDSYS Crystal Clock
enum class GsPixelFormat {
    PSMCT32,  // 32-bit RGBA (page: 64×32, block: 8×8)
    PSMCT24,  // 24-bit RGB, same swizzle as PSMCT32; alpha expanded via TEXA (0x80 default)
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
    // PSMT4: address in NIBBLES (page 128x128, block 32x16, 512 nibbles/block;
    // even nibble = low 4 bits of the byte, matching PCSX2 ReadPixel4).
    static uint32_t psmt4NibbleAddress(int x, int y, int bufferWidth);

    // TEXA alpha expansion (GS GSLocalMemory). Input/output u32 are packed
    // R(0..7) G(8..15) B(16..23) A(24..31) — little-endian byte order R,G,B,A.
    // expand24To32: 24-bit RGB (stored alpha ignored) -> RGBA via TEXA.
    static uint32_t expand24To32(uint32_t c, const GsTexa& texa);
    // expand16To32: 16-bit 5551 -> RGBA via TEXA (TA0/TA1).
    static uint32_t expand16To32(uint16_t c, const GsTexa& texa);

    // De-swizzle an entire texture from GS VRAM layout to linear RGBA.
    // Input: raw swizzled bytes from VRAM
    // Output: linear RGBA8 pixel data (width × height × 4 bytes)
    //
    // For indexed formats (PSMT8), a CLUT (Color Lookup Table) must be provided.
    // The CLUT is expected as 256 × 4 bytes (RGBA32).
    //
    // `bufferWidth` is the GS buffer stride in pixels (TBW*64), used for swizzle
    // addressing; the output is `width` × `height` (the texture dimensions, 2^TW × 2^TH).
    //
    // `texa` drives alpha for the formats with no full stored alpha: PSMCT24
    // expands via expand24To32 (this is the GS texture read, ReadTexel24). The
    // default {0x80,0x80,AEM=0} yields a constant 0x80 — the display ReadFrame24
    // behaviour — so existing frame-read callers are unchanged.
    static std::vector<uint8_t> deswizzle(
        const uint8_t* src,
        int width, int height, int bufferWidth,
        GsPixelFormat format,
        const uint8_t* clut = nullptr,
        const GsTexa& texa = {0x80, 0x80, false});

    // Swizzle linear pixel data into GS VRAM layout (write-back / encoding / testing).
    // `bufferWidth` is the GS buffer stride in pixels (TBW*64), matching deswizzle.
    static std::vector<uint8_t> swizzle(
        const uint8_t* src,
        int width, int height, int bufferWidth,
        GsPixelFormat format);

    // Swizzle a linear RGBA region back into an existing VRAM buffer in place
    // (the GS write-back: framebuffer -> unified memory). Writes only the
    // width×height pixels at their swizzled addresses; other bytes untouched.
    static void swizzleInto(
        uint8_t* vram, size_t vramSize,
        const uint8_t* src,
        int width, int height, int bufferWidth,
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

    // PSMT4 tables (page 128x128, block 32x16, 4x8 blocks/page) — GSTables.cpp
    static const int s_blockTablePSMT4[8][4];
    static const int s_columnTablePSMT4[16][32];

    // Column tables for sub-block pixel ordering
    static const int s_columnTablePSMCT32[8][8];
    static const int s_columnTablePSMT8[16][16];
};
