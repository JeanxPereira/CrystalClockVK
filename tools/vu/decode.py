import struct
from dataclasses import dataclass
from tools.vu.fields import fields, dest_suffix, dest_field_str, BC
from tools.vu.tables import UPPER_MAIN, FTOI_BITS, LOWER1_OPS, LOWER1_SPECIAL_OPS, LOWER2_OPS, VU1_SPECIAL_REGS

# OPMSUB's dest field is decoded but the reference hardcodes ".xyz" regardless
# of its actual value (see opmsub() in vu_micro_ref.py) - matched here, not invented.
def _std(name, f):
    suffix = ".xyz" if name == "OPMSUB" else dest_suffix(f['dest'])
    return f"{name}{suffix} vf{f['fd']}, vf{f['fs']}, vf{f['ft']}"

def _bc(name, f):
    c = BC[f["bc"]]
    return f"{name}{c}{dest_suffix(f['dest'])} vf{f['fd']}, vf{f['fs']}, vf{f['ft']}{c}"

def _qi(name, reg, f):
    return f"{name}{reg.lower()}{dest_suffix(f['dest'])} vf{f['fd']}, vf{f['fs']}, {reg}"

# SPECIAL sub-op layout matches reference upper_special(): op = (fd<<2)|bc.
# Corrections vs the brief's transcription (validated against reference):
#   - fd=0x09,bc=0 (VSUBAq) is unhandled in reference -> returns "" (gap kept, not invented).
#   - fd=0x0A,bc=3 is unhandled in reference (NOT VOPMULA) -> returns "".
#   - VOPMULA is actually fd=0x0B,bc=2, fixed ".xyz" suffix, no bc suffix on operands.
#   - VNOP is fd=0x0B,bc=3 (brief had this right, but bc=2 in between was unhandled).
#   - VCLIPw uses the actual dest-field mask (not hardcoded ".xyz").
#   - fd=0x0C (VMOVE) / fd=0x0D (VMR32) belong to LOWER1_SPECIAL, not UPPER_SPECIAL
#     -> reference returns None for them here, so decode_upper returns "".
def _special(word, f):
    fd = f["fd"]; bc = f["bc"]; d = dest_suffix(f["dest"])
    ft = f["ft"]; fs = f["fs"]
    op = (fd << 2) | bc

    if op <= 0x03:
        return f"ADDA{BC[bc]}{d} ACC, vf{fs}, vf{ft}{BC[bc]}"
    if op <= 0x07:
        return f"SUBA{BC[bc]}{d} ACC, vf{fs}, vf{ft}{BC[bc]}"
    if op <= 0x0B:
        return f"MADDA{BC[bc]}{d} ACC, vf{fs}, vf{ft}{BC[bc]}"
    if op <= 0x0F:
        return f"MSUBA{BC[bc]}{d} ACC, vf{fs}, vf{ft}{BC[bc]}"
    if op == 0x10:
        return f"ITOF{FTOI_BITS[0]}{d} vf{ft}, vf{fs}"
    if op == 0x11:
        return f"ITOF{FTOI_BITS[1]}{d} vf{ft}, vf{fs}"
    if op == 0x12:
        return f"ITOF{FTOI_BITS[2]}{d} vf{ft}, vf{fs}"
    if op == 0x13:
        return f"ITOF{FTOI_BITS[3]}{d} vf{ft}, vf{fs}"
    if op == 0x14:
        return f"FTOI{FTOI_BITS[0]}{d} vf{ft}, vf{fs}"
    if op == 0x15:
        return f"FTOI{FTOI_BITS[1]}{d} vf{ft}, vf{fs}"
    if op == 0x16:
        return f"FTOI{FTOI_BITS[2]}{d} vf{ft}, vf{fs}"
    if op == 0x17:
        return f"FTOI{FTOI_BITS[3]}{d} vf{ft}, vf{fs}"
    if 0x18 <= op <= 0x1B:
        return f"MULA{BC[bc]}{d} ACC, vf{fs}, vf{ft}{BC[bc]}"
    if op == 0x1C:
        return f"MULAq{d} ACC, vf{fs}, Q"
    if op == 0x1D:
        return f"ABS{d} vf{ft}, vf{fs}"
    if op == 0x1E:
        return f"MULAi{d} ACC, vf{fs}, I"
    if op == 0x1F:
        return f"CLIPw{d} vf{fs}, vf{ft}w"
    if op == 0x20:
        return f"ADDAq{d} ACC, vf{fs}, Q"
    if op == 0x21:
        return f"MADDAq{d} ACC, vf{fs}, Q"
    if op == 0x22:
        return f"ADDAi{d} ACC, vf{fs}, I"
    if op == 0x23:
        return f"MADDAi{d} ACC, vf{fs}, I"
    if op == 0x25:
        return f"MSUBAq{d} ACC, vf{fs}, Q"
    if op == 0x26:
        return f"SUBAi{d} ACC, vf{fs}, I"
    if op == 0x27:
        return f"MSUBAi{d} ACC, vf{fs}, I"
    if op == 0x28:
        return f"ADDA{d} ACC, vf{fs}, vf{ft}"
    if op == 0x29:
        return f"MADDA{d} ACC, vf{fs}, vf{ft}"
    if op == 0x2A:
        return f"MULA{d} ACC, vf{fs}, vf{ft}"
    if op == 0x2C:
        return f"SUBA{d} ACC, vf{fs}, vf{ft}"
    if op == 0x2D:
        return f"MSUBA{d} ACC, vf{fs}, vf{ft}"
    if op == 0x2E:
        return f"OPMULA.xyz ACC, vf{fs}, vf{ft}"
    if op == 0x2F:
        return "NOP"
    return ""

def decode_upper(word: int) -> str:
    f = fields(word)
    name, shape = UPPER_MAIN.get(f["funct"], (None, None))
    if name is not None and name.startswith("V"):
        name = name[1:]
    if shape == "std": return _std(name, f)
    if shape == "bc":  return _bc(name, f)
    if shape == "q":   return _qi(name, "Q", f)
    if shape == "i":   return _qi(name, "I", f)
    if shape == "special": return _special(word, f)
    return ""

def decode_macro_cop2(operand: int) -> str:
    return decode_upper(operand & 0x1FFFFFF)

def get_vu1_reg(imm: int) -> str:
    imm &= 0x3F
    if imm < 0x20:
        return f"vu1_vf{imm}"
    if imm < 0x30:
        return f"vu1_vi{imm - 0x20}"
    return "vu1_" + VU1_SPECIAL_REGS[imm - 0x30]

def _imm11_addr(imm: int, base: int):
    sign = ""
    if imm > 0x3FF and base != 0:
        imm = (~imm) & 0x3FF
        imm += 1
        sign = "-"
    elif imm > 0x3FF and base == 0:
        return None, None, get_vu1_reg(imm)
    imm *= 16
    return imm, sign, None

def _branch_addr(imm: int) -> int:
    if imm > 0x3FF:
        imm &= 0x3FF
        imm = (~imm) & 0x3FF
        imm *= 8
        return -imm
    imm *= 8
    return imm + 8

def _lq(name, word):
    is_ = (word >> 11) & 0x1F
    ft = (word >> 16) & 0x1F
    d = dest_suffix((word >> 21) & 0xF)
    scaled, sign, vu1 = _imm11_addr(word & 0x7FF, is_)
    if vu1 is not None:
        return f"{name}{d} vf{ft}, {vu1}"
    return f"{name}{d} vf{ft}, {sign}0x{scaled:X}(vi{is_})"

def _sq(name, word):
    it = (word >> 16) & 0x1F
    fs = (word >> 11) & 0x1F
    d = dest_suffix((word >> 21) & 0xF)
    scaled, sign, vu1 = _imm11_addr(word & 0x7FF, it)
    if vu1 is not None:
        return f"{name}{d} vf{fs}, {vu1}"
    return f"{name}{d} vf{fs}, {sign}0x{scaled:X}(vi{it})"

def _loadstore_imm(name, word):
    is_ = (word >> 11) & 0x1F
    it = (word >> 16) & 0x1F
    d = dest_suffix((word >> 21) & 0xF)
    scaled, sign, vu1 = _imm11_addr(word & 0x7FF, is_)
    if vu1 is not None:
        return f"{name}{d} vi{it}, {vu1}"
    return f"{name}{d} vi{it}, {sign}0x{scaled:X}(vi{is_})"

def _arithu(name, word):
    source = (word >> 11) & 0x1F
    dest = (word >> 16) & 0x1F
    imm = (word & 0x7FF) | (((word >> 21) & 0xF) << 11)
    return f"{name} vi{dest}, vi{source}, 0x{imm:X}"

def _fc_vi1(name, word):
    return f"{name} vi1, 0x{word & 0xFFFFFF:X}"

def _fc_noreg(name, word):
    return f"{name} 0x{word & 0xFFFFFF:X}"

def _fs_reg(name, word):
    imm = ((word >> 10) & 0x800) | (word & 0x7FF)
    dest = (word >> 16) & 0x1F
    return f"{name} vi{dest}, 0x{imm:X}"

def _fs_noreg(name, word):
    imm = ((word >> 10) & 0x800) | (word & 0x7FF)
    return f"{name} 0x{imm:X}"

def _fm(name, word):
    is_ = (word >> 11) & 0x1F
    it = (word >> 16) & 0x1F
    return f"{name} vi{it}, vi{is_}"

def _fcget(name, word):
    it = (word >> 16) & 0x1F
    return f"{name} vi{it}"

def _b(name, word):
    addr = _branch_addr(word & 0x7FF)
    return f"{name} 0x{addr:X}"

def _bal(name, word):
    link = (word >> 16) & 0x1F
    addr = _branch_addr(word & 0x7FF)
    return f"{name} vi{link} 0x{addr:X}"

def _jr(name, word):
    reg = (word >> 11) & 0x1F
    return f"{name} vi{reg}"

def _jalr(name, word):
    link = (word >> 16) & 0x1F
    reg = (word >> 11) & 0x1F
    return f"{name} vi{link}, vi{reg}"

def _branch(name, word):
    is_ = (word >> 11) & 0x1F
    it = (word >> 16) & 0x1F
    addr = _branch_addr(word & 0x7FF)
    return f"{name} vi{it}, vi{is_}, 0x{addr:X}"

def _branch_zero(name, word):
    reg = (word >> 11) & 0x1F
    addr = _branch_addr(word & 0x7FF)
    return f"{name} vi{reg}, 0x{addr:X}"

_LOWER2_DISPATCH = {
    "lq": _lq,
    "sq": _sq,
    "loadstore_imm": _loadstore_imm,
    "arithu": _arithu,
    "fc_vi1": _fc_vi1,
    "fc_noreg": _fc_noreg,
    "fs_reg": _fs_reg,
    "fs_noreg": _fs_noreg,
    "fm": _fm,
    "fcget": _fcget,
    "b": _b,
    "bal": _bal,
    "jr": _jr,
    "jalr": _jalr,
    "branch": _branch,
    "branch_zero": _branch_zero,
}

def _iarith(name, word):
    dest = (word >> 6) & 0xF
    reg1 = (word >> 11) & 0xF
    reg2 = (word >> 16) & 0xF
    return f"{name} vi{dest}, vi{reg1}, vi{reg2}"

def _iaddi(name, word):
    reg1 = (word >> 11) & 0xF
    dest = (word >> 16) & 0xF
    imm5 = (word >> 6) & 0x1F
    sign = ""
    if imm5 > 0xF:
        imm5 = (~imm5) & 0xF
        imm5 += 1
        sign = "-"
    return f"{name} vi{dest}, vi{reg1}, {sign}0x{imm5:X}"

def _itof_move(name, word):
    source = (word >> 11) & 0x1F
    dest = (word >> 16) & 0x1F
    d = dest_suffix((word >> 21) & 0xF)
    return f"{name}{d} vf{dest}, vf{source}"

def _lqi(name, word):
    is_ = (word >> 11) & 0xF
    ft = (word >> 16) & 0x1F
    d = dest_suffix((word >> 21) & 0xF)
    return f"{name}{d} vf{ft}, (vi{is_}++)"

def _sqi(name, word):
    fs = (word >> 11) & 0x1F
    it = (word >> 16) & 0xF
    d = dest_suffix((word >> 21) & 0xF)
    return f"{name}{d} vf{fs}, (vi{it}++)"

def _lqd(name, word):
    is_ = (word >> 11) & 0xF
    ft = (word >> 16) & 0x1F
    d = dest_suffix((word >> 21) & 0xF)
    return f"{name}{d} vf{ft}, (--vi{is_})"

def _sqd(name, word):
    fs = (word >> 11) & 0x1F
    it = (word >> 16) & 0xF
    d = dest_suffix((word >> 21) & 0xF)
    return f"{name}{d} vf{fs}, (--vi{it})"

def _div(name, word):
    reg1 = (word >> 11) & 0x1F
    reg2 = (word >> 16) & 0x1F
    fsf = BC[(word >> 21) & 0x3]
    ftf = BC[(word >> 23) & 0x3]
    return f"{name} Q, vf{reg1}{fsf} vf{reg2}{ftf}"

def _sqrt(name, word):
    source = (word >> 16) & 0x1F
    ftf = BC[(word >> 23) & 0x3]
    return f"{name} Q, vf{source}{ftf}"

def _rsqrt(name, word):
    reg1 = (word >> 11) & 0x1F
    reg2 = (word >> 16) & 0x1F
    fsf = BC[(word >> 21) & 0x3]
    ftf = BC[(word >> 23) & 0x3]
    return f"{name} Q, vf{reg1}{fsf} vf{reg2}{ftf}"

def _mtir(name, word):
    fs = (word >> 11) & 0x1F
    it = (word >> 16) & 0xF
    fsf = BC[(word >> 21) & 0x3]
    return f"{name} vi{it}, vf{fs}{fsf}"

def _mfir(name, word):
    is_ = (word >> 11) & 0x1F
    ft = (word >> 16) & 0x1F
    d = dest_suffix((word >> 21) & 0xF)
    return f"{name}{d} vf{ft}, vi{is_}"

def _ilwr_iswr(name, word):
    is_ = (word >> 11) & 0x1F
    it = (word >> 16) & 0x1F
    field = (word >> 21) & 0xF
    d = dest_suffix(field)
    raw = dest_field_str(field)
    return f"{name}{d} vi{it}, (vi{is_}){raw}"

def _r_field(name, word):
    dest = (word >> 16) & 0x1F
    d = dest_suffix((word >> 21) & 0xF)
    return f"{name}{d} vf{dest}, R"

def _r_fsf(name, word):
    source = (word >> 11) & 0x1F
    fsf = BC[(word >> 21) & 0x3]
    return f"{name} R, vf{source}.{fsf}"

def _mfp(name, word):
    dest = (word >> 16) & 0x1F
    d = dest_suffix((word >> 21) & 0xF)
    return f"{name}{d} vf{dest}, P"

def _xtop(name, word):
    it = (word >> 16) & 0x1F
    return f"{name} vi{it}"

def _xgkick(name, word):
    is_ = (word >> 11) & 0x1F
    return f"{name} vi{is_}"

def _p_novec(name, word):
    source = (word >> 11) & 0x1F
    return f"{name} P, vf{source}"

def _p_fsf(name, word):
    source = (word >> 11) & 0x1F
    fsf = BC[(word >> 21) & 0x3]
    return f"{name} P, vf{source}.{fsf}"

def _literal(name, word):
    return name

_LOWER1_SPECIAL_DISPATCH = {
    "itof_move": _itof_move,
    "lqi": _lqi,
    "sqi": _sqi,
    "lqd": _lqd,
    "sqd": _sqd,
    "div": _div,
    "sqrt": _sqrt,
    "rsqrt": _rsqrt,
    "literal": _literal,
    "mtir": _mtir,
    "mfir": _mfir,
    "ilwr_iswr": _ilwr_iswr,
    "r_field": _r_field,
    "r_fsf": _r_fsf,
    "mfp": _mfp,
    "xtop": _xtop,
    "xgkick": _xgkick,
    "p_novec": _p_novec,
    "p_fsf": _p_fsf,
}

def _lower1_special(word: int) -> str:
    op = (word & 0x3) | ((word >> 4) & 0x7C)
    entry = LOWER1_SPECIAL_OPS.get(op)
    if entry is None:
        return ""
    name, shape = entry
    return _LOWER1_SPECIAL_DISPATCH[shape](name, word)

def _lower1(word: int) -> str:
    op = word & 0x3F
    entry = LOWER1_OPS.get(op)
    if entry is not None:
        name, shape = entry
        return _iaddi(name, word) if shape == "iaddi" else _iarith(name, word)
    if 0x3C <= op <= 0x3F:
        return _lower1_special(word)
    return ""

def _lower2(word: int) -> str:
    op = (word >> 25) & 0x7F
    entry = LOWER2_OPS.get(op)
    if entry is None:
        return ""
    name, shape = entry
    return _LOWER2_DISPATCH[shape](name, word)

def decode_lower(word: int) -> str:
    if word == 0x8000033C:
        return "NOP"
    if word & (1 << 31):
        return _lower1(word)
    return _lower2(word)

@dataclass
class Instr:
    address: int
    lower: int
    upper: int
    lower_str: str
    upper_str: str
    is_end: bool

def decode_micro(lower_word: int, upper_word: int) -> tuple[str, str]:
    up = decode_upper(upper_word)
    lo = decode_lower(lower_word)
    return lo, up

def decode_program(blob: bytes, base_addr: int = 0) -> list[Instr]:
    out = []
    for i in range(0, len(blob) - 7, 8):
        lower, upper = struct.unpack_from("<II", blob, i)
        is_end = bool(upper & (1 << 30))
        lo, up = decode_micro(lower, upper)
        out.append(Instr(base_addr + i, lower, upper, lo, up, is_end))
        if is_end:
            break
    return out
