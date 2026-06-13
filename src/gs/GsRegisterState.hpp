#pragma once

#include <cstdint>

// GS register state containers. Values must come from decoded OSDSYS packets
// (pktSetAlphaBlend, pktSetTEST_1, sceGsPutDrawEnv) — never hand-authored.

// ALPHA: Cv = ((A - B) * C >> 7) + D
// A,B,D: 0=Cs 1=Cd 2=0 | C: 0=As 1=Ad 2=FIX
struct GsAlpha {
    uint8_t a;
    uint8_t b;
    uint8_t c;
    uint8_t d;
    uint8_t fix;
};

struct GsColClamp {
    bool clamp;
};

struct GsDthe {
    bool enable;
};

struct GsDimx {
    int8_t dm[4][4];
};

// GS TEX0 register: texture configuration
struct GsTex0 {
    uint32_t tbp0;   // Texture base pointer (in 64-byte units)
    uint16_t tbw;    // Texture buffer width (in 64-pixel units)
    uint8_t psm;     // Pixel storage method (0=PSMCT32, 19=PSMT8, etc.)
    uint16_t tw;     // Texture width (log2)
    uint16_t th;     // Texture height (log2)
    uint8_t tcc;     // Texture color component (0=RGB, 1=RGBA)
    uint16_t tfx;    // Texture function (0=MODULATE, 1=DECAL, 2=HIGHLIGHT, 3=HIGHLIGHT2)
    uint32_t cbp;    // CLUT buffer pointer
    uint8_t cpsm;    // CLUT pixel storage mode
    uint8_t csm;     // CLUT storage mode (0=CSM1, 1=CSM2)
    uint8_t csa;     // CLUT entry start address
    uint8_t cld;     // CLUT buffer load control
};

// GS TEST register: pixel test configuration
struct GsTest {
    bool ate;         // Alpha test enable
    uint8_t atst;     // Alpha test method (0=NEVER, 1=ALWAYS, etc.)
    uint8_t aref;     // Alpha reference value
    uint8_t afail;    // Action on alpha test fail
    bool date;        // Destination alpha test enable
    uint8_t datm;     // Destination alpha test mode
    bool zte;         // Z test enable
    uint8_t ztst;     // Z test method
};

// GS FRAME register: framebuffer configuration
struct GsFrame {
    uint32_t fbp;    // Frame buffer base pointer (in 2048-pixel units)
    uint8_t fbw;     // Frame buffer width (in 64-pixel units)
    uint8_t psm;     // Pixel storage method
    uint32_t fbmsk;  // Frame buffer write mask
};

// GS ZBUF register: Z-buffer configuration
struct GsZbuf {
    uint32_t zbp;    // Z-buffer base pointer
    uint8_t psm;     // Z pixel storage method
    bool zmsk;       // Z write mask (true = writes disabled)
};
