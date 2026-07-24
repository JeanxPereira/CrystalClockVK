FTOI_BITS = {0: 0, 1: 4, 2: 12, 3: 15}

def _fill():
    t = {}
    for f in range(0x00, 0x04): t[f] = ("VADD", "bc")
    for f in range(0x04, 0x08): t[f] = ("VSUB", "bc")
    for f in range(0x08, 0x0C): t[f] = ("VMADD", "bc")
    for f in range(0x0C, 0x10): t[f] = ("VMSUB", "bc")
    for f in range(0x10, 0x14): t[f] = ("VMAX", "bc")
    for f in range(0x14, 0x18): t[f] = ("VMINI", "bc")
    for f in range(0x18, 0x1C): t[f] = ("VMUL", "bc")
    t[0x1C] = ("VMUL", "q")
    t[0x1D] = ("VMAX", "i")
    t[0x1E] = ("VMUL", "i")
    t[0x1F] = ("VMINI", "i")
    t[0x20] = ("VADD", "q")
    t[0x21] = ("VMADD", "q")
    t[0x22] = ("VADD", "i")
    t[0x23] = ("VMADD", "i")
    t[0x24] = ("VSUB", "q")
    t[0x25] = ("VMSUB", "q")
    t[0x26] = ("VSUB", "i")
    t[0x27] = ("VMSUB", "i")
    t[0x28] = ("VADD", "std")
    t[0x29] = ("VMADD", "std")
    t[0x2A] = ("VMUL", "std")
    t[0x2B] = ("VMAX", "std")
    t[0x2C] = ("VSUB", "std")
    t[0x2D] = ("VMSUB", "std")
    t[0x2E] = ("VOPMSUB", "std")
    t[0x2F] = ("VMINI", "std")
    for f in range(0x3C, 0x40): t[f] = ("SPECIAL", "special")
    return t

UPPER_MAIN = _fill()

VU1_SPECIAL_REGS = (
    "status", "mac", "clip", "vi19", "R", "I", "Q", "P",
    "vi24_reserved", "vi25_reserved", "TPC", "vi27_reserved",
    "vi28_reserved", "vi29_reserved", "vi30_reserved", "vi31_reserved",
)

LOWER2_OPS = {
    0x00: ("LQ", "lq"),
    0x01: ("SQ", "sq"),
    0x04: ("ILW", "loadstore_imm"),
    0x05: ("ISW", "loadstore_imm"),
    0x08: ("IADDIU", "arithu"),
    0x09: ("ISUBIU", "arithu"),
    0x10: ("FCEQ", "fc_vi1"),
    0x11: ("FCSET", "fc_noreg"),
    0x12: ("FCAND", "fc_vi1"),
    0x13: ("FCOR", "fc_vi1"),
    0x14: ("FSEQ", "fs_reg"),
    0x15: ("FSSET", "fs_noreg"),
    0x16: ("FSAND", "fs_reg"),
    0x17: ("FSOR", "fs_reg"),
    0x18: ("FMEQ", "fm"),
    0x1A: ("FMAND", "fm"),
    0x1B: ("FMOR", "fm"),
    0x1C: ("FCGET", "fcget"),
    0x20: ("B", "b"),
    0x21: ("BAL", "bal"),
    0x24: ("JR", "jr"),
    0x25: ("JALR", "jalr"),
    0x28: ("IBEQ", "branch"),
    0x29: ("IBNE", "branch"),
    0x2C: ("IBLTZ", "branch_zero"),
    0x2D: ("IBGTZ", "branch_zero"),
    0x2E: ("IBLEZ", "branch_zero"),
    0x2F: ("IBGEZ", "branch_zero"),
}

LOWER1_OPS = {
    0x30: ("IADD", "iarith"),
    0x31: ("ISUB", "iarith"),
    0x32: ("IADDI", "iaddi"),
    0x34: ("IAND", "iarith"),
    0x35: ("IOR", "iarith"),
}

LOWER1_SPECIAL_OPS = {
    0x30: ("MOVE", "itof_move"),
    0x31: ("MR32", "itof_move"),
    0x34: ("LQI", "lqi"),
    0x35: ("SQI", "sqi"),
    0x36: ("LQD", "lqd"),
    0x37: ("SQD", "sqd"),
    0x38: ("DIV", "div"),
    0x39: ("SQRT", "sqrt"),
    0x3A: ("RSQRT", "rsqrt"),
    0x3B: ("WAITQ", "literal"),
    0x3C: ("MTIR", "mtir"),
    0x3D: ("MFIR", "mfir"),
    0x3E: ("ILWR", "ilwr_iswr"),
    0x3F: ("ISWR", "ilwr_iswr"),
    0x40: ("RNEXT", "r_field"),
    0x41: ("RGET", "r_field"),
    0x42: ("RINIT", "r_fsf"),
    0x43: ("RXOR", "r_fsf"),
    0x64: ("MFP", "mfp"),
    0x68: ("XTOP", "xtop"),
    0x69: ("XITOP", "xtop"),
    0x6C: ("XGKICK", "xgkick"),
    0x70: ("ESADD", "p_novec"),
    0x71: ("ERSADD", "p_novec"),
    0x72: ("ELENG", "p_novec"),
    0x73: ("ERLENG", "p_novec"),
    0x74: ("EATANXY", "p_novec"),
    0x75: ("EATANXZ", "p_novec"),
    0x76: ("ESUM", "p_novec"),
    0x78: ("ESQRT", "p_fsf"),
    0x79: ("ERSQRT", "p_fsf"),
    0x7A: ("ERCPR", "p_fsf"),
    0x7B: ("WAITP", "literal"),
    0x7C: ("ESIN", "p_fsf"),
    0x7D: ("EATAN", "p_fsf"),
    0x7E: ("EEXP", "p_fsf"),
}
