from tools.vu.cli import main

def test_macro_mode(capsys):
    rc = main(["macro", "0x03e4ae0"])
    out = capsys.readouterr().out
    assert rc == 0
    assert "vf" in out.lower()

def test_micro_mode(tmp_path, capsys):
    import struct
    upper_end = (1<<30) | ((0xF<<21)|(1<<16)|(2<<11)|(3<<6)|0x28)
    blob = struct.pack("<II", 0, upper_end)
    p = tmp_path / "prog.bin"
    p.write_bytes(blob)
    rc = main(["micro", str(p), "--base", "0x1000"])
    out = capsys.readouterr().out
    assert rc == 0
    assert "1000" in out and "ADD" in out

def test_selfcheck(capsys):
    rc = main(["selfcheck"])
    assert rc == 0
    assert "OK" in capsys.readouterr().out
