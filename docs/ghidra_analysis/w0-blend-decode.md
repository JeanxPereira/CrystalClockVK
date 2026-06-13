# W0 Blend Decode — Crystal Clock GS Alpha Blend Mapping

Static RE only. Sources: CrystalOSD decomp assembly, pcsx2-ref GSRegs.h, runtime-trace.md.
No live PCSX2 reads. All Ghidra addresses are OSDSYS.elf.

> ⚠️ **CONTROLLER REVIEW VERDICT (W0-3 is the weakest W0 result — treat with caution):**
> - **ACCEPTED:** the BLOCKER — the `0x002973a0` template rows decode to all-zero ALPHA fields, i.e.
>   they are VIF-wrapped GIF packets, NOT raw A+D entries; not decodable statically (needs the init
>   writer trace or a different live capture).
> - **CONTESTED / DO NOT TREAT AS FACT:** the blend REMAPPING below. (1) It claims the rod crystal
>   pass = "mode 2 = Cd passthrough, zero color" — this CONTRADICTS the GS-dump ground truth (652
>   additive + 1176 subtractive + 2108 src-over draws; `MEMORY.md` gs-dump note), which is HARDER
>   evidence than a static decode. (2) It decodes `FUN_00230fe8(mode)` as a BLEND setter, but
>   `CLOCK-SYSTEM-MAP.md §3` classifies `FUN_00230fe8` as a TEST/Z setter (blend = `FUN_002324e8`/
>   `FUN_00232538`) — so the "mode 6/7 → subtractive/src-over" orb mapping may be misattributed.
> - **AUTHORITATIVE BLEND SOURCE remains the GS dump** (3 blends, counts known). This doc is a
>   cross-check that hit a blocker; it does NOT override the dump. Re-verify FUN_00230fe8's role
>   (blend vs Z/test) and the orb blends before relying on them (W3/W4, not W1).

---

## 1. GIF A+D Entry Layout

From `sceVif1PkAddGsAD@0x0027DC98` (decomp path: `asm/core/sceVif1PkAddGsAD.s`):

```
offset +0  : DATA_lo32  = bits[31:0] of the 64-bit register value
offset +4  : DATA_hi32  = bits[63:32] of the 64-bit register value
offset +8  : REG        = GS register address (u32, only bits[7:0] used)
offset +12 : 0
```

The function sign-extends a2 via `dsll32/dsra32` to get `DATA_lo32`, then `dsra32 a2,a2,0` to get `DATA_hi32` (upper 32 bits of the incoming 64-bit register in a2 shifted down). Result: a proper 64-bit DATA split across two u32 slots.

**NOT** a [DATA_lo, DATA_lo, REG, 0] duplicate format — confirmed from disassembly lines:
```asm
dsll32 v1, a2, 0   ; v1 = low32 of a2 (sign-extended)
dsra32 v1, v1, 0
dsra32 a2, a2, 0   ; a2 = high32 of a2 shifted to bits[31:0]
sw v1, 0(v0)       ; [+0] = DATA_lo32
sw a2, 0(v0+4)     ; [+4] = DATA_hi32
sw a1, 4(v0+4)     ; [+8] = REG
sw 0,  8(v0+4)     ; [+12] = 0
```

---

## 2. GS ALPHA Register Bit Layout

From `GIFRegALPHA` in `pcsx2/GS/GSRegs.h` (line 521):

```cpp
struct GIFRegALPHA {
    u32 A     :  2;   // bits[1:0]  — 0=Cs, 1=Cd, 2=0
    u32 B     :  2;   // bits[3:2]  — 0=Cs, 1=Cd, 2=0
    u32 C     :  2;   // bits[5:4]  — 0=As, 1=Ad, 2=FIX
    u32 D     :  2;   // bits[7:6]  — 0=Cs, 1=Cd, 2=0
    u32 _PAD1 : 24;   // bits[31:8]
    u8  FIX;          // bits[39:32]
    u8  _PAD2[3];     // bits[63:40]
};
```

Blend equation: `output = (A - B) * C/128 + D`.

GS register numbers: ALPHA_1 = 0x42 (context 1), ALPHA_2 = 0x43 (context 2).
PABE register = 0x49 (per-pixel blend enable; 0 = ALPHA always applied, 1 = only if pixel alpha = 0xFF).

---

## 3. pktSetAlphaBlend Argument → ALPHA Packing

From `pktSetAlphaBlend@0x0021B9C8` (decomp: `asm/graph/pktSetAlphaBlend.s`):

```
Signature: pktSetAlphaBlend(buf, ABE_flag, blend_mode_idx, FIX_alpha)
  a0 = buf        — VIF1 packet buffer pointer-pointer
  a1 = ABE_flag   — 1 = ABE enabled (normal blend), 0 = ABE disabled (write PABE=1)
  a2 = blend_mode_idx — index into D_002B0CC0 table (stride 16 bytes)
  a3 = FIX_alpha  — fixed alpha value (placed in ALPHA.FIX, bits[39:32])
```

Steps:
1. `pktSetAD(buf, reg=0x49, data=!ABE_flag)` — writes PABE register
2. Load `A,B,C,D` from `D_002B0CC0[blend_mode_idx * 16 + {0,4,8,12}]`
3. Pack: `DATA_64bit = A | (B<<2) | (C<<4) | (D<<6) | (FIX_alpha << 32)`
4. `pktSetAD(buf, reg=0x42, data=DATA_64bit)` — writes ALPHA_1

The FIX_alpha is put into the HIGH 32 bits via `dsll32 s1, s1, 0` before OR-ing into DATA.

---

## 4. D_002B0CC0 Blend Mode Table

Static data at Ghidra `0x002B0CC0` (decomp: `asm/data/datasect.data.s` line 13088).
10 entries, stride 16 bytes, layout = [A, B, C, D] (each i32).

| Idx | A | B | C | D | Equation                   | Name                  |
|-----|---|---|---|---|----------------------------|-----------------------|
|  0  | 0 | 2 | 2 | 1 | (Cs-0)×0/128+Cd = Cd      | passthrough-dst       |
|  1  | 2 | 0 | 2 | 1 | (0-Cs)×0/128+Cd = Cd      | passthrough-dst       |
|  2  | 0 | 1 | 2 | 1 | (Cs-Cd)×0/128+Cd = Cd     | passthrough-dst       |
|  3  | 1 | 2 | 2 | 0 | (Cd-0)×0/128+Cs = Cs      | passthrough-src       |
|  4  | 0 | 1 | 0 | 1 | (Cs-Cd)×As/128+Cd          | **SRC-OVER**          |
|  5  | 0 | 2 | 0 | 1 | (Cs-0)×As/128+Cd           | **ADDITIVE**          |
|  6  | 2 | 0 | 0 | 1 | (0-Cs)×As/128+Cd           | **SUBTRACTIVE**       |
|  7  | 0 | 1 | 0 | 1 | (Cs-Cd)×As/128+Cd          | **SRC-OVER** (=idx4)  |
|  8  | 0 | 2 | 1 | 1 | (Cs-0)×Ad/128+Cd           | DST-ALPHA ADDITIVE    |
|  9  | 2 | 0 | 1 | 1 | (0-Cs)×Ad/128+Cd           | DST-ALPHA SUBTRACTIVE |

Note: C=2 (FIX) with FIX=0 collapses to zero factor → indices 0–3 are passthrough modes.
Indices 0–3 are used for state setup where ABE_flag=0 (ABE disabled during init).

---

## 5. Call-Site Evidence: Blend Mode Usage

All addresses are Ghidra/decomp addresses.

### Rod render function call sites

| Caller addr  | Function called   | a0 (mode) | a1 (ABE) | a2 (FIX) | Pass role              |
|--------------|-------------------|-----------|----------|----------|------------------------|
| 0x00224190   | FUN_002324e8      | 1         | 0        | 1        | src-over glow (pass 1) |
| 0x00224640   | FUN_002324e8      | 0         | 1        | 1        | draw env switch        |
| 0x0022428c   | FUN_00230fe8      | 2         | 1        | 2        | crystal pass           |
| 0x002247d4   | FUN_00230fe8      | 2         | 1        | 2        | crystal pass (again)   |
| 0x00224548   | FUN_00232538      | 1         | 0        | 1        | back-face sub-variant  |

### Orb render function call sites

| Caller addr  | Function called   | a0 (mode) | a1 (ABE) | a2 (FIX) | Pass role       |
|--------------|-------------------|-----------|----------|----------|-----------------|
| 0x00225cb0   | FUN_00230fe8      | 6         | 1        | 1        | orb CORE        |
| 0x00226248   | FUN_00230fe8      | 7         | 1        | 1        | orb trail/halo  |
| 0x00226374   | FUN_00230fe8      | 6         | 1        | 1        | orb CORE (2nd)  |

**Important**: `FUN_00230fe8` in the decomp corresponds to `func_00230FD8@0x00230FD8` (the function
body starts at 0x00230FD8; 0x00230FE8 is 0x10 bytes in). It is a UI/controller input handler
(reads button state, calls 0x0024b330 etc.) — NOT the blend setter directly. The (a0,a1,a2)
arguments may be passed through to an inner call. Further tracing needed to identify the actual
blend setter that processes these indices.

### Direct pktSetAlphaBlend / vif1SetAlphaBlend calls (graph layer, not clock)

| Caller file              | Function called       | a1 (ABE) | a2 (mode) | a3 (FIX) |
|--------------------------|-----------------------|----------|-----------|----------|
| func_0021D6C0.s (graph)  | vif1SetAlphaBlend     | 1        | 1         | 0x80     |
| func_0021D848.s (graph)  | vif1SetAlphaBlend     | 1        | 4         | 0        |
| func_0021D3D0.s (graph)  | vif1SetAlphaBlend     | 0        | 0         | 0        |
| func_0021D990.s (graph)  | pktSetAlphaBlend      | 1        | 4         | 0        |
| func_0021D140.s (graph)  | pktSetAlphaBlend      | (varies) | (varies)  | (varies) |
| OpeningDrawLights.s      | pktSetAlphaBlend      | 1        | 5         | 0x80     |

These confirm: mode 4 = src-over, mode 5 = additive, used consistently across graph + opening.

---

## 6. Orb Blend Correction

orbs-particles.md had halo/core blend REVERSED. Correct assignment from call-site evidence:

| Orb pass  | FUN_00230fe8 a0 | Table idx | Blend equation              | Name        |
|-----------|-----------------|-----------|------------------------------|-------------|
| CORE      | 6               | 6         | (0-Cs)×As/128+Cd             | SUBTRACTIVE |
| HALO/trail| 7               | 7 = idx4  | (Cs-Cd)×As/128+Cd            | SRC-OVER    |

orbs-particles.md hypothesis (halo=subtractive, core=additive) is WRONG.
Correct: core=subtractive, halo=src-over. Confidence: HIGH (direct call-site arg read).

---

## 7. Rod Blend Mode Mapping

| Rod pass            | Mode arg | Table idx | Blend equation         | Name           | Confidence |
|---------------------|----------|-----------|------------------------|----------------|------------|
| Pass 1 glow         | 1 (env)  | —         | draw env 1 setup       | (via env)      | HYPOTHESIS |
| Pass 2 crystal      | 2        | 2         | (Cs-Cd)×0/128+Cd = Cd  | PASSTHROUGH    | CONFIRMED  |
| Pass 3 back-face    | 1 (env)  | —         | draw env 1 setup       | (via env)      | HYPOTHESIS |

Mode 2 → table idx 2 → (Cs-Cd)×FIX/128+Cd with C=2(FIX) and FIX=0 → Cd passthrough.
This means the crystal pass writes geometry with zero color contribution. The visual effect of
the refraction pass must come from PABE=0 (ABE disabled for specific pixels) or depth-buffer
interaction, not from direct color blending. This needs further investigation.

Expected 3 clock blends from gs-dump-format-and-clock-regs.md: src-over, additive, subtractive.
- Src-over: present (orb halo mode 7 = idx4; rod pass 1 via draw env)
- Additive: present (expected from graph layer mode 5 calls; rod crystal pass mode 2 does NOT
  produce additive — the additive blend for rods comes from the draw environment, not mode 2)
- Subtractive: present (orb core mode 6)

The crystal-rod pass with mode 2 (Cd passthrough) is the refraction/after-image pass, not
the additive crystal pass. The additive pass is encoded in the draw environment, not in mode 2.

---

## 8. Template Bytes at 0x002973a0 — Partial Decode, BLOCKED

Runtime dump from runtime-trace.md:
```
0x2973a0: 00008000 c4000000  43431880 00004343   <- rod template A row 0
0x2973b0: 00008000 a4000000  43434310 00000043   <- rod template A row 1
0x2973c0: 00008000 e4000000  41241280 00412412   <- rod template B row 0
0x2973d0: 000000ff 000000ff   000000ff 00000080  <- vertex RGBA constant
```

These addresses are BSS (zero in static ELF, written by init). The 0xC4/0xA4/0xE4 byte
appears at offset +4 of each 16-byte row (DATA_hi32 position in GIF A+D format).

If rows are GIF A+D entries [DATA_lo32, DATA_hi32, REG, 0]:
- DATA_lo32 = 0x00008000 → ALPHA bits[7:0] = 0x00 → A=B=C=D=0 (C=As=0, not FIX)
- DATA_hi32 = 0xC4000000 → ALPHA.FIX = bits[39:32] = byte[0] of hi32 = 0x00 (FIX=0, not 0xC4)
- REG (u32[2]) LE byte[0] = 0x80 — NOT a valid GS register

**BLOCKER**: 0x80 at the REG position is invalid (GS ALPHA regs are 0x42/0x43). Two competing
interpretations exist:
1. PCSX2 dump is LE u32 display → byte[8] = 0x80 (invalid)
2. PCSX2 dump is big-endian byte display → byte[8] = 0x43 = ALPHA_2 (valid)

Under interpretation 2 (BE byte display):
- ADDR = 0x43 = GIF_A_D_REG_ALPHA_2 (context 2)
- DATA bytes [0..7] in memory order: 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0xC4
- DATA as LE u64 = 0xC400000000800000 → ALPHA bits[7:0] = 0x00 (STILL A=B=C=D=0)

The A,B,C,D fields at ALPHA bits[7:0] = 0 in ALL interpretations tried. HYPOTHESIS: the
0x00008000 value is NOT the raw ALPHA DATA but rather a GIF tag header or part of a
larger surrounding packet (VIF DIRECT wrapper). The init function that writes to 0x002973a0
must be traced to resolve this. No further static decode is possible without that trace.

Row 3 (`000000ff 000000ff 000000ff 00000080`) = vertex RGBA constant: white (0xFF,0xFF,0xFF)
with alpha 0x80 = 128. This is the vertex color for the full-alpha additive draw. Confirmed.

---

## 9. Summary

| Finding                                    | Confidence |
|--------------------------------------------|------------|
| D_002B0CC0 blend table (10 entries)        | CONFIRMED  |
| GIF A+D layout [lo32, hi32, reg, 0]        | CONFIRMED  |
| pktSetAlphaBlend packing (FIX in hi32)     | CONFIRMED  |
| Orb CORE = SUBTRACTIVE (mode 6)            | CONFIRMED  |
| Orb HALO/TRAIL = SRC-OVER (mode 7=idx4)   | CONFIRMED  |
| Rod crystal pass = Cd PASSTHROUGH (mode 2) | CONFIRMED  |
| Rod glow/back-face = via draw environment  | HYPOTHESIS |
| Template bytes A,B,C,D field decode        | BLOCKED    |
| Template REG = ALPHA_2 (0x43)              | HYPOTHESIS (BE display only) |

3 clock blends confirmed present (src-over, additive, subtractive) but additive for rods
comes from draw environments, not from an inline pktSetAlphaBlend call with mode 5.
The Cd-passthrough mode 2 for the crystal rod pass is the refraction/depth-write pass.
