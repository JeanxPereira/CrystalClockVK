import re
from tools.vu.decode import decode_lower
from tools.vu.reference.vu_micro_ref import ref_lower

def norm(s): return re.sub(r"[,\s]+"," ",s.strip().lower()).split()

def _text(s): return s if s is not None else ""

def test_lower_sweep():
    # sweep the top-6-bit lower opcode with fixed operands; compare to reference
    for op in range(0x40):
        w = (op << 25) | (1 << 16) | (2 << 11) | (3 << 6)
        assert norm(_text(decode_lower(w))) == norm(_text(ref_lower(w))), f"op=0x{op:02x}"
