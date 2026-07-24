# VU Decoder Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A tested Python package `tools/vu/` that correctly decodes PS2 VU macro-COP2 and micro (64-bit) instructions, validated instruction-for-instruction against a vendored reference decoder, plus a Ghidra script that carves VU microprograms from `hddosd.elf`, then corrected analysis docs.

**Architecture:** Pure-data opcode tables (`tables.py`) feed stateless decode functions (`decode.py`); a CLI (`cli.py`) and a blob loader (`dump_vu.py`) drive them. Correctness is enforced by a golden cross-check against `ps2_ida_vu_micro`'s decoder over the real clock instruction corpus plus a generated funct sweep. A Ghidra Java companion produces the microprogram blobs the decoder consumes.

**Tech Stack:** Python 3.11+, pytest, standard library only (struct). Ghidra Java for the dump script.

## Global Constraints

- Python standard library only — no third-party runtime deps (pytest is dev-only).
- English only; commit convention `Type(Scope): imperative subject ≤72 chars`; no AI attribution anywhere.
- Decode/disassemble only — no VU emulation.
- Field extraction is fixed by the ISA and MUST be: `funct = w & 0x3F`, `fd = (w>>6)&0x1F`, `fs = (w>>11)&0x1F`, `ft = (w>>16)&0x1F`, `dest = (w>>21)&0xF`, `bc = w & 0x3` (for broadcast ops).
- The vendored reference decoder (`ps2_ida_vu_micro/_vu_micro.py`, in scratchpad at `C:\Users\dell04\AppData\Local\Temp\claude\C--CodingProjects-Personal-CrystalClockVK\a6b1799c-e8e9-41db-9c6f-eb6fafd7bbec\scratchpad\vu_micro_ref.py`) is ground truth for the upper-instruction opcode dispatch. Its confirmed main table: funct 0x00-0x03=ADDbc, 0x04-0x07=SUBbc, 0x08-0x0B=MADDbc, 0x0C-0x0F=MSUBbc, 0x10-0x13=MAXbc, 0x14-0x17=MINIbc, 0x18-0x1B=MULbc, 0x1C=MULq, 0x1D=MAXi, 0x1E=MULi, 0x1F=MINIi, 0x20=ADDq, 0x21=MADDq, 0x22=ADDi, 0x23=MADDi, 0x24=SUBq, 0x25=MSUBq, 0x26=SUBi, 0x27=MSUBi, 0x28=ADD, 0x29=MADD, 0x2A=MUL, 0x2B=MAX, 0x2C=SUB, 0x2D=MSUB, 0x2E=OPMSUB, 0x2F=MINI, 0x3C-0x3F=SPECIAL.
- Reference decoder is IDA-oriented (`set_manual_insn`, `get_dword`); vendor only its pure decode logic, adapted to RETURN strings. It is used ONLY by the test harness, never imported by the decode path.

## Notes for all tasks
- Package lives at repo-root `tools/vu/` with `__init__.py`, run tests with `python -m pytest tools/vu/tests/ -v` from repo root.
- Mnemonic case: emit UPPERCASE mnemonics with dest mask suffix, e.g. `VADD.xyzw vf1, vf2, vf3`. The golden cross-check normalizes case and whitespace before comparing, so exact spacing need not match the reference — only tokens.

---

### Task 1: Package scaffold + vendored reference + field extraction

**Files:**
- Create: `tools/vu/__init__.py`
- Create: `tools/vu/fields.py`
- Create: `tools/vu/reference/__init__.py`
- Create: `tools/vu/reference/vu_micro_ref.py` (vendored, adapted)
- Create: `tools/vu/reference/NOTICE.md`
- Create: `tools/vu/tests/__init__.py`
- Create: `tools/vu/tests/test_fields.py`

**Interfaces:**
- Produces `tools/vu/fields.py`:
```python
def dest_mask(dest: int) -> str    # 0..0xF -> "xyzw" subset, e.g. 0xE -> "xyz", 0 -> ""
def dest_suffix(dest: int) -> str  # "." + dest_mask, or "" when mask empty
BC = ("x", "y", "z", "w")          # broadcast component names
def fields(word: int) -> dict      # {funct, fd, fs, ft, dest, bc}
```
- Produces `tools/vu/reference/ref_upper(word:int)->str` and `ref_lower(word:int)->str`: pure functions returning the reference decoder's mnemonic string (no IDA calls).

- [ ] **Step 1: Write the failing test**

`tools/vu/tests/test_fields.py`:
```python
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
```

- [ ] **Step 2: Run to verify it fails**

Run: `python -m pytest tools/vu/tests/test_fields.py -v`
Expected: FAIL (ModuleNotFoundError: tools.vu.fields)

- [ ] **Step 3: Implement fields.py**

```python
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
```
Create empty `tools/vu/__init__.py`, `tools/vu/tests/__init__.py`, `tools/vu/reference/__init__.py`.

- [ ] **Step 4: Vendor the reference decoder**

Copy the scratchpad file `vu_micro_ref.py` into `tools/vu/reference/vu_micro_ref.py`, then adapt it to be import-safe and return strings:
- Remove/replace all IDA API calls (`set_manual_insn`, `get_dword`, `get_qword`, `idaapi`, `idc` imports) — the functions currently call `set_manual_insn(address, string)`; change them to `return string` and drop the `address` write side effects.
- Expose two module-level entry points `ref_upper(word:int)->str` and `ref_lower(word:int)->str` that dispatch exactly as the original `upper()`/`lower()` do but return the assembled mnemonic string instead of writing it.
- For LOI handling in `upper()` (the `instruction>>31==1` branch that reads the previous dword): since the harness passes single words, `ref_upper` takes an optional `loi_word:int|None=None` param and formats the LOI line from it when the I-bit is set; when `None`, skip the LOI annotation.
- Keep the original dispatch/table logic byte-for-byte otherwise — this file is the oracle; do not "improve" it.
Write `tools/vu/reference/NOTICE.md` crediting `Goatman13/ps2_ida_vu_micro` (source URL https://github.com/Goatman13/ps2_ida_vu_micro) and noting local adaptation to return strings.

- [ ] **Step 5: Run to verify pass + reference imports**

Run: `python -m pytest tools/vu/tests/test_fields.py -v`
Expected: PASS (4 tests)
Run: `python -c "from tools.vu.reference.vu_micro_ref import ref_upper; print(ref_upper((0xF<<21)|(3<<16)|(2<<11)|(1<<6)|0x28))"`
Expected: prints an ADD mnemonic string (e.g. `add.xyzw vf1, vf2, vf3`) with no IDA import error.

- [ ] **Step 6: Commit**

```bash
git add tools/vu/
git commit -m "Build(Project): Scaffold VU decoder package and vendor reference"
```

---

### Task 2: Upper-instruction opcode tables

**Files:**
- Create: `tools/vu/tables.py`
- Create: `tools/vu/tests/test_tables.py`

**Interfaces:**
- Consumes: `fields.BC`.
- Produces `tools/vu/tables.py`:
```python
# funct (bits 5-0) -> ("MNEMONIC", shape) where shape in:
#   "bc"   : VOPbc.dest vfFD, vfFS, vfFT<bc>       (funct low 2 bits pick x/y/z/w)
#   "std"  : VOP.dest   vfFD, vfFS, vfFT
#   "q"    : VOP.dest   vfFD, vfFS, Q
#   "i"    : VOP.dest   vfFD, vfFS, I
#   "special": dispatched by SPECIAL_SUBOP
UPPER_MAIN: dict[int, tuple[str, str]]
# SPECIAL block: funct 0x3C-0x3F. Sub-op is keyed by (fd_hi, bc-ish bits) per the
# reference decoder's upper_special(); expose a decoder hook, not a flat dict, since
# the special block mixes fd-as-subop with bc. Provide:
SPECIAL: object  # see decode task; tables.py only holds the raw name maps it needs
```
Keep `tables.py` PURE DATA + tiny name lookups. The SPECIAL dispatch logic lives in `decode.py`; `tables.py` provides the name maps SPECIAL needs (e.g. `FTOI_BITS = {0:0,1:4,2:12,3:15}` for VFTOI/VITOF suffixes, and the subop→mnemonic maps transcribed from the reference `upper_special`).

- [ ] **Step 1: Write the failing test**

`tools/vu/tests/test_tables.py`:
```python
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
```

- [ ] **Step 2: Run to verify it fails**

Run: `python -m pytest tools/vu/tests/test_tables.py -v`
Expected: FAIL (ModuleNotFoundError)

- [ ] **Step 3: Implement tables.py**

Transcribe the main table from the Global Constraints funct list (verified against the reference decoder). Full listing:
```python
FTOI_BITS = {0: 0, 1: 4, 2: 12, 3: 15}

def _fill():
    t = {}
    for f in range(0x00, 0x04): t[f] = ("VADD", "bc")
    for f in range(0x04, 0x08): t[f] = ("VSUB", "bc")
    for f in range(0x08, 0x0C): t[f] = ("VMADD", "bc")
    for f in range(0x0C, 0x10): t[f] = ("VMSUB", "bc")
    for f in range(0x10, 0x14): t[f] = ("VMAX", "bc")
    for f in range(0x14, 0x18): t[f] = ("VMINI", "bc")
    for f in range(0x18, 0x1C): t[f] = ("VMUL", "bc")
    t[0x1C] = ("VMUL", "q")
    t[0x1D] = ("VMAX", "i")
    t[0x1E] = ("VMUL", "i")
    t[0x1F] = ("VMINI", "i")
    t[0x20] = ("VADD", "q")
    t[0x21] = ("VMADD", "q")
    t[0x22] = ("VADD", "i")
    t[0x23] = ("VMADD", "i")
    t[0x24] = ("VSUB", "q")
    t[0x25] = ("VMSUB", "q")
    t[0x26] = ("VSUB", "i")
    t[0x27] = ("VMSUB", "i")
    t[0x28] = ("VADD", "std")
    t[0x29] = ("VMADD", "std")
    t[0x2A] = ("VMUL", "std")
    t[0x2B] = ("VMAX", "std")
    t[0x2C] = ("VSUB", "std")
    t[0x2D] = ("VMSUB", "std")
    t[0x2E] = ("VOPMSUB", "std")
    t[0x2F] = ("VMINI", "std")
    for f in range(0x3C, 0x40): t[f] = ("SPECIAL", "special")
    return t

UPPER_MAIN = _fill()
```
Note `VMAXi` at 0x1D and `VMINIi` at 0x1F use MAX/MINI with "i" shape — encode as `("VMAX","i")` / `("VMINI","i")`; the decode task appends the `i` suffix from the shape.

- [ ] **Step 4: Run to verify pass**

Run: `python -m pytest tools/vu/tests/test_tables.py -v`
Expected: PASS (6 tests)

- [ ] **Step 5: Commit**

```bash
git add tools/vu/tables.py tools/vu/tests/test_tables.py
git commit -m "Build(Project): Add VU upper-instruction opcode tables"
```

---

### Task 3: Upper decoder + macro-COP2 adapter, golden cross-check

**Files:**
- Create: `tools/vu/decode.py`
- Create: `tools/vu/tests/test_known_instructions.py`
- Create: `tools/vu/tests/test_against_reference.py`
- Create: `tools/vu/tests/corpus.py`

**Interfaces:**
- Consumes: `tables.UPPER_MAIN`, `tables.FTOI_BITS`, `fields.*`, `reference.vu_micro_ref.ref_upper`.
- Produces `tools/vu/decode.py`:
```python
def decode_upper(word: int) -> str          # 32-bit upper instruction -> mnemonic
def decode_macro_cop2(operand: int) -> str  # 25-bit Ghidra cop1 operand (CO=1 implicit) -> mnemonic
```
`decode_macro_cop2` just calls `decode_upper(operand & 0x1FFFFFF)` (the 25 bits ARE the upper instruction's low bits; the field extraction already masks correctly).

- [ ] **Step 1: Write the manual spot-check test**

`tools/vu/tests/test_known_instructions.py` — hand-verified encodings (independent of the reference so both can't be wrong the same way):
```python
from tools.vu.decode import decode_upper

def enc(dest, ft, fs, fd, funct):
    return (dest<<21)|(ft<<16)|(fs<<11)|(fd<<6)|funct

def test_vadd_xyzw():
    # VADD.xyzw vf3, vf2, vf1  (funct 0x28)
    assert decode_upper(enc(0xF, 1, 2, 3, 0x28)) == "VADD.xyzw vf3, vf2, vf1"

def test_vmul_std():
    assert decode_upper(enc(0xF, 4, 5, 6, 0x2A)) == "VMUL.xyzw vf6, vf5, vf4"

def test_vopmsub():
    assert decode_upper(enc(0xE, 7, 8, 9, 0x2E)) == "VOPMSUB.xyz vf9, vf8, vf7"

def test_vaddx_bc():
    # funct 0x00 -> VADDx, broadcast component x, ft register 5
    assert decode_upper(enc(0x8, 5, 2, 3, 0x00)) == "VADDx.x vf3, vf2, vf5x"

def test_vmulq():
    # funct 0x1C -> VMULq, uses Q register
    assert decode_upper(enc(0xF, 0, 2, 3, 0x1C)) == "VMULq.xyzw vf3, vf2, Q"

def test_vmuli():
    # funct 0x1E -> VMULi, uses I register
    assert decode_upper(enc(0xF, 0, 2, 3, 0x1E)) == "VMULi.xyzw vf3, vf2, I"
```
(If the exact operand-order/format below differs from what you implement, keep the format but make it consistent — then the reference cross-check in Step 4 is the real gate. Pick this format: `MNEMONIC[bc-suffix].dest vfFD, vfFS, {vfFT[bc] | Q | I}`.)

- [ ] **Step 2: Run to verify it fails**

Run: `python -m pytest tools/vu/tests/test_known_instructions.py -v`
Expected: FAIL (ModuleNotFoundError)

- [ ] **Step 3: Implement decode_upper + SPECIAL block**

```python
from tools.vu.fields import fields, dest_suffix, BC
from tools.vu.tables import UPPER_MAIN, FTOI_BITS

def _std(name, f):
    return f"{name}{dest_suffix(f['dest'])} vf{f['fd']}, vf{f['fs']}, vf{f['ft']}"

def _bc(name, f):
    c = BC[f["bc"]]
    return f"{name}{c}{dest_suffix(f['dest'])} vf{f['fd']}, vf{f['fs']}, vf{f['ft']}{c}"

def _qi(name, reg, f):
    return f"{name}{reg.lower()}{dest_suffix(f['dest'])} vf{f['fd']}, vf{f['fs']}, {reg}"

def _special(word, f):
    # SPECIAL: funct 0x3C-0x3F. Sub-op = bits [10:6] (fd) with bc in [1:0].
    # Transcribed and validated against reference upper_special().
    fd = f["fd"]; bc = f["bc"]; d = dest_suffix(f["dest"])
    ft = f["ft"]; fs = f["fs"]
    if fd == 0x04:  # ITOF
        return f"VITOF{FTOI_BITS[bc]}{d} vf{ft}, vf{fs}"
    if fd == 0x05:  # FTOI
        return f"VFTOI{FTOI_BITS[bc]}{d} vf{ft}, vf{fs}"
    if fd == 0x06:  # MULA family (bc)
        return f"VMULA{BC[bc]}{d} ACC, vf{fs}, vf{ft}{BC[bc]}"
    if fd == 0x07:
        if bc == 0: return f"VMULAq{d} ACC, vf{fs}, Q"
        if bc == 1: return f"VABS{d} vf{ft}, vf{fs}"
        if bc == 2: return f"VMULAi{d} ACC, vf{fs}, I"
        if bc == 3: return f"VCLIPw.xyz vf{fs}, vf{ft}w"
    if fd == 0x08:
        return [f"VADDAq", f"VMADDAq", f"VADDAi", f"VMADDAi"][bc] + f"{d} ACC, vf{fs}, " + ("Q" if bc in (0,1) else "I")
    if fd == 0x09:
        return [f"VSUBAq", f"VMSUBAq", f"VSUBAi", f"VMSUBAi"][bc] + f"{d} ACC, vf{fs}, " + ("Q" if bc in (0,1) else "I")
    if fd == 0x0A:
        if bc == 0: return f"VADDA{d} ACC, vf{fs}, vf{ft}"
        if bc == 1: return f"VMADDA{d} ACC, vf{fs}, vf{ft}"
        if bc == 2: return f"VMULA{d} ACC, vf{fs}, vf{ft}"
        if bc == 3: return f"VOPMULA.xyz ACC, vf{fs}, vf{ft}"
    if fd == 0x0B:
        if bc == 0: return f"VSUBA{d} ACC, vf{fs}, vf{ft}"
        if bc == 1: return f"VMSUBA{d} ACC, vf{fs}, vf{ft}"
        if bc == 3: return "VNOP"
    if fd == 0x0C: return f"VMOVE{d} vf{ft}, vf{fs}"
    if fd == 0x0D: return f"VMR32{d} vf{ft}, vf{fs}"
    # accumulator-broadcast (fd 0x00-0x03)
    if fd in (0x00, 0x01, 0x02, 0x03):
        base = ["VADDA", "VSUBA", "VMADDA", "VMSUBA"][fd]
        return f"{base}{BC[bc]}{d} ACC, vf{fs}, vf{ft}{BC[bc]}"
    return f"VSPECIAL fd=0x{fd:02x} bc={bc}{d} ft=vf{ft} fs=vf{fs}"

def decode_upper(word: int) -> str:
    f = fields(word)
    name, shape = UPPER_MAIN.get(f["funct"], ("V???", None))
    if shape == "std": return _std(name, f)
    if shape == "bc":  return _bc(name, f)
    if shape == "q":   return _qi(name, "Q", f)
    if shape == "i":   return _qi(name, "I", f)
    if shape == "special": return _special(word, f)
    return f"V??? funct=0x{f['funct']:02x}"

def decode_macro_cop2(operand: int) -> str:
    return decode_upper(operand & 0x1FFFFFF)
```
IMPORTANT: the SPECIAL sub-op layout above is the plan author's best transcription. **The reference cross-check (Step 5) is the authority** — if it flags a SPECIAL mismatch, fix `_special` to match the reference's `upper_special()`, since that is the validated oracle. Record each correction as a comment citing the reference line.

- [ ] **Step 4: Run the manual spot-checks**

Run: `python -m pytest tools/vu/tests/test_known_instructions.py -v`
Expected: PASS (6 tests). If format differs, align the test strings to your chosen consistent format (must still be human-correct per the Sony manual mnemonics).

- [ ] **Step 5: Write + run the golden cross-check**

`tools/vu/tests/corpus.py` — the real instruction words from the clock builders:
```python
# Every value from docs/ghidra_analysis/decode_vu0.py rotation_build + projection_build.
ROTATION = [0x11a35a5,0x117c6e9,0x1155689,0x112e49a,0x1107130,0x10dfc61,0x10b8640,
0x1090ee0,0x1069658,0x1041cbb,0x0fe4d23,0x0f9545c,0x0f45a0b,0x0ef5e59,0x0ea616d,
0x0e5636e,0x0e06483,0x0db64d6,0x0d66489,0x0d163c8,0x0cc62b7,0x0c7617e,0x0c26040,
0x0bd5f28,0x0b85e58,0x0b35df7,0x0ae5e2c,0x0a95f1b,0x0a460ea,0x09f63be,0x09a67ba,
0x0956d05,0x09073c2,0x08b7c17,0x0868626,0x0819213,0x0794006,0x06f602e,0x06584e8,
0x05bae77,0x051dd22,0x048112f,0x03e4ae0]
# projection_build values:
PROJECTION = [0x191493d,0x191ed4b,0x1928c38,0x1932605,0x193bab5,0x1944a48,0x194d4c2,
0x1955a24,0x195da70,0x19655a9,0x196cbd1,0x1973ceb,0x197a8f9,0x1980fff,0x19871ff,
0x198cefd,0x19926fc,0x19979ff,0x199c80a,0x19a1121,0x19a5547,0x19a9481,0x19aced1,
0x19b043c,0x19b34cc,0x19b607b,0x19b8755,0x19ba95d,0x19bc697,0x19bdf08,0x19bf2b6,
0x19c01a5,0x19c0bdc,0x19c115e,0x19c1233,0x19c0e5e,0x19c05e7,0x19bf8d3,0x19be728,
0x19bd0eb,0x19bb623,0x19b96d6,0x19b730b,0x19b4ac8,0x19b1e13,0x19aecf3,0x19ab770,
0x19a7d90,0x19a3f5a,0x199fcd6,0x199b60a,0x1996afd,0x1991bb7,0x198c83d,0x1987092,
0x19814d5,0x197b4f2,0x19750fd,0x196e8fe,0x1967cfb,0x1960cfc,0x195990a,0x195212d,
0x194a56d,0x19425d3,0x193a267,0x1931b31,0x192903a,0x192018a,0x1916f2b,0x190d926,
0x1903f82,0x18fa249,0x18f0184,0x18e5d3d,0x18db57b,0x18d0a49,0x18c5baf,0x18ba9b8,
0x18af46b,0x18a3bd4,0x1897ffa,0x188c0e9,0x187fea8,0x1873943,0x1862d18,0x185abe3,
0x184ceca,0x183fd2a,0x1832893,0x182510e,0x18176a7]

def funct_sweep():
    # one representative word per funct value 0x00-0x3F with fixed dest/regs
    return [(0xF<<21)|(1<<16)|(2<<11)|(3<<6)|funct for funct in range(0x40)]
```

`tools/vu/tests/test_against_reference.py`:
```python
import re
from tools.vu.decode import decode_upper
from tools.vu.reference.vu_micro_ref import ref_upper
from tools.vu.tests.corpus import ROTATION, PROJECTION, funct_sweep

def norm(s: str) -> list:
    # lowercase, split into tokens, strip commas — compare token sequences
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
```
Run: `python -m pytest tools/vu/tests/ -v`
Expected: ALL PASS. If any `_check` fails, the mismatch is between our decoder and the oracle — fix OUR decoder (usually `_special`) to match the reference, unless inspection of the Sony manual proves the reference wrong (then document it in a skip with a manual citation). Do not weaken `norm` to hide a real token mismatch.

- [ ] **Step 6: Commit**

```bash
git add tools/vu/decode.py tools/vu/tests/
git commit -m "GS(Project): Add validated VU upper decoder with golden cross-check"
```

---

### Task 4: Lower decoder + 64-bit micro decode + program walker

**Files:**
- Modify: `tools/vu/decode.py`
- Modify: `tools/vu/tables.py` (add `LOWER_*` maps)
- Create: `tools/vu/tests/test_lower.py`
- Create: `tools/vu/tests/test_micro.py`

**Interfaces:**
- Consumes: reference `ref_lower`.
- Produces (added to `decode.py`):
```python
def decode_lower(word: int) -> str
def decode_micro(lower_word: int, upper_word: int) -> tuple[str, str]
class Instr:  # address:int, lower:int, upper:int, lower_str:str, upper_str:str, is_end:bool
def decode_program(blob: bytes, base_addr: int = 0) -> list[Instr]
```
Micro layout per ps2tek: each 64-bit doubleword is `lower_word` (low 32) then `upper_word` (high 32); the upper word's bit 31 = I-bit (LOI), bit 30 = E (end), bit 27 = M, bit 28 = D, bit 29 = T (flag bits live in the upper word).

- [ ] **Step 1: Write failing lower test (golden cross-check)**

`tools/vu/tests/test_lower.py`:
```python
import re
from tools.vu.decode import decode_lower
from tools.vu.reference.vu_micro_ref import ref_lower

def norm(s): return re.sub(r"[,\s]+"," ",s.strip().lower()).split()

def test_lower_sweep():
    # sweep the top-6-bit lower opcode with fixed operands; compare to reference
    for op in range(0x40):
        w = (op << 25) | (1 << 16) | (2 << 11) | (3 << 6)
        assert norm(decode_lower(w)) == norm(ref_lower(w)), f"op=0x{op:02x}"
```

- [ ] **Step 2: Run to verify it fails**

Run: `python -m pytest tools/vu/tests/test_lower.py -v`
Expected: FAIL (decode_lower not defined)

- [ ] **Step 3: Implement decode_lower**

Transcribe the lower-pipeline dispatch from the reference decoder's `lower()` function into `decode_lower(word)` returning the mnemonic string, backed by `LOWER_*` name maps added to `tables.py`. Mirror the reference's opcode-field slicing exactly (the lower op is the top bits; sub-tables LQ/SQ/loadstore/branch/special1/special2 dispatch as in the reference). Because Step 1 is a pure golden cross-check, correctness = matching the reference across the full 0x00-0x3F sweep; iterate `decode_lower` until the sweep passes.

- [ ] **Step 4: Run to verify pass**

Run: `python -m pytest tools/vu/tests/test_lower.py -v`
Expected: PASS.

- [ ] **Step 5: Write + implement micro + program walker**

`tools/vu/tests/test_micro.py`:
```python
from tools.vu.decode import decode_micro, decode_program, Instr

def test_decode_micro_pairs_lower_then_upper():
    # lower=NOP-ish (op 0x2F=... pick a benign op), upper=VADD.xyzw vf3,vf2,vf1
    upper = (0xF<<21)|(1<<16)|(2<<11)|(3<<6)|0x28
    lower = 0x8000033C  # LOI-ish / whatever; just assert tuple shape + upper decodes
    lo, up = decode_micro(lower, upper)
    assert up == "VADD.xyzw vf3, vf2, vf1"
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
    assert prog[0].upper_str.startswith("VADD")
```
Implement:
```python
import struct
from dataclasses import dataclass

@dataclass
class Instr:
    address: int
    lower: int
    upper: int
    lower_str: str
    upper_str: str
    is_end: bool

def decode_micro(lower_word: int, upper_word: int) -> tuple[str, str]:
    # upper flag bits live in [31:27]; strip them for the opcode decode
    up = decode_upper(upper_word & 0x1FFFFFF | (upper_word & 0x0)  )  # opcode uses low 25? no:
    # upper instruction opcode uses full low bits; flags are separate high bits.
    up = decode_upper(upper_word)
    lo = decode_lower(lower_word)
    return lo, up

def decode_program(blob: bytes, base_addr: int = 0) -> list[Instr]:
    out = []
    for i in range(0, len(blob) - 7, 8):
        lower, upper = struct.unpack_from("<II", blob, i)
        is_end = bool(upper & (1 << 30))  # E flag
        lo, up = decode_micro(lower, upper)
        out.append(Instr(base_addr + i, lower, upper, lo, up, is_end))
        if is_end:
            break
    return out
```
NOTE: `decode_upper` reads only `funct = word & 0x3F` etc. — flag bits [31:27] do not collide with the funct/reg fields, so passing the raw `upper_word` is correct; remove the confused first `up =` line and keep the single `up = decode_upper(upper_word)`.

- [ ] **Step 6: Run to verify pass**

Run: `python -m pytest tools/vu/tests/ -v`
Expected: ALL PASS.

- [ ] **Step 7: Commit**

```bash
git add tools/vu/decode.py tools/vu/tables.py tools/vu/tests/test_lower.py tools/vu/tests/test_micro.py
git commit -m "GS(Project): Add VU lower decoder and 64-bit micro program walker"
```

---

### Task 5: CLI + blob loader

**Files:**
- Create: `tools/vu/cli.py`
- Create: `tools/vu/dump_vu.py`
- Create: `tools/vu/tests/test_cli.py`
- Create: `tools/vu/__main__.py`

**Interfaces:**
- Consumes: `decode.decode_macro_cop2`, `decode.decode_program`.
- Produces:
```python
# dump_vu.py
def load_and_decode(path: str, base_addr: int = 0) -> list[Instr]
def format_listing(instrs: list[Instr]) -> str
# cli.py
def main(argv: list[str]) -> int
```
CLI modes: `macro <hex> [<hex> ...]`, `micro <file.bin> [--base 0xADDR]`, `selfcheck`.

- [ ] **Step 1: Write failing CLI test**

`tools/vu/tests/test_cli.py`:
```python
from tools.vu.cli import main

def test_macro_mode(capsys):
    rc = main(["macro", "0x03e4ae0"])
    out = capsys.readouterr().out
    assert rc == 0
    assert "vf" in out.lower()  # decoded something

def test_micro_mode(tmp_path, capsys):
    import struct
    upper_end = (1<<30) | ((0xF<<21)|(1<<16)|(2<<11)|(3<<6)|0x28)
    blob = struct.pack("<II", 0, upper_end)
    p = tmp_path / "prog.bin"
    p.write_bytes(blob)
    rc = main(["micro", str(p), "--base", "0x1000"])
    out = capsys.readouterr().out
    assert rc == 0
    assert "1000" in out and "VADD" in out

def test_selfcheck(capsys):
    rc = main(["selfcheck"])
    assert rc == 0
    assert "OK" in capsys.readouterr().out
```

- [ ] **Step 2: Run to verify it fails**

Run: `python -m pytest tools/vu/tests/test_cli.py -v`
Expected: FAIL (ModuleNotFoundError).

- [ ] **Step 3: Implement dump_vu.py + cli.py + __main__.py**

```python
# dump_vu.py
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
```
```python
# cli.py
import sys
from tools.vu.decode import decode_macro_cop2
from tools.vu.dump_vu import load_and_decode, format_listing

def main(argv):
    if not argv:
        print("usage: macro <hex...> | micro <file> [--base 0x..] | selfcheck")
        return 2
    mode, rest = argv[0], argv[1:]
    if mode == "macro":
        for h in rest:
            w = int(h, 16)
            print(f"0x{w:07x}: {decode_macro_cop2(w)}")
        return 0
    if mode == "micro":
        path = rest[0]; base = 0
        if "--base" in rest:
            base = int(rest[rest.index("--base") + 1], 16)
        print(format_listing(load_and_decode(path, base)))
        return 0
    if mode == "selfcheck":
        import subprocess
        r = subprocess.run([sys.executable, "-m", "pytest", "tools/vu/tests/", "-q"])
        print("OK" if r.returncode == 0 else "FAIL")
        return r.returncode
    print(f"unknown mode: {mode}")
    return 2
```
```python
# __main__.py
import sys
from tools.vu.cli import main
raise SystemExit(main(sys.argv[1:]))
```

- [ ] **Step 4: Run to verify pass**

Run: `python -m pytest tools/vu/tests/test_cli.py -v`
Expected: PASS (3 tests). Then `python -m tools.vu macro 0x03e4ae0` prints a decoded line.

- [ ] **Step 5: Commit**

```bash
git add tools/vu/cli.py tools/vu/dump_vu.py tools/vu/__main__.py tools/vu/tests/test_cli.py
git commit -m "Build(Project): Add VU decoder CLI and blob loader"
```

---

### Task 6: Ghidra microprogram carver

**Files:**
- Create: `tools/vu/DumpVuMicro.java`
- Create: `tools/vu/dumps/.gitkeep`

**Interfaces:**
- Standalone Ghidra script (run via the ghidra MCP `run_script_inline` or the Script Manager against `hddosd.elf`). No Python interface; it writes files consumed by `dump_vu.py micro`.
- Produces: `tools/vu/dumps/<name>.bin` per microprogram + `tools/vu/dumps/index.json` rows `{elf_addr, vu_target, length, name}`.

- [ ] **Step 1: Write the script**

`tools/vu/DumpVuMicro.java` — scan for VIF MPG unpack tags and known upload sites. Since the ghidra MCP `run_script_inline` compiles Java, the script uses the Ghidra API:
```java
import ghidra.app.script.GhidraScript;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.address.Address;
import java.io.*;
import java.util.*;

public class DumpVuMicro extends GhidraScript {
    @Override
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        String outDir = "C:/CodingProjects/Personal/CrystalClockVK/tools/vu/dumps/";
        new File(outDir).mkdirs();
        StringBuilder idx = new StringBuilder("[\n");
        int found = 0;
        // Scan the whole image for VIFcode MPG tags: cmd byte 0x4A in the high byte
        // of a 32-bit VIFcode word: [IMMEDIATE:16][NUM:8][CMD:8], CMD==0x4A -> MPG.
        Address start = mem.getMinAddress();
        Address end = mem.getMaxAddress();
        long lo = start.getOffset(), hi = end.getOffset();
        for (long a = lo; a < hi - 8; a += 4) {
            int word;
            try { word = mem.getInt(toAddr(a)); } catch (Exception e) { continue; }
            int cmd = (word >> 24) & 0x7F;   // top bit is IRQ flag, mask it
            if (cmd != 0x4A) continue;        // MPG
            int num = (word >> 16) & 0xFF;    // count of 64-bit micro-instructions (0 => 256)
            int count = (num == 0) ? 256 : num;
            int loadAddr = word & 0xFFFF;     // dest in VU micro mem (in dwords)
            long dataAddr = a + 4;            // micro data usually follows the tag
            int bytes = count * 8;
            // sanity: require the following bytes to be readable
            byte[] buf = new byte[bytes];
            boolean ok = true;
            try { mem.getBytes(toAddr(dataAddr), buf); } catch (Exception e) { ok = false; }
            if (!ok) continue;
            String name = String.format("mpg_%08x_vu%04x", dataAddr, loadAddr);
            try (FileOutputStream fo = new FileOutputStream(outDir + name + ".bin")) {
                fo.write(buf);
            }
            idx.append(String.format(
                "  {\"elf_addr\": \"0x%08x\", \"vu_target\": \"0x%04x\", \"length\": %d, \"name\": \"%s\"},\n",
                dataAddr, loadAddr, bytes, name));
            found++;
        }
        idx.append("]\n");
        try (FileWriter fw = new FileWriter(outDir + "index.json")) { fw.write(idx.toString()); }
        println("DumpVuMicro: found " + found + " candidate MPG uploads, wrote " + outDir);
    }
}
```

- [ ] **Step 2: Run against hddosd.elf**

Run via ghidra MCP `run_script_inline` (paste the class body) against program `hddosd.elf`, OR document the Script Manager path. Expected: prints a candidate count > 0 and writes `tools/vu/dumps/index.json` + one or more `.bin` files. Record the actual count in the report; MPG scanning is heuristic — some candidates may be false positives (that's why the decoder's end-bit + no-unknown-opcode checks in Step 3 filter them).

- [ ] **Step 3: Decode one real blob end-to-end**

Pick the largest blob from `index.json` (or the one whose `vu_target` matches a known dmem address) and run:
`python -m tools.vu micro tools/vu/dumps/<name>.bin --base 0x0`
Expected: a listing that terminates on an `[E]` and contains no `V???`/`VSPECIAL fd=` unknown opcodes. If a candidate is pure garbage (no end-bit within its length, many unknowns), note it as a false-positive MPG hit in the report — do NOT tune the decoder to accommodate garbage. Record which blob decoded cleanly.

- [ ] **Step 4: Commit**

```bash
git add tools/vu/DumpVuMicro.java tools/vu/dumps/.gitkeep
git commit -m "GS(Project): Add Ghidra VU microprogram carver script"
```
(Do not commit the raw `.bin` dumps or `index.json` — add `tools/vu/dumps/*.bin` and `tools/vu/dumps/index.json` to `.gitignore` in this step; the `.gitkeep` keeps the dir.)

---

### Task 7: Correct the analysis docs, retire the old decoder

**Files:**
- Modify: `docs/ghidra_analysis/vu0_decode.md`
- Modify: `docs/ghidra_analysis/decode_vu0.py` (replace body with a pointer)
- Create: `docs/ghidra_analysis/rotation_decoded.txt` (generated listing)

**Interfaces:** none (docs only).

- [ ] **Step 1: Regenerate the corrected listing**

Run: `python -m tools.vu macro <all ROTATION hex values space-separated> > docs/ghidra_analysis/rotation_decoded.txt`
(Use the `ROTATION` list from `tools/vu/tests/corpus.py`.) Then the same for the projection words appended. Expected: a clean listing with no `V???`.

- [ ] **Step 2: Rewrite vu0_decode.md conclusions**

Compare the new `rotation_decoded.txt` against the instruction listing embedded in the current `docs/ghidra_analysis/vu0_decode.md`. For each instruction the old doc named differently, correct it. Specifically re-examine the "axis-angle via VOPMSUB" conclusion: confirm whether a `VOPMSUB`/`VOPMULA` actually appears in the corrected `rotation_build` decode.
- If it does: keep the conclusion, cite the exact corrected instruction line.
- If it does NOT (the old decode was wrong): retract it, replace with what the corrected decode actually shows, and add a note: "Prior 'axis-angle' claim was based on an unvalidated decoder and is retracted."
Add a header line to the doc: "Decode corrected 2026-07-24 via tools/vu (validated against ps2_ida_vu_micro). Superseded the ad-hoc decode_vu0.py."

- [ ] **Step 3: Retire decode_vu0.py**

Replace the entire contents of `docs/ghidra_analysis/decode_vu0.py` with:
```python
"""Superseded by tools/vu (validated VU decoder).

Use:  python -m tools.vu macro <hexwords>
The ad-hoc tables here were never validated against a reference and
contained SPECIAL-block errors; do not use. See
docs/superpowers/specs/2026-07-24-vu-decoder-design.md.
"""
```

- [ ] **Step 4: Verify docs build/readable + commit**

Run: `python -m tools.vu selfcheck`
Expected: prints `OK` (full suite green).
```bash
git add docs/ghidra_analysis/vu0_decode.md docs/ghidra_analysis/decode_vu0.py docs/ghidra_analysis/rotation_decoded.txt
git commit -m "Docs(Project): Correct VU decode analysis with validated tools/vu"
```

---

### Task 8: Final sweep

- [ ] **Step 1: Full suite + no-unknown audit**

Run: `python -m pytest tools/vu/tests/ -v`
Expected: all tests pass.
Run: `grep -rn "V???" docs/ghidra_analysis/rotation_decoded.txt` — expected: no matches.

- [ ] **Step 2: Confirm .gitignore + no stray binaries committed**

Run: `git status --porcelain tools/vu/dumps/` — expected: no `.bin` or `index.json` staged/committed.

- [ ] **Step 3: Update project memory**

Note in `docs/ghidra_analysis/` context (or the project memory) that `tools/vu` is now the canonical VU decoder and that the icon-lighting microprogram (VU dmem source ~0x2A5A60) can be dumped via `DumpVuMicro.java` and decoded. No commit needed if memory-only; otherwise fold into the Task 7 docs commit range.
