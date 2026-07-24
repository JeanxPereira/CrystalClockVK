from tools.vu.decode import decode_micro, decode_program, Instr

def test_decode_micro_pairs_lower_then_upper():
    # lower=NOP encoding, upper=VADD.xyzw vf3, vf2, vf1
    upper = (0xF<<21)|(1<<16)|(2<<11)|(3<<6)|0x28
    lower = 0x8000033C
    lo, up = decode_micro(lower, upper)
    assert up == "ADD.xyzw vf3, vf2, vf1"
    assert isinstance(lo, str)

def test_decode_program_end_bit():
    import struct
    upper_end = (1<<30) | ((0xF<<21)|(1<<16)|(2<<11)|(3<<6)|0x28)  # E-bit set
    lower = 0x00000000
    blob = struct.pack("<II", lower, upper_end)
    prog = decode_program(blob, base_addr=0x1000)
    assert len(prog) == 1
    assert prog[0].address == 0x1000
    assert prog[0].is_end is True
    assert prog[0].upper_str.startswith("ADD")
