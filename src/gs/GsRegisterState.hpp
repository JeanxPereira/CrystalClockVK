#pragma once

#include <cstdint>

// Pure structs representing PS2 GS register state.
// No methods — these are data containers that map the GS hardware registers
// used by the Crystal Clock rendering pipeline.

// GS ALPHA register: blend equation (A - B) * C + D
// Used to configure the 4 blend modes in the 5-pass pipeline.
struct GsAlpha {
    uint8_t a;  // 0=Cs, 1=Cd, 2=0
    uint8_t b;  // 0=Cs, 1=Cd, 2=0
    uint8_t c;  // 0=As, 1=Ad, 2=FIX
    uint8_t d;  // 0=Cs, 1=Cd, 2=0
    uint8_t fix; // Fixed alpha value (when C=FIX)

    // Predefined configs from opus-rod-analysis.md:
    // (1,0,1) → AlphaBlend:    (Cd - Cs) * As + Cs  → actually standard src-over
    // (2,1,2) → Additive:      (0 - Cd) * FIX + Cd  → pure additive
    // (0,1,1) → ReverseAlpha:  (Cs - Cd) * As + Cd  → inverted blend for fill

    static GsAlpha alphaBlend()     { return {1, 0, 0, 1, 0}; }
    static GsAlpha additive()       { return {2, 1, 2, 1, 0x80}; }
    static GsAlpha reverseAlpha()   { return {0, 1, 0, 0, 0}; }
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
