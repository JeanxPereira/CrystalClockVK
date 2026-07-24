# Vendored Reference Decoder

`vu_micro_ref.py` is adapted from the IDA Pro plugin
[Goatman13/ps2_ida_vu_micro](https://github.com/Goatman13/ps2_ida_vu_micro),
an open-source PS2 Vector Unit (VU0/VU1) micro-instruction disassembler for IDA.

## Purpose

This file is a validation oracle for `tools/vu`. Its dispatch tables and bit-field
math are kept byte-for-byte identical to the original plugin logic (this is the
whole point — it must decode instructions exactly as the original does), only the
IDA integration layer was removed.

## Adaptation from the original

- Removed all IDA API imports and calls (`ida_bytes`, `idaapi`, `idc`,
  `set_manual_insn`, `get_dword`, `warning`, `add_cref`, `print`-as-diagnostics,
  the plugin/action-registration classes, and the VIF/DMA memory scanner
  `mark_code` and friends). None of that is decode logic; it is IDA UI/analysis
  plumbing that has no place in the pure oracle.
- Every function that used to call `set_manual_insn(address, string)` now
  `return`s the assembled mnemonic string instead of writing it into IDA.
- Two new public entry points, `ref_upper(word, loi_word=None)` and
  `ref_lower(word)`, dispatch exactly like the original `upper()`/`lower()`
  but operate on a single 32-bit instruction word passed by the caller instead
  of reading live process/database memory.
- `upper()`'s original LOI handling read the preceding dword directly from the
  IDA database (`get_dword(address - 4)`) to render the float literal. Since
  the harness only supplies one word at a time, `ref_upper` takes an optional
  `loi_word` parameter; when the I-bit (`word >> 31 == 1`) is set and
  `loi_word` is given, the same `struct.pack('>I', ...)` / `unpack('>f', ...)`
  conversion is used to render a `loi ...` line ahead of the decoded op. When
  `loi_word` is `None`, the LOI line is skipped and only the op is decoded.
- `skip_vif_data()` originally scanned the live VU memory image via `get_dword`
  to skip over interleaved VIF/DMA tag data when resolving absolute branch
  targets. With no memory image available to a pure single-word decoder, it is
  stubbed to always return `0` (no bytes skipped). Branch target math
  (`address + imm*8 + 8`, etc.) is otherwise unchanged, so decoded branch
  mnemonics are correct for code with no interleaved VIF data, and will be
  off by the skipped amount otherwise. Flagged as a known limitation.
- `lower()` originally peeked at the *next* dword (`get_dword(address + 4)`)
  to detect whether the paired upper instruction carries the I-bit, and if so
  skipped decoding entirely (the word is float payload, not a real lower
  instruction). This check needs data external to the current word and is not
  reproducible from `ref_lower(word)`'s single-word contract, so it was
  dropped; `ref_lower` always dispatches lower1/lower2 on the given word.
- `get_4bit_field()`'s malformed-input branch called IDA's `warning()`; it now
  silently falls back to `"xyzw"`, matching the original's return value.

## Not vendored

The IDA plugin scaffolding (`ActionHandler`, `register_actions`,
`vu_helper_t`, `PLUGIN_ENTRY`, `vu_single_line`, `vu_mpg_4A`, `mark_code`,
`calculate_mpg_size`, `new_skip_vif_data`) has no decode logic and was not
ported; it exists only to wire the plugin into IDA's UI and to walk raw VIF/DMA
packet streams.
