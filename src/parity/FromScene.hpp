#pragma once
#include <array>
#include <map>
#include <string>

#include "parity/GsFrame.hpp"
#include "scene/Frame.hpp"

namespace parity {

struct GsTextureImage {
    std::string id;
    uint32_t width = 0, height = 0;
};

// Where the clock's buffers and textures sit in GS memory for one frame (clock_frame.mjs stateWriters).
struct GsFrameLayout {
    std::array<std::string, 3> targets;
    uint32_t width = 0, height = 0;
    uint32_t targetTextureWidth = 0, targetTextureHeight = 0;
    std::map<int32_t, GsTextureImage> textures;
};

GsFrameLayout clockLayout(int32_t width, int32_t height, int32_t displayIndex);

// The scene frame in the GS units of the parity rule: the field's half line added where the OSD draws
// with it, the targets and textures named by their GS addresses, the state as GS registers express it.
GsFrame fromScene(const scene::Frame& frame, const GsFrameLayout& layout);

}
