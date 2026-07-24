import re
from tools.vu.decode import decode_upper
from tools.vu.reference.vu_micro_ref import ref_upper
from tools.vu.tests.corpus import ROTATION, PROJECTION, funct_sweep, funct_sweep_dest_zero, special_sweep

def norm(s: str) -> list:
    return re.sub(r"[,\s]+", " ", s.strip().lower()).split()

def _check(word):
    ours = norm(decode_upper(word))
    theirs = norm(ref_upper(word))
    assert ours == theirs, f"word=0x{word:07x} ours={ours} ref={theirs}"

def test_rotation_corpus():
    for w in ROTATION: _check(w)

def test_projection_corpus():
    for w in PROJECTION: _check(w)

def test_funct_sweep():
    for w in funct_sweep(): _check(w)

def test_funct_sweep_dest_zero():
    for w in funct_sweep_dest_zero(): _check(w)

def test_special_sweep():
    words = special_sweep()
    assert len(words) == 128
    for w in words: _check(w)
