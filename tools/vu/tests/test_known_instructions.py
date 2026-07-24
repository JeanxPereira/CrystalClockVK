from tools.vu.decode import decode_upper

def enc(dest, ft, fs, fd, funct):
    return (dest<<21)|(ft<<16)|(fs<<11)|(fd<<6)|funct

def test_vadd_xyzw():
    assert decode_upper(enc(0xF, 1, 2, 3, 0x28)) == "ADD.xyzw vf3, vf2, vf1"

def test_vmul_std():
    assert decode_upper(enc(0xF, 4, 5, 6, 0x2A)) == "MUL.xyzw vf6, vf5, vf4"

def test_vopmsub():
    assert decode_upper(enc(0xE, 7, 8, 9, 0x2E)) == "OPMSUB.xyz vf9, vf8, vf7"

def test_vaddx_bc():
    assert decode_upper(enc(0x8, 5, 2, 3, 0x00)) == "ADDx.x vf3, vf2, vf5x"

def test_vmulq():
    assert decode_upper(enc(0xF, 0, 2, 3, 0x1C)) == "MULq.xyzw vf3, vf2, Q"

def test_vmuli():
    assert decode_upper(enc(0xF, 0, 2, 3, 0x1E)) == "MULi.xyzw vf3, vf2, I"
