// Code-parity test: SwizzleEngine::psmct32Address must equal PCSX2's PSMCT32
// pixel address for every (x, y) over several buffer widths.
//
// The oracle below is a faithful port of PCSX2 GSLocalMemory's swizzle path:
//   _blockTable32 + columnTable32 (GSTables.cpp) -> pxOffset (col/row offset
//   tables) -> GSOffset::pa (pixelAddressZeroXRaw + row lookup). It returns a
//   32-bit WORD address; our psmct32Address returns a BYTE offset (= word * 4).
//
// Reference: pcsx2-ref/pcsx2/GS/{GSTables.cpp,GSLocalMemory.h}. GPLv3 (logic
// reproduced for parity verification only).

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "gs/SwizzleEngine.hpp"

namespace pcsx2 {

// GSTables.cpp: _blockTable32[4][8] and columnTable32[8][8] (exact).
static const int kBlockTable32[4][8] = {
    {  0,  1,  4,  5, 16, 17, 20, 21 },
    {  2,  3,  6,  7, 18, 19, 22, 23 },
    {  8,  9, 12, 13, 24, 25, 28, 29 },
    { 10, 11, 14, 15, 26, 27, 30, 31 },
};

static const int kColumnTable32[8][8] = {
    {  0,  1,  4,  5,  8,  9, 12, 13 },
    {  2,  3,  6,  7, 10, 11, 14, 15 },
    { 16, 17, 20, 21, 24, 25, 28, 29 },
    { 18, 19, 22, 23, 26, 27, 30, 31 },
    { 32, 33, 36, 37, 40, 41, 44, 45 },
    { 34, 35, 38, 39, 42, 43, 46, 47 },
    { 48, 49, 52, 53, 56, 57, 60, 61 },
    { 50, 51, 54, 55, 58, 59, 62, 63 },
};

// GSTables.cpp pxOffset() specialised for PSMCT32:
//   BlocksHigh=4, BlocksWide=8, ColHeight=8, ColWidth=8.
static int pxOffset32(int x, int y) {
    const int blockSize = 8 * 8;        // ColHeight * ColWidth
    const int pageSize = blockSize * 4 * 8;  // * BlocksHigh * BlocksWide = 2048
    const int pageWidth = 8 * 8;        // BlocksWide * ColWidth = 64
    const int pageX = x / pageWidth;
    const int subpageX = x % pageWidth;
    const int blockID = kBlockTable32[y / 8][subpageX / 8];
    const int sublockOffset = kColumnTable32[y % 8][subpageX % 8];
    return pageX * pageSize + blockID * blockSize + sublockOffset;
}

// GSTables.cpp makeColOffsetTable() / makeRowOffsetTable() for PSMCT32.
// pixelColOffset32[y] = pxOffset32(0, y), y in [0,32).
static int colOffset32(int y) { return pxOffset32(0, y); }
// pixelRowOffset32[x] = pxOffset32(x % 2048, 0) - pxOffset32(0, 0).
static int rowOffset32(int x) { return pxOffset32(x % 2048, 0) - pxOffset32(0, 0); }

// GSLocalMemory.h GSOffset::pa() for PSMCT32 (bp=0, non-Z so both xors are 0).
//   pageShiftX=6, pageShiftY=5, pageMask.y=31, bwPg=bw (page units), GS_MAX_PAGES=512.
// Returns a 32-bit word address.
static uint32_t pixelAddress32(int x, int y, int bwPages) {
    int base = 0;                                       // bp << (6+5-5), bp=0
    base += ((y & ~31) * bwPages) << 6;                 // pages in y
    base &= (512 << 11) - 1;                            // GS_MAX_PAGES mask
    base += colOffset32(y & 31);                        // y within page
    return static_cast<uint32_t>(base + rowOffset32(x));
}

}  // namespace pcsx2

namespace {

int g_fails = 0;
int g_checked = 0;

void compareWidth(int bufferWidth, int height) {
    const int bwPages = bufferWidth / 64;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < bufferWidth; x++) {
            const uint32_t oursByte = SwizzleEngine::psmct32Address(x, y, bufferWidth);
            const uint32_t refWord = pcsx2::pixelAddress32(x, y, bwPages);
            g_checked++;
            if (oursByte != refWord * 4) {
                if (g_fails < 16) {
                    std::printf("  FAIL bw=%d (%d,%d): ours=%u word=%u (PCSX2 word=%u)\n",
                                bufferWidth, x, y, oursByte, oursByte / 4, refWord);
                }
                g_fails++;
            }
        }
    }
}

}  // namespace

int main() {
    // Buffer widths: 1, 2, and 10 pages (64/128/640 px). Heights span >1 page.
    compareWidth(64, 96);
    compareWidth(128, 96);
    compareWidth(640, 224);  // GS framebuffer dims

    if (g_fails) {
        std::printf("psmct32 address parity: %d/%d MISMATCHES\n", g_fails, g_checked);
        return 1;
    }
    std::printf("psmct32 address parity: OK (%d pixels match PCSX2)\n", g_checked);
    return 0;
}
