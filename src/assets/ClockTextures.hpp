#pragma once
#include <array>
#include <string_view>

#include "assets/Bytes.hpp"

namespace assets {

// facts/clock-textures.md: resources 0x2B..0x34, the TEXC* members of TEXIMAGE, and the form func_002344F8
// (HDD OSD 1.10U) turns each into 32-bit pixels.
enum class PixelForm { Grey, RgbQuarter, Alpha, GreyAlpha, Rgba };

struct ClockTextureInfo {
    std::string_view name;
    uint32_t width, height;
    PixelForm form;
    uint32_t tbp;
};

inline constexpr std::array<ClockTextureInfo, 10> kClockTextures{{
    {"TEXCFLOW", 64, 64, PixelForm::Grey, 0x2bc0},       {"TEXCKABE", 128, 128, PixelForm::RgbQuarter, 0x2c00},
    {"TEXCBUMP", 64, 64, PixelForm::Grey, 0x2d00},       {"TEXCBINV", 64, 64, PixelForm::Grey, 0x2d40},
    {"TEXCSMOK", 64, 64, PixelForm::Alpha, 0x2d80},      {"TEXCREFA", 64, 64, PixelForm::Grey, 0x2dc0},
    {"TEXCNAVI", 64, 64, PixelForm::Alpha, 0x2e00},      {"TEXCBLUR", 64, 64, PixelForm::Alpha, 0x2e40},
    {"TEXCSTSL", 64, 64, PixelForm::GreyAlpha, 0x2e80},  {"TEXCMARU", 64, 64, PixelForm::Rgba, 0x2ec0},
}};

// The bytes the form reads: width x height x (1, 2 or 4), or a quarter at 3 bytes for RgbQuarter.
size_t rawTextureSize(PixelForm form, uint32_t width, uint32_t height);
// R, G, B, A bytes in memory order, width x height.
Bytes convertTexture(View raw, uint32_t width, uint32_t height, PixelForm form);

}
