#pragma once

#include <cstdint>

namespace GsConstants {

constexpr int ROD_COUNT = 12;

constexpr int ROD_STRUCT_SIZE = 0x140;
constexpr int ROD_OFFSET_MATRIX = 0x30;
constexpr int ROD_OFFSET_POSITION = 0x60;
constexpr int ROD_OFFSET_SCALE = 0x78;
constexpr int ROD_OFFSET_GLASS_RGBA = 0x90;
constexpr int ROD_OFFSET_ADDITIVE_RGBA = 0xD0;
constexpr int ROD_OFFSET_OFFSETPASS_RGBA = 0xE0;
constexpr int ROD_OFFSET_SELECTION_FLAG = 0xF0;

constexpr uint32_t ROD_ARRAY_PTR_ADDR = 0x0034E980;
constexpr uint32_t ROD_DATA_ADDR = 0x0034F9C0;
constexpr uint32_t ROD_GROUP_A_ADDR = 0x375250;
constexpr uint32_t ROD_GROUP_B_ADDR = 0x377E50;

constexpr uint32_t CLOCK_RENDER_ENTRY = 0x00232618;
constexpr uint32_t ROD_GLASS_RENDER_FUNC = 0x00232640;
constexpr uint32_t ROD_SPECULAR_RENDER_FUNC = 0x00232878;

constexpr float ANGLE_STEP_PASS2 = 0.26f;
constexpr float ANGLE_STEP_PASS3 = 0.33f;
constexpr float REFRACT_DISP_A = 0.037f;
constexpr float REFRACT_DISP_B = 0.031f;
constexpr float REFRACT_DISP_C = 0.028f;

constexpr uint8_t GLASS_RGBA[4] = {45, 87, 102, 128};
constexpr uint8_t ADDITIVE_RGBA[4] = {60, 60, 60, 128};
constexpr uint8_t OFFSETPASS_RGBA[4] = {40, 40, 40, 128};

constexpr float GS_FAR_PLANE = 2048.0f;
constexpr float GS_FIXED_POINT_SCALE = 16.0f;

constexpr int SCREEN_RATIO_16_9 = 0x10;
constexpr int SCREEN_RATIO_4_3 = 0x0E;

constexpr int SCALE_MAX_RODS_MIDDLE_STD = 39;
constexpr int SCALE_MAX_RODS_MIDDLE_WIDE = 31;
constexpr float SCALE_DIVISOR_MIDDLE_STD = 13.0f;
constexpr float SCALE_DIVISOR_MIDDLE_WIDE = 10.0f;

constexpr int SCALE_MAX_RODS_EDGE_STD = 32;
constexpr int SCALE_MAX_RODS_EDGE_WIDE = 26;
constexpr float SCALE_DIVISOR_EDGE_STD = 6.0f;
constexpr float SCALE_DIVISOR_EDGE_WIDE = 5.0f;

constexpr int COUNTDOWN_STD = 40;
constexpr int COUNTDOWN_WIDE = 33;
constexpr float COUNTDOWN_MAX_STD = 40.0f;
constexpr float COUNTDOWN_MAX_WIDE = 33.0f;

} // namespace GsConstants
