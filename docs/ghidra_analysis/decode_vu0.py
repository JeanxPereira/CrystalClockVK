#!/usr/bin/env python3
"""
PS2 VU0 Macro Instruction Decoder v2
Corrected: Ghidra shows bits[24:0] of COP2 instructions; CO=1 is implicit.
"""

BC_NAMES = ['x', 'y', 'z', 'w']

def dest_str(dest_bits):
    s = ''
    if dest_bits & 0x8: s += 'x'
    if dest_bits & 0x4: s += 'y'
    if dest_bits & 0x2: s += 'z'
    if dest_bits & 0x1: s += 'w'
    return '.' + s if s else ''

def decode_vu0_upper(val):
    """Decode bits [24:0] of a COP2 VU0 upper instruction (CO=1 forced)."""
    dest  = (val >> 21) & 0xF
    ft    = (val >> 16) & 0x1F
    fs    = (val >> 11) & 0x1F
    fd    = (val >>  6) & 0x1F
    funct = val & 0x3F
    bc    = funct & 0x3
    op4   = (funct >> 2) & 0xF
    d     = dest_str(dest)
    bcn   = BC_NAMES[bc]

    # --- Standard 6-bit operations (bits 5-0) ---
    STD_OPS = {
        0x28: 'VADD',    # 101000
        0x29: 'VMADD',   # 101001
        0x2A: 'VMUL',    # 101010
        0x2B: 'VMAX',    # 101011
        0x2C: 'VSUB',    # 101100
        0x2D: 'VMSUB',   # 101101
        0x2E: 'VOPMSUB', # 101110
        0x2F: 'VMINI',   # 101111
    }
    if funct in STD_OPS:
        return f"{STD_OPS[funct]}{d} vf{fd}, vf{fs}, vf{ft}"

    # --- Q/I register operations ---
    QI_OPS = {
        0x20: ('VADDQ',  'Q'),   # 100000
        0x21: ('VMADDQ', 'Q'),   # 100001
        0x22: ('VADDI',  'I'),   # 100010
        0x23: ('VMADDI', 'I'),   # 100011
        0x24: ('VSUBQ',  'Q'),   # 100100
        0x25: ('VMSUBQ', 'Q'),   # 100101
        0x26: ('VSUBI',  'I'),   # 100110
        0x27: ('VMSUBI', 'I'),   # 100111
        0x1C: ('VMULQ',  'Q'),   # 011100
        0x1D: ('VMAXI',  'I'),   # 011101
        0x1E: ('VMULI',  'I'),   # 011110
        0x1F: ('VMINII', 'I'),   # 011111
    }
    if funct in QI_OPS:
        name, reg = QI_OPS[funct]
        return f"{name}{d} vf{fd}, vf{fs}, {reg}"

    # --- Broadcast operations (bits 5-2 = op, bits 1-0 = bc) ---
    BC_OPS = {
        0x0: 'VADD',   # 0000
        0x1: 'VSUB',   # 0001
        0x2: 'VMADD',  # 0010
        0x3: 'VMSUB',  # 0011
        0x4: 'VMAX',   # 0100
        0x5: 'VMINI',  # 0101
        0x6: 'VMUL',   # 0110
        0x7: 'VMULQ',  # 0111 ← not standard, might be different
    }
    if op4 in BC_OPS and op4 <= 0x6:
        return f"{BC_OPS[op4]}{bcn}{d} vf{fd}, vf{fs}, vf{ft}{bcn}"

    # --- SPECIAL operations (op4 = 0xF, bits 5-2 = 1111) ---
    if op4 == 0xF:
        # fd becomes sub-opcode
        FTOI_MODES = {0: 0, 1: 4, 2: 12, 3: 15}
        
        if fd == 0x04:  # ITOF
            m = FTOI_MODES.get(bc, bc)
            return f"VITOF{m}{d} vf{ft}, vf{fs}"
        elif fd == 0x05:  # FTOI
            m = FTOI_MODES.get(bc, bc)
            return f"VFTOI{m}{d} vf{ft}, vf{fs}"
        elif fd == 0x07:
            if bc == 1: return f"VABS{d} vf{ft}, vf{fs}"
            if bc == 3: return f"VCLIPw{d} vf{ft}, vf{fs}"
        elif fd == 0x0A:
            if bc == 0: return f"VADDA{d} ACC, vf{fs}, vf{ft}"
            if bc == 1: return f"VMADDA{d} ACC, vf{fs}, vf{ft}"
        elif fd == 0x0B:
            if bc == 0: return f"VSUBA{d} ACC, vf{fs}, vf{ft}"
            if bc == 1: return f"VMSUBA{d} ACC, vf{fs}, vf{ft}"
            if bc == 2: return f"VOPMULA{d} ACC, vf{fs}, vf{ft}"
            if bc == 3: return f"VNOP"
        # Accumulator broadcast ops
        elif fd == 0x00:
            return f"VADDA{bcn}{d} ACC, vf{fs}, vf{ft}{bcn}"
        elif fd == 0x01:
            return f"VSUBA{bcn}{d} ACC, vf{fs}, vf{ft}{bcn}"
        elif fd == 0x02:
            return f"VMADDA{bcn}{d} ACC, vf{fs}, vf{ft}{bcn}"
        elif fd == 0x03:
            return f"VMSUBA{bcn}{d} ACC, vf{fs}, vf{ft}{bcn}"
        elif fd == 0x08:
            if bc == 0: return f"VADDAQ{d} ACC, vf{fs}, Q"
            if bc == 1: return f"VMADDAQ{d} ACC, vf{fs}, Q"
            if bc == 2: return f"VADDAI{d} ACC, vf{fs}, I"
            if bc == 3: return f"VMADDAI{d} ACC, vf{fs}, I"
        elif fd == 0x09:
            if bc == 0: return f"VSUBAQ{d} ACC, vf{fs}, Q"
            if bc == 1: return f"VMSUBAQ{d} ACC, vf{fs}, Q"
            if bc == 2: return f"VSUBAI{d} ACC, vf{fs}, I"
            if bc == 3: return f"VMSUBAI{d} ACC, vf{fs}, I"
        elif fd == 0x0C:
            return f"VMOVE{d} vf{ft}, vf{fs}"
        elif fd == 0x0D:
            return f"VMR32{d} vf{ft}, vf{fs}"
        elif fd == 0x10:  # R register
            if bc == 0: return f"VRNEXT{d} vf{ft}, R"
            if bc == 1: return f"VRGET{d} vf{ft}, R"
        
        return f"VSPECIAL fd=0x{fd:02x} bc={bc}{d} ft=vf{ft} fs=vf{fs}"
    
    # Accumulator variants with Q/I (when bits match)
    # funct = 011000-011011: MULAbc
    if op4 == 0x7:
        return f"VMULA{bcn}{d} ACC, vf{fs}, vf{ft}{bcn}"

    return f"V??? funct=0x{funct:02x}({funct:06b}){d} vf{fd}, vf{fs}, vf{ft}"


# ============================================================
# Data from Ghidra
# ============================================================

rotation_build = [
    (0x002732d8, 0x11a35a5, ""),
    (0x002732dc, 0x117c6e9, ""),
    (0x002732e0, 0x1155689, ""),
    (0x002732e4, 0x112e49a, ""),
    (0x002732e8, 0x1107130, ""),
    (0x002732ec, 0x10dfc61, ""),
    (0x002732f0, 0x10b8640, ""),
    (0x002732f4, 0x1090ee0, ""),
    (0x002732f8, 0x1069658, ""),
    (0x002732fc, 0x1041cbb, ""),
    (0x00273300, 0, "bc1t 0x0025bb74"),
    (0x00273304, 0x0fe4d23, "_cop1 (delay slot)"),
    (0x00273308, 0x0f9545c, ""),
    (0x0027330c, 0x0f45a0b, ""),
    (0x00273310, 0x0ef5e59, ""),
    (0x00273314, 0x0ea616d, ""),
    (0x00273318, 0x0e5636e, ""),
    (0x0027331c, 0x0e06483, ""),
    (0x00273320, 0x0db64d6, ""),
    (0x00273324, 0x0d66489, ""),
    (0x00273328, 0x0d163c8, ""),
    (0x0027332c, 0x0cc62b7, ""),
    (0x00273330, 0x0c7617e, ""),
    (0x00273334, 0x0c26040, ""),
    (0x00273338, 0x0bd5f28, ""),
    (0x0027333c, 0x0b85e58, ""),
    (0x00273340, 0x0b35df7, ""),
    (0x00273344, 0x0ae5e2c, ""),
    (0x00273348, 0x0a95f1b, ""),
    (0x0027334c, 0x0a460ea, ""),
    (0x00273350, 0x09f63be, ""),
    (0x00273354, 0x09a67ba, ""),
    (0x00273358, 0x0956d05, ""),
    (0x0027335c, 0x09073c2, ""),
    (0x00273360, 0x08b7c17, ""),
    (0x00273364, 0x0868626, ""),
    (0x00273368, 0x0819213, ""),
    (0x0027336c, 0x0794006, ""),
    (0x00273370, 0x06f602e, ""),
    (0x00273374, 0x06584e8, ""),
    (0x00273378, 0x05bae77, ""),
    (0x0027337c, 0x051dd22, ""),
    (0x00273380, 0x048112f, ""),
    (0x00273384, 0x03e4ae0, ""),
]

print("=" * 90)
print("  FUN_002732d8 — rotation_build (VU0 COP2 Upper, CO=1 forced)")
print("=" * 90)
for addr, val, note in rotation_build:
    if note.startswith("bc1t"):
        print(f"  0x{addr:08x}: {note}")
        continue
    decoded = decode_vu0_upper(val)
    extra = f"  ; {note}" if note else ""
    print(f"  0x{addr:08x}: {decoded}{extra}")

# Let's also decode FUN_002730a8 (projection)
projection_build = [
    (0x002730a8, 0x191493d), (0x002730ac, 0x191ed4b), (0x002730b0, 0x1928c38),
    (0x002730b4, 0x1932605), (0x002730b8, 0x193bab5), (0x002730bc, 0x1944a48),
    (0x002730c0, 0x194d4c2), (0x002730c4, 0x1955a24), (0x002730c8, 0x195da70),
    (0x002730cc, 0x19655a9), (0x002730d0, 0x196cbd1), (0x002730d4, 0x1973ceb),
    (0x002730d8, 0x197a8f9), (0x002730dc, 0x1980fff), (0x002730e0, 0x19871ff),
    (0x002730e4, 0x198cefd), (0x002730e8, 0x19926fc), (0x002730ec, 0x19979ff),
    (0x002730f0, 0x199c80a), (0x002730f4, 0x19a1121), (0x002730f8, 0x19a5547),
    (0x002730fc, 0x19a9481), (0x00273100, 0x19aced1), (0x00273104, 0x19b043c),
    (0x00273108, 0x19b34cc), (0x0027310c, 0x19b607b), (0x00273110, 0x19b8755),
    (0x00273114, 0x19ba95d), (0x00273118, 0x19bc697), (0x0027311c, 0x19bdf08),
    (0x00273120, 0x19bf2b6), (0x00273124, 0x19c01a5), (0x00273128, 0x19c0bdc),
    (0x0027312c, 0x19c115e), (0x00273130, 0x19c1233), (0x00273134, 0x19c0e5e),
    (0x00273138, 0x19c05e7), (0x0027313c, 0x19bf8d3), (0x00273140, 0x19be728),
    (0x00273144, 0x19bd0eb), (0x00273148, 0x19bb623), (0x0027314c, 0x19b96d6),
    (0x00273150, 0x19b730b), (0x00273154, 0x19b4ac8), (0x00273158, 0x19b1e13),
    (0x0027315c, 0x19aecf3), (0x00273160, 0x19ab770), (0x00273164, 0x19a7d90),
    (0x00273168, 0x19a3f5a), (0x0027316c, 0x199fcd6), (0x00273170, 0x199b60a),
    (0x00273174, 0x1996afd), (0x00273178, 0x1991bb7), (0x0027317c, 0x198c83d),
    (0x00273180, 0x1987092), (0x00273184, 0x19814d5), (0x00273188, 0x197b4f2),
    (0x0027318c, 0x19750fd), (0x00273190, 0x196e8fe), (0x00273194, 0x1967cfb),
    (0x00273198, 0x1960cfc), (0x0027319c, 0x195990a), (0x002731a0, 0x195212d),
    (0x002731a4, 0x194a56d), (0x002731a8, 0x19425d3), (0x002731ac, 0x193a267),
    (0x002731b0, 0x1931b31), (0x002731b4, 0x192903a), (0x002731b8, 0x192018a),
    (0x002731bc, 0x1916f2b), (0x002731c0, 0x190d926), (0x002731c4, 0x1903f82),
    (0x002731c8, 0x18fa249), (0x002731cc, 0x18f0184), (0x002731d0, 0x18e5d3d),
    (0x002731d4, 0x18db57b), (0x002731d8, 0x18d0a49), (0x002731dc, 0x18c5baf),
    (0x002731e0, 0x18ba9b8), (0x002731e4, 0x18af46b), (0x002731e8, 0x18a3bd4),
    (0x002731ec, 0x1897ffa), (0x002731f0, 0x188c0e9), (0x002731f4, 0x187fea8),
    (0x002731f8, 0x1873943), (0x002731fc, 0x1862d18), (0x00273200, 0x185abe3),
    (0x00273204, 0x184ceca), (0x00273208, 0x183fd2a), (0x0027320c, 0x1832893),
    (0x00273210, 0x182510e), (0x00273214, 0x18176a7),
]

print()
print("=" * 90)
print("  FUN_002730a8 — projection_build (VU0 COP2 Upper, CO=1 forced)")
print("=" * 90)
for addr, val in projection_build:
    decoded = decode_vu0_upper(val)
    print(f"  0x{addr:08x}: {decoded}")

# Analysis: check what registers are used most
print()
print("=" * 90)
print("  REGISTER USAGE ANALYSIS (rotation_build)")
print("=" * 90)
from collections import Counter
ft_used = Counter()
fs_used = Counter()
fd_used = Counter()
ops_used = Counter()
for addr, val, note in rotation_build:
    if note.startswith("bc1t"):
        continue
    dest  = (val >> 21) & 0xF
    ft    = (val >> 16) & 0x1F
    fs    = (val >> 11) & 0x1F
    fd    = (val >>  6) & 0x1F
    funct = val & 0x3F
    ft_used[ft] += 1
    fs_used[fs] += 1
    fd_used[fd] += 1
    ops_used[funct] += 1

print("  ft (source/broadcast): ", sorted(ft_used.items()))
print("  fs (source):           ", sorted(fs_used.items()))
print("  fd (destination):      ", sorted(fd_used.items()))
print("  funct (opcode):        ", [(f"0x{k:02x}", v) for k, v in sorted(ops_used.items())])
