BC = ("x", "y", "z", "w")

def dest_mask(dest: int) -> str:
    s = ""
    if dest & 0x8: s += "x"
    if dest & 0x4: s += "y"
    if dest & 0x2: s += "z"
    if dest & 0x1: s += "w"
    return s

def dest_suffix(dest: int) -> str:
    m = dest_mask(dest)
    return "." + m if m else ""

def fields(word: int) -> dict:
    return {
        "funct": word & 0x3F,
        "fd": (word >> 6) & 0x1F,
        "fs": (word >> 11) & 0x1F,
        "ft": (word >> 16) & 0x1F,
        "dest": (word >> 21) & 0xF,
        "bc": word & 0x3,
    }
