from tools.vu.fields import dest_mask, dest_suffix, fields, BC

def test_dest_mask_full():
    assert dest_mask(0xF) == "xyzw"

def test_dest_mask_partial():
    assert dest_mask(0xE) == "xyz"
    assert dest_mask(0x8) == "x"
    assert dest_mask(0x1) == "w"
    assert dest_mask(0x0) == ""

def test_dest_suffix():
    assert dest_suffix(0xF) == ".xyzw"
    assert dest_suffix(0x0) == ""

def test_bc_names():
    assert BC == ("x", "y", "z", "w")

def test_fields_extraction():
    # dest=0xF(31..21bits), ft=3, fs=2, fd=1, funct=0x28 (ADD)
    w = (0xF << 21) | (3 << 16) | (2 << 11) | (1 << 6) | 0x28
    f = fields(w)
    assert f == {"funct": 0x28, "fd": 1, "fs": 2, "ft": 3, "dest": 0xF, "bc": 0x0}
