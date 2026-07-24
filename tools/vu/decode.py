from tools.vu.fields import fields, dest_suffix, BC
from tools.vu.tables import UPPER_MAIN, FTOI_BITS

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
