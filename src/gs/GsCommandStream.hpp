#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gs/GsRegisterState.hpp"

// Decoded PCSX2 .gs dump as an ordered GS command stream (Phase 1 W1).
// Pure PS2 logic, zero Vulkan. Mirrors tools/gsdump/inspect.mjs.

// GS PRIM register (also carried in a GIFtag PRE field).
struct GsPrim {
    uint8_t type;  // 0=POINT 1=LINE 2=LINE_STRIP 3=TRI 4=TRI_STRIP 5=TRI_FAN 6=SPRITE
    bool iip;      // shading: 0=flat 1=gouraud
    bool tme;      // texture mapping enable
    bool fge;      // fog enable
    bool abe;      // alpha blend enable
    bool aa1;      // antialiasing
    bool fst;      // texcoord: 0=STQ 1=UV
    uint8_t ctxt;  // context: 0=ctx1 1=ctx2
    bool fix;      // fragment value control
};

// GS TEX1 register: texture sampling.
struct GsTex1 {
    bool lcm;
    uint8_t mxl;
    bool mmag;     // magnification filter: 0=NEAREST 1=LINEAR
    uint8_t mmin;  // minification filter
    bool mtba;
};

struct GsScissor {
    uint16_t scax0, scax1, scay0, scay1;  // inclusive pixel bounds
};

struct GsXyOffset {
    uint32_t ofx, ofy;  // 12.4 fixed-point (divide by 16 for pixels)
};

// One GS vertex (XYZ kick), with the active color/texcoord at kick time.
struct GsVertex {
    float x, y;          // screen pixels (XYOFFSET removed, 12.4 -> /16)
    uint32_t z;
    uint8_t r, g, b, a;   // RGBAQ color
    float u, v;          // UV in pixels (FST=1)
    float s, t;          // STQ texcoords (FST=0)
    float q;             // RGBAQ.Q at kick time (STQ divisor; reset value 1.0)
    uint8_t fog;
};

// A draw group: a run of vertices sharing one PRIM load + resolved register
// state (snapshotted at the group's first kick).
struct GsPrimitive {
    uint32_t index;
    GsPrim prim;
    GsAlpha alpha;
    GsTest test;
    GsTexa texa;
    GsTex0 tex0;
    GsTex1 tex1;
    GsFrame frame;
    GsZbuf zbuf;
    GsScissor scissor;
    GsXyOffset xyoffset;
    bool dthe;
    bool colclamp;
    bool pabe;
    bool fba;
    std::vector<GsVertex> verts;
};

struct GsDumpHeaderInfo {
    uint32_t stateVersion = 0;
    uint32_t stateSize = 0;  // freeze size in bytes
    uint32_t crc = 0;
    uint32_t screenshotWidth = 0;
    uint32_t screenshotHeight = 0;
    std::string serial;
};

struct GsStreamCounts {
    uint32_t transfers = 0;
    uint32_t vsync = 0;
    uint32_t readfifo = 0;
    uint32_t regsPackets = 0;
    uint32_t giftags = 0;
    uint32_t draws = 0;   // draw groups (== prims.size())
    uint32_t kicks = 0;   // vertices emitted (XYZ kicks)
    uint64_t nloopSum = 0;
    uint32_t flgPacked = 0;
    uint32_t flgReglist = 0;
    uint32_t flgImage = 0;
    uint32_t flgImage2 = 0;
};

struct GsCommandStream {
    GsDumpHeaderInfo header;
    std::vector<uint8_t> freeze;    // GS state freeze (VRAM + state), stateSize bytes
    std::vector<uint8_t> privRegs;  // 8192-byte GSPrivRegSet
    std::vector<GsPrimitive> prims;
    GsStreamCounts counts;
};
