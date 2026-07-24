BC = ("x", "y", "z", "w")

def dest_mask(dest: int) -> str:
    s = ""
    if dest & 0x8: s += "x"
    if dest & 0x4: s += "y"
    if dest & 0x2: s += "z"
    if dest & 0x1: s += "w"
    return s

def dest_field_str(dest: int) -> str:
    # reference get_4bit_field() falls through to "xyzw" for field==0
    # (vu_micro_ref.py lines 61-62), so a zero dest mask still yields a suffix
    return dest_mask(dest) or "xyzw"

def dest_suffix(dest: int) -> str:
    return "." + dest_field_str(dest)

def fields(word: int) -> dict:
    return {
        "funct": word & 0x3F,
        "fd": (word >> 6) & 0x1F,
        "fs": (word >> 11) & 0x1F,
        "ft": (word >> 16) & 0x1F,
        "dest": (word >> 21) & 0xF,
        "bc": word & 0x3,
    }
