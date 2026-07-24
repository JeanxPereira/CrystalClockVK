# VU Decoder — Design Spec

Date: 2026-07-24
Status: proposed

## Motivation

Reverse-engineering the OSDSYS clock and icon rendering keeps hitting VU
(Vector Unit) code that Ghidra's r5900 module cannot disassemble:

1. **Macro-mode COP2** — VU0 instructions run inline on the EE. Ghidra
   labels them `cop1` with a raw 25-bit operand. We decode these by hand
   today with `docs/ghidra_analysis/decode_vu0.py`.
2. **Micro-mode microprograms** — VU0/VU1 microcode uploaded via VIF
   `MPG` unpacks (e.g. the icon-lighting rig at VU dmem, source `0x2A5A60`).
   These are 64-bit doublewords (upper|lower) and we currently cannot
   read them at all.

The existing `decode_vu0.py` was written by trial-and-error and never
validated against a reference, so `docs/ghidra_analysis/vu0_decode.md`
(which builds the "axis-angle rotation" conclusion on top of it) may
carry decode errors. We need a correct, validated VU decoder plus a way
to pull microprograms out of the ELF.

## Goal

A small, tested Python package `tools/vu/` that decodes both VU input
forms into correct assembly text, validated instruction-for-instruction
against an external reference decoder, plus a Ghidra Java script that
carves VU microprograms out of `hddosd.elf` into `.bin` + metadata for
the decoder to consume. Then re-decode the clock matrix builders and
correct the analysis docs.

## Non-goals

- No VU emulation/execution — decode/disassemble only.
- No GUI. CLI + importable functions.
- Not wired into the C++ build; this is an RE tooling package.
- Lower-pipeline decode is in scope for microprograms, but macro-mode
  COP2 (our Ghidra case) is upper-only by nature — lower ops there are
  ordinary EE instructions Ghidra already handles.

## Reference sources (ground truth, in priority order)

1. Sony VU Users Manual / BigBoss VU Instruction Manual — the encoding
   authority. `http://lukasz.dk/files/vu-instruction-manual.pdf`
2. `Goatman13/ps2_ida_vu_micro` `_vu_micro.py` — a working VU
   disassembler (field extraction confirmed identical to ours:
   `fd=(w>>6)&0x1F`, `fs=(w>>11)&0x1F`, `ft=(w>>16)&0x1F`,
   `dest=(w>>21)&0xF`, `funct=w&0x3F`). Fetched to scratchpad; will be
   vendored as `tools/vu/reference/vu_micro_ref.py` for the validation
   harness.
3. `chaoticgd/vutrace` `vudis` — standalone VU disassembler; optional
   second cross-check on real microprogram bytes.
4. PCSX2 `microVU` opcode tables — tertiary tiebreaker.

## Architecture

Four small units under `tools/vu/`, each independently testable:

### `tables.py` — the opcode data, no logic
Pure data transcribed from the manual and cross-checked against the
reference decoder:
- `UPPER_FUNCT`: funct (bits 5-0) → (mnemonic, operand-shape) for the
  standard ops (VADD/VSUB/VMUL/VMADD/VMSUB/VMAX/VMINI), the bc-broadcast
  block, the Q/I block, and the SPECIAL block (funct 0x3C-0x3F with
  fd-as-subop for VADDA/VMADDA/VMULA/VSUBA/VOPMSUB/VOPMULA/VFTOI*/
  VITOF*/VCLIP/VABS/VMOVE/VMR32/VNOP etc).
- `LOWER_OP`: the lower-pipeline table (LQ/SQ/ILW/ISW/IADD/ISUB/IADDI/
  branches/DIV/SQRT/RSQRT/WAITQ/MFIR/MTIR/MOVE/MFP/XGKICK/XTOP/XITOP/
  LOI/etc), keyed by the lower opcode fields.
- Field-mask helpers (`dest_str`) and bc/component name tables.

### `decode.py` — the decoder logic
- `decode_upper(word: int) -> str` — 32-bit upper instruction. (The
  Ghidra macro-COP2 case passes the 25-bit `cop1` operand with the
  implicit `CO=1`; expose `decode_macro_cop2(operand: int)` that adapts
  it to `decode_upper`.)
- `decode_lower(word: int) -> str` — 32-bit lower instruction, including
  the I-bit / LOI immediate handling (upper-instruction bit 31 flags a
  32-bit float immediate in the lower slot).
- `decode_micro(doubleword: bytes|int) -> (upper_str, lower_str)` — one
  64-bit VU micro instruction (lower word then upper word per ps2tek).
- `decode_program(blob: bytes, base_addr=0) -> list[Instr]` — a whole
  microprogram; each `Instr` carries address, raw words, and both
  decoded strings. Stops/annotates on the `[E]` end-bit.

### `dump_vu.py` — carve microprograms from a dumped region
Given a `.bin` (from the Ghidra script or a PCSX2 savestate
vu1MicroMem.bin) plus a base address, run `decode_program` and emit a
listing. Also accepts raw ELF offsets when the caller knows the MPG
target.

### `cli.py` — entry point
`python -m tools.vu <mode> ...`:
- `macro <hexlist>` — decode Ghidra `cop1` operands (replaces the
  hardcoded arrays in the old `decode_vu0.py`).
- `micro <file.bin> [--base 0x...]` — decode a microprogram blob.
- `selfcheck` — run the validation harness (see Testing).

### Ghidra companion: `tools/vu/DumpVuMicro.java`
Scans `hddosd.elf` for VU microprogram uploads:
- Find VIF `MPG` unpack tags (VIFcode cmd `0x4A`) and the DMA/`FUN_0023a580`
  style upload sites (VIF1 TADR/CHCR writes) that reference known dmem
  targets (e.g. `0x2A5A60`).
- For each, carve the referenced bytes to `tools/vu/dumps/<name>.bin`
  and append a row to `tools/vu/dumps/index.json` with:
  ELF address, VU dmem target address, byte length, and a guessed name.
- Print a summary. Idempotent (re-run overwrites the same files).

## Data flow

```
hddosd.elf ──DumpVuMicro.java──> dumps/*.bin + index.json
                                        │
Ghidra cop1 arrays ──┐                  ▼
                     ├──> tools/vu ──> assembly listing (.txt / stdout)
dumps/*.bin ─────────┘
                     ▲
      vu_micro_ref.py (validation only, not in decode path)
```

## Testing (this is the point of the rewrite)

- **Golden cross-check** (`test_against_reference.py`): for a corpus of
  words — every value from the existing `rotation_build` /
  `projection_build` arrays, plus a generated sweep covering each funct
  and each SPECIAL subop — assert `decode_upper(w)` matches the vendored
  reference decoder's output (normalized for formatting). Any mismatch
  is a bug in our table or theirs; investigate and record the verdict in
  a comment. This is the test that would have caught the old decoder's
  errors.
- **Manual spot-checks** (`test_known_instructions.py`): a handful of
  hand-verified encodings from the Sony manual (e.g. a known VADD.xyzw,
  VOPMSUB, VFTOI4, a lower LQ and a branch) asserted against expected
  strings, so the reference and our decoder can't be wrong the same way.
- **Round-trip on a real program**: decode the icon-lighting
  microprogram blob end to end; assert it terminates on an end-bit and
  produces no `V???`/`UNKNOWN` opcodes (every instruction decoded).
- No C++/engine tests — this package doesn't touch the build.

## Deliverable docs

- Re-run the corrected decoder on `rotation_build` / `projection_build`
  and rewrite `docs/ghidra_analysis/vu0_decode.md` to match, explicitly
  flagging any conclusion from the old doc that the corrected decode
  overturns (the "axis-angle via VOPMSUB" claim gets re-verified or
  retracted).
- Retire `docs/ghidra_analysis/decode_vu0.py` (superseded by `tools/vu`);
  leave a one-line pointer.

## Risks

- The old decoder's SPECIAL-block (funct 0x3C-0x3F, fd-as-subop) mapping
  is the most error-prone area and the reason for the golden cross-check.
- MPG-tag scanning may miss microprograms uploaded by paths the script
  doesn't model; the script logs what it found and the total bytes so
  gaps are visible, and blobs can also be supplied from PCSX2 savestate
  dumps as a fallback.
- Reference decoder is IDA-oriented (uses `set_manual_insn` etc.); we
  vendor only its pure decode tables/logic, adapted to return strings.
