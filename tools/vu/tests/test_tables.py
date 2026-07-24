from tools.vu.tables import UPPER_MAIN, FTOI_BITS

def test_main_standard_ops():
    assert UPPER_MAIN[0x28] == ("VADD", "std")
    assert UPPER_MAIN[0x2A] == ("VMUL", "std")
    assert UPPER_MAIN[0x2E] == ("VOPMSUB", "std")
    assert UPPER_MAIN[0x2F] == ("VMINI", "std")

def test_main_bc_block():
    for f in range(0x00, 0x04): assert UPPER_MAIN[f] == ("VADD", "bc")
    for f in range(0x18, 0x1C): assert UPPER_MAIN[f] == ("VMUL", "bc")
    for f in range(0x10, 0x14): assert UPPER_MAIN[f] == ("VMAX", "bc")

def test_main_qi_block():
    assert UPPER_MAIN[0x1C] == ("VMUL", "q")
    assert UPPER_MAIN[0x1E] == ("VMUL", "i")
    assert UPPER_MAIN[0x20] == ("VADD", "q")
    assert UPPER_MAIN[0x23] == ("VMADD", "i")

def test_special_range_marked():
    for f in range(0x3C, 0x40): assert UPPER_MAIN[f][1] == "special"

def test_ftoi_bits():
    assert FTOI_BITS == {0: 0, 1: 4, 2: 12, 3: 15}
