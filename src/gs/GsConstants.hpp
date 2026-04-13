#pragma once

#include <cstdint>

namespace GsConstants {

// Rod data structure layout (0x160 bytes per rod)
constexpr int ROD_STRUCT_SIZE = 0x160;
constexpr int ROD_OFFSET_ANGLE_A = 0x04;
constexpr int ROD_OFFSET_SCREEN_X = 0x10;
constexpr int ROD_OFFSET_SCREEN_Y = 0x14;
constexpr int ROD_OFFSET_MATRIX = 0x30;
constexpr int ROD_OFFSET_YSCALE = 0x60;
constexpr int ROD_OFFSET_FLAGS = 0x90;
constexpr int ROD_OFFSET_SCREEN_RATIO = 0xAC;
constexpr int ROD_OFFSET_SELECTION_FLAG = 0x150;

// Rod groups (absolute VRAM addresses in OSDSYS)
constexpr uint32_t ROD_GROUP_A_ADDR = 0x375250;
constexpr uint32_t ROD_GROUP_B_ADDR = 0x377E50;

// VU0 matrix memory layout
constexpr uint32_t MAT_TEMP_ADDR = 0x29BCF0;    // 32B trig buffer
constexpr uint32_t MAT_ROTATION_ADDR = 0x29BD10; // 64B rotation matrix
constexpr uint32_t MAT_PROJECTION_ADDR = 0x29BD50;// 64B projection matrix
constexpr uint32_t MAT_COMBINED_ADDR = 0x29BD90;  // 64B combined matrix

// GS coordinate space
constexpr float GS_FAR_PLANE = 2048.0f;
constexpr float GS_FIXED_POINT_SCALE = 65536.0f;
constexpr float GS_ASPECT = 1.0f;

// Per-pass angle step globals (GP-relative)
// These are runtime values in OSDSYS. Placeholder values derived from
// visual analysis — run ReadOSDSYSConstants.java in Ghidra to extract exact floats.
// The formula per rod: baseAngle + rodIndex * angleStep
constexpr float ANGLE_STEP_P2_STATIC = 0.10472f;  // ~6deg in radians (2*PI/60)
constexpr float ANGLE_STEP_P3_STATIC = 0.10472f;
constexpr float ANGLE_STEP_P2_ANIM = 0.10472f;
constexpr float ANGLE_STEP_P2B_ANIM = 0.10472f;
constexpr float ANGLE_STEP_P3_ANIM = 0.10472f;
constexpr float ANGLE_STEP_P3B_ANIM = 0.10472f;

// Projection parameters (GP-relative globals)
// fGpffff8c28 = FOV, fGpffff8488 = near, fGpffff8480/8484 = half-width
constexpr float GS_FOV = 0.7854f;            // ~45deg in radians (placeholder)
constexpr float GS_NEAR_PLANE = 1.0f;
constexpr float GS_HALF_WIDTH_4_3 = 256.0f;  // standard 4:3
constexpr float GS_HALF_WIDTH_16_9 = 341.0f; // widescreen 16:9

// Initial scale factor (fGpffff8318)
constexpr float INITIAL_SCALE_FACTOR = 1.0f;

// Screen ratio values from rod struct (+0xAC)
constexpr int SCREEN_RATIO_16_9 = 0x10; // 16
constexpr int SCREEN_RATIO_4_3 = 0x0E;  // 14

// Rod scale computation constants (from FUN_00232da0 → 0x00242630)
// Middle rods (indices 2-9)
constexpr int SCALE_MAX_RODS_MIDDLE_STD = 39;    // 0x27
constexpr int SCALE_MAX_RODS_MIDDLE_WIDE = 31;   // 0x1F
constexpr float SCALE_DIVISOR_MIDDLE_STD = 13.0f;
constexpr float SCALE_DIVISOR_MIDDLE_WIDE = 10.0f;

// Edge rods (indices 0-1 and 10+)
constexpr int SCALE_MAX_RODS_EDGE_STD = 32;      // 0x20
constexpr int SCALE_MAX_RODS_EDGE_WIDE = 26;     // 0x1A
constexpr float SCALE_DIVISOR_EDGE_STD = 6.0f;
constexpr float SCALE_DIVISOR_EDGE_WIDE = 5.0f;

// Hour countdown
constexpr int COUNTDOWN_STD = 40;     // 0x28
constexpr int COUNTDOWN_WIDE = 33;    // 0x21
constexpr float COUNTDOWN_MAX_STD = 40.0f;
constexpr float COUNTDOWN_MAX_WIDE = 33.0f;

// GS primitive color data addresses
constexpr uint32_t PRIM_TRANSPARENT_ADDR = 0x002973A0; // DAT_002973a0 (Pass 1/4/5)
constexpr uint32_t PRIM_COLORED_ADDR = 0x002973C0;     // DAT_002973c0 (Pass 2/3)

// Pass 5 alpha override
constexpr uint8_t PASS5_ALPHA_OVERRIDE = 0xFF;

} // namespace GsConstants
