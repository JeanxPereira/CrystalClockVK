from tools.vu.decode import decode_program, Instr

def load_and_decode(path: str, base_addr: int = 0) -> list[Instr]:
    with open(path, "rb") as fh:
        return decode_program(fh.read(), base_addr)

def format_listing(instrs) -> str:
    lines = []
    for ins in instrs:
        end = "  [E]" if ins.is_end else ""
        lines.append(f"0x{ins.address:08x}: {ins.upper_str:<32} | {ins.lower_str}{end}")
    return "\n".join(lines)
