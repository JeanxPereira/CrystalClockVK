# SP1 Interpreter Runs — eerun discovery on 0x00232618 (2026-07-06)

> Status tags per master-strategy §6: [LIVE-VERIFIED] = read directly from
> `re/ram/clock/eeMemory.bin` (captured EE RAM) or produced by `eerun` against
> it; [HYPOTHESIS] = inferred, not yet cross-checked; [FALSIFIED] = disproven
> this session.

## Summary [LIVE-VERIFIED]

`bin\eerun.exe re\ram\clock\eeMemory.bin 232618 --trace --dump-stores re\ram\clock\stores_232618.bin`
exits 0: **retired 688 instructions**, full call graph, MMIO trace showing the
GIF DMA kick, and store extents that include the packet buffer at 0x00297220.
No hardware wall was hit — every gap was a missing opcode.

## Ops added this session [LIVE-VERIFIED, each has a hand-assembled unit test]

All added to `src/ee/EeInterpreter.cpp`, tests appended to
`tests/EeInterpreterTest.cpp` (poke() pattern, cases 5-7):

1. **SPECIAL fn=0x0F (`sync`)** — memory barrier; no-op in this single-threaded
   interpreter. First failure: `pc=0026E5E0 word=0000000F`.
2. **COP2 macro broadcast group (fn 0x00-0x1B)** — generalized the existing
   narrow `vmaddx/y/z/w` (fn 0x08-0x0B) case into the full broadcast family:
   `subop = fn>>2`, `bc = fn&3`: 0=VADDbc, 1=VSUBbc, 2=VMADDbc (preserved),
   3=VMSUBbc, 4=VMAXbc, 5=VMINIbc, 6=VMULbc. fn 0x1C-0x1F (VMULq/VMAXi/VMULi/
   VMINIi, which broadcast off I/Q registers rather than vft) are explicitly
   left unimplemented (throw) — not hit this run. First failure that triggered
   this: `pc=0026E7E4 word=4846E800` (rs=2→dest=Z, rt=6, rd=8, sa=29, fn=0 →
   VADDx.z vf29, vf8, vf6x).
3. **SPECIAL fn=0x1B (`divu`)** — unsigned divide, lo=quot, hi=rem, divide-by-
   zero → lo=hi=0 (matches existing `div` convention already in the file).
   First failure: `pc=0022F85C word=0045001B` (divu $2,$5).

No MMIO/hardware-register wall was hit that required inventing a value: every
MMIO access encountered was a **read of an unimplemented register returning 0**
(the existing `EeMemory` default), which is exactly the "not busy" value these
DMA-wait polling loops want, so execution proceeded past them without needing
a fake busy-bit model. Nothing was silently guessed — see MMIO list below,
values are exactly what the interpreter's default (0) produced, logged in full.

## a0-protocol decision [LIVE-VERIFIED by disassembly, contradicts naive brief guess]

The brief flagged a judgment point: does `0x00232618` take `a0` as a
context/state pointer? **Disassembled the function body directly from
`re/ram/clock/eeMemory.bin` with Capstone (MIPS64LE mode)** — see
`0x00232618`-`0x002326A0`:

```
00232618: addiu $sp, $sp, -0x20
0023261C: lui   $a2, 0x1f
00232620: sd    $s0, ($sp)
00232624: lui   $a1, 0x2000
00232628: sd    $ra, 0x10($sp)
0023262C: lui   $s0, 0x29
00232630: addiu $s0, $s0, 0x7220
00232634: move  $a0, $zero          ; <-- a0 is DISCARDED here, not read first
00232638: lw    $v0, 0xc50($a2)     ; a2 = 0x001F0000 fixed global base
0023263C: or    $s0, $s0, $a1       ; s0 = 0x00297220 | 0x20000000 = 0x20297220
...                                  ; builds the rect (12.4 fixed, <<4 shifts)
00232688: jal   0x00230518
0023268C: sw    $v1, 0x2c($s0)      ; delay slot
00232690: move  $a0, $s0            ; a0 REWRITTEN to the buffer for the tail call
00232694: ld    $ra, 0x10($sp)
00232698: ld    $s0, ($sp)
0023269C: j     0x0022fd00
002326A0: addiu $sp, $sp, 0x20
```

**Decision: 0x232618 takes no meaningful arguments.** The buffer address
`0x20297220` is a compile-time constant (`lui 0x29 / addiu 0x7220 / or
0x20000000`), not derived from `a0`, `s0` on entry, or `gp`. `a0` on entry is
immediately zeroed (`move $a0, $zero` at 0x232634) and only reused afterward
once overwritten with `s0` (the buffer) to feed the `0x0022FD00` tail call.
Confirmed empirically: `eerun` runs with `a0=0` and `a0=0x20297220` produced
byte-identical instruction counts, call graphs, MMIO traces, and store logs —
consistent with `a0` being dead on entry. sp0-live-reads.md's "s0 =
0x20297220 set by a CALLER" [reinterpreted]: `s0` is *not* propagated in from
the caller here — it's rebuilt from scratch inside 0x232618 every call. The
prior doc's phrasing implied a caller-supplied value; this run shows the
constant is self-contained. Not calling the caller-behavior claim FALSIFIED
(the caller may still separately set `s0` before some other path), but for
*this* entry point the a0/s0-from-caller theory is not what's happening.

## eerun's own bug found and fixed [LIVE-VERIFIED]

The brief's reference implementation of the store-extent merge (Step 1 code)
had a latent bug: it merges consecutive `StoreRecord`s if
`s.physAddr <= ext.back().second + 16`, with no lower-bound check. Since
`storeLog` is chronological, not address-sorted, a store at a much *lower*
physical address than the current extent (e.g. the packet buffer at
0x00297240 following stack saves at 0x01FF7FE0) satisfied that inequality
trivially (small addr ≤ large addr + 16) and got silently swallowed into the
unrelated stack extent instead of starting its own. Fixed in
`tools/eerun/main.cpp` by requiring `s.physAddr >= ext.back().first` too.
Verified by re-running: before the fix, the discovery run reported a single
24-byte extent near the stack; after the fix, 37 extents appear including
the packet buffer and a 0x00296E00-region extent (see below).

## MMIO accesses — the GIF/DMA kick [LIVE-VERIFIED]

Full trace (`--trace` run) shows the DMA channel-2 (GIF) kick, twice (matches
the "begin → append → submit, ×2" pattern in sp0-live-reads.md):

```
MMIO R 10009000 / R 1000A000 / R 10003C00 / R 10003020 / R 1000A000   (poll/checks)
MMIO W 1000A020 size 4 val 3          ; D2_QWC  = 3 quadwords
MMIO W 1000A010 size 4 val 296DF0     ; D2_MADR = 0x00296DF0 (source addr!)
MMIO W 1000A000 size 4 val 101        ; D2_CHCR = 0x101 (STR=1, start transfer)
```

`0x1000A000` region = D2 (GIF) DMA channel registers (CHCR/MADR/QWC at
+0x00/+0x10/+0x20 respectively, standard EE DMAC layout). `MADR=0x296DF0` sits
right next to the GIF-tag template constants at `0x00296dd0/0x00296de0` that
sp0-live-reads.md already flagged as DMA-chain template data — this is the
"kick," i.e. this run reaches an actual `D2_CHCR` STR=1 write, not just a
simulated/no-op path. The rest of the MMIO trace (`0x10000000`-`0x1000004C`,
`0x10009000` region, `0x1FFF8A0C`) reads as further DMA-channel setup/poll
traffic (channel base registers + a busy-flag-style scratch address) for a
second DMA-chain build — [HYPOTHESIS] exact channel-register semantics beyond
D2 not individually decoded this session; not required for the completion
criterion, and no value was invented — every read returned the pre-existing
`EeMemory` default of 0, logged verbatim.

## Store extents (post-fix) [LIVE-VERIFIED]

37 extents total; key ones:

```
00297240..00297250 (16 bytes)   <- the packet-buffer rect: s0+0x20/0x24/0x28/0x2c
                                     (s0 = 0x20297220 -> phys 0x00297220)
00296E00..00296E50 (64 bytes)   <- staging buffer near the GIF-tag templates
                                     at 0x00296dd0/0x00296de0 (sp0-live-reads.md)
01FF7FE0..01FF7FF8 and many small 01FF7Fxx ranges  <- stack frame saves/restores
                                     across the call chain (routine prologues)
```

Full extent list and MMIO log are reproducible via the command above; not
duplicated file-by-file here.

## Call graph (`--trace`) [LIVE-VERIFIED]

```
0022F720 x2   0022F7F8 x2   0022FB28 x1   0022FBE8 x1
00230518 x1   0026E590 x1   0026E700 x3   00272FA0 x2
```

Matches sp0-live-reads.md's documented spine
`0x0022FD00: F720(sp) -> FB28(sp,buf) -> F7F8(sp) -> F720(sp) -> FBE8(sp,buf) -> F7F8(sp)`
(begin/append/submit ×2) — `0022FD00` itself is a plain `j` tail-jump (not
`jal`), so it doesn't appear in the trace list (only `jal`/`jalr` targets are
recorded), but its callees do, and the ×2 pattern of F720/F7F8 confirms the
two-pass structure. `00230518` (called once from 0x232618 directly) and its
descendants `0026E590`, `0026E700` (×3), `00272FA0` (×2) are new leads for
later SP1 work — not decoded further this session (out of scope: task was to
run to completion, not to name every callee).

## Files

- `tools/eerun/main.cpp` — the discovery CLI (per brief, with the extent-merge
  fix above).
- `src/ee/EeInterpreter.cpp` — sync, COP2 broadcast group, divu.
- `tests/EeInterpreterTest.cpp` — cases 5 (sync), 6 (VADDx), 7 (divu).
- `re/ram/clock/stores_232618.bin` — dumped store-extent contents (gitignored,
  under `re/`).
