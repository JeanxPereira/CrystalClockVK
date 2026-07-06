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
  fix above), now also `--decode <stores.bin> --json <out>` (Task 6).
- `src/ee/EeInterpreter.cpp` — sync, COP2 broadcast group, divu.
- `tests/EeInterpreterTest.cpp` — cases 5 (sync), 6 (VADDx), 7 (divu).
- `re/ram/clock/stores_232618.bin` — dumped store-extent contents (gitignored,
  under `re/`).

## Task 6: decoding the captured stream — the packet is register-only, 0 draws [DUMP-MEASURED]

Decoded the actual DMA'd bytes with `eerun --decode`, reusing
`GsDumpParser::decodeGifData`/`writeJson` (already public/shared from earlier
work — no parser regression: `gsdump --verify clock_viewer.gs` output is
byte-identical before/after, still the same 2 pre-existing FAILs: TEXA +
blend-mode-count).

**Window used**: `D2_MADR=0x00296DF0`, length = `D2_QWC * 16 = 3*16 = 48`
bytes (`0x00296DF0..0x00296E20`) — the literal hardware-transfer size logged
in the MMIO trace, not a guessed range. The leading GIFtag quadword
(`0x00296DF0..0x00296E00`) is never touched by a store this run (static
template, per the earlier "GIF-tag template staging" note), so `--decode`
reconstructs it by overlaying the captured store extents on top of
`re/ram/clock/eeMemory.bin` (sibling-path convention: `--base` defaults to
`eeMemory.bin` next to the `.bin` given, matching the `re/ram/clock/` layout
used by every gate command in this doc).

**Result**: `1 giftag, 0 draws, 0 kicks`. Hand-verified byte-for-byte: the
tag is PACKED, `nloop=2 nreg=1 regs=0xe` (A+D), and its two payload quadwords
write `TEST_1` (addr 0x47, val 0x30000 → ZTE=1 ZTST=NEVER) and `ALPHA_1`
(addr 0x42, val 0x44 → A=0 B=1 C=0 D=1). **This packet is pure GS
register/blend-state setup — it carries no PRIM load and no vertex kicks.**
`vdiff --subset re/oracle/clock_sw_prims.json re/oracle/cand_232618.json` →
`0/0 candidate draws matched` (vacuously trivial: there is nothing to check
against the oracle, not a real subset-match).

**Why 0 draws, and what this implies** [DUMP-MEASURED, real finding, not a
bug in the decoder]: a full `--trace` MMIO dump of this run shows a large
block of GS-packet-shaped writes (`3F800000` = 1.0f, `80808080` = a flat
color, plus GIFtag-like words `70000000`/`87008000`/`0E081408`) landing on
addresses `0x10000000..0x1000004C`. These are **not real hardware
registers** — they are the EE Scratchpad RAM (SPR), which lives at EE
virtual `0x70000000`/`0xB0000000`+ and, after `EeMemory::translate`'s
`& 0x1FFFFFFF` mask, aliases into exactly this `0x10000xxx` range.
`EeMemory` has no SPR implementation (addresses ≥ `kRamSize` are treated as
fake MMIO, logged and discarded), so **whatever packet the renderer is
building in scratchpad — very plausibly the actual rect/SPRITE draw, given
the float/color-shaped values — is invisible to this capture.** The one GIF
DMA transfer we DID observe (D2, MADR/QWC/CHCR) is real and decodes cleanly,
but it is only the state-setup fragment; the vertex data for "the rect"
does not travel through the modeled DMA path in this run.

**Conclusion for the gate**: candidate decodes cleanly (valid GIFtag,
plausible/exact register values, matches known clock invariants — the
decoded `ALPHA`/`TEST` shapes are consistent with the blend-mode family
`gsdump --verify` already reports for the real dump). It does NOT produce a
meaningful subset match because it produces no draws at all — 0x00232618's
own D2 DMA packet is state-only. Getting an actual vertex-level subset match
requires scratchpad-RAM emulation in `EeMemory` (new work, not scoped to
this task) so the real vertex packet can be captured. Logged here per the
"real finding, not a failure to hide" rule — nothing was invented or fudged
to force a pass.

## Task 7: SPR emulation added — packet captured, still no real subset match [DUMP-MEASURED]

`EeMemory` now backs a 16KB Scratchpad-RAM window at virtual `0x70000000`
(`kSprBase`/`kSprSize`/`isSpr()`), checked in every accessor *before*
`translate()` runs, so it is no longer aliased onto the `0x10000xxx` MMIO
range and silently discarded. `0x1000A000` (the real D2/GIF DMA kick) is
regression-tested to still route to `onMmio`. Straddling the SPR window's
end (`0x70003FFC` + 8 bytes) is guarded to MMIO, mirroring the RAM
boundary-guard pattern. Unit tests: `tests/EeMemoryTest.cpp` (128-bit
roundtrip via `sprData()`, MMIO regression, straddle guard) — all pass,
`ee_memory` suite 17/17 green project-wide.

`eerun --dump-stores` now tags SPR stores with their real vaddr (not
`translate()`'d) and both the extent-dump and `--decode` overlay logic
route those bytes through the separate SPR backing store instead of the
32MB RAM buffer. `--decode` decodes the SPR content first, then the
existing main-RAM GIF window, into the same `GsCommandStream`.

**Re-running the phase gate**, this session's captured `stores_232618.bin`
now contains **31 SPR store extents** spanning `0x70000000..0x70000050` (80
bytes) — the previously-invisible writes are now genuinely captured, not
discarded. Decoding them:

```
decode: SPR content 70000000..70000050 (80 bytes)
decode: after SPR pass -> 3 draws, 3 kicks, 1 giftags
```

`vdiff --subset` → **`0/3 candidate draws matched`**.

**Honest read of the "3 draws"**: hand-decoded the raw captured header
byte-for-byte (`data[0x00..0x10]` = `03 00 00 10 00 00 00 00 00 00 00 00 03
00 00 50`): `nloop=3`, `pre=0`, `prim=0`, `flg=0` (PACKED), and the
second tag word (`b`) is exactly `0` — which `decodeGifData` interprets as
`nreg=16` (its "0 means 16" convention). `nloop=3 * nreg=16 * 16 bytes =
768 bytes` of PACKED payload is required to satisfy that header, but only
80 bytes of this run's SPR writes actually landed — the remaining ~688
bytes `decodeGifData`'s inner loop walks are the zero-filled, never-written
tail of the 16KB SPR buffer. That is exactly why the JSON candidate has 3
"draws" with every field zero (`PRIM.type=0`, all verts `x=y=0`,
`r=g=b=a=0`): the decoder is consuming untouched zero memory as if it were
real GIFtag payload, not decoding real geometry. This is a decoder/model
limitation, not a hardware fact — `decodeGifData`'s inner PACKED/REGLIST
walk has no bound against the caller's `size` (unlike the outer
`while (p+16<=size)` tag-header check), a property that was harmless for
the main-RAM GIF window (its length is QWC-precise from a real MMIO
`D2_QWC` value) but is wrong here, where no such hardware-verified length
exists for SPR content.

**Conclusion for the gate** [DUMP-MEASURED, not invented]: the core bug —
SPR writes vanishing into the MMIO/discard path — is fixed and verified:
the capture went from **0 bytes / 0 draws** to **80 real bytes across 31
store extents**, a genuine, non-vacuous improvement. But treating those 80
bytes as a ready-made GIFtag+PACKED stream starting at offset 0 does not
recover real geometry — either the true GIFtag lives at a different offset
inside the 16KB window than `0x70000000`, or this SPR region is a
field-by-field workspace/staging struct (consistent with the extent shape:
many small 4/8/16-byte stores at scattered sub-offsets, not one bulk
quadword write) that gets assembled/copied/DMA'd elsewhere in a call this
single `0x232618` invocation does not reach. **Subset match is still 0**
and the "3 draws" are spurious artifacts of walking past the captured
bytes, not real vertices — reported here rather than presented as
progress it is not. Resolving this needs either (a) RE of the SPR
workspace's true struct layout, or (b) tracing the actual consumer of this
buffer (DMA source or FIFO write) to find where/how it becomes a real
GIFtag — both out of scope for "emulate the SPR so it isn't discarded,"
which is the part this task fixed.

## Phase 2 caller spike: driving 0x00233928 [DUMP-MEASURED]

Goal: run the caller loop `0x00233928` (which is supposed to assemble the
full per-frame packet, 3801 draws per the oracle) instead of the single leaf
`0x00232618`. Same discovery loop as above: run `eerun`, decode the
`EeError`, implement the missing op with a discriminating unit test, rebuild,
repeat. All new ops in `src/ee/EeInterpreter.cpp`, tests in
`tests/EeInterpreterTest.cpp` (cases 9-13); `ee_interpreter` suite is
14/14, full project suite 17/17 green.

### Ops added this session

1. **`lwc1`/`swc1`** (opcodes 0x31/0x39) — FPU load/store to memory. First
   failure: `pc=00233958 word=E7B400A0` (`swc1 $f20, 0xA0($sp)`), the
   caller's FPU-context save in its prologue.
2. **`c.olt.s`** (COP1.S function 0x34) — ordered less-than compare, same
   implementation as the existing `c.lt.s` (NaN handling not modeled).
   First failure: `pc=00233960 word=46010034`.
3. **`syscall`** (SPECIAL fn 0x0C) — implemented as a **blanket no-op**:
   the BIOS call number (from `$v1`, positive-index convention) is recorded
   into a new `EeInterpreter::syscalls` log but no kernel/thread/DMA/GS
   state is touched, per the "don't invent hardware behavior" rule. First
   failure: `pc=00258B64 word=0000000C`, `$v1=0x64=100`. Confirmed via a
   dedicated lookup against `pcsx2/R5900OpcodeImpl.cpp`'s `R5900::bios[256]`
   name table: **syscall 100 = `FlushCache`**, a pure cache-maintenance call
   with no state an interpreter without a cache model needs to honor — a
   legitimately safe no-op, not a guess.
4. **`di`/`ei`** (COP0, rs=0x10 "CO" group, fn=0x39/0x38) — disable/enable
   interrupts. No interrupt controller is modeled, so both no-op. First
   failure: `pc=00271900`'s caller jumped into the trampoline at
   `pc=00258B60`... (actually first COP0 CO-group failure was the `di` at
   `pc=00271900`-adjacent code, word `0x42000039`).
5. **`mfc0`/`mtc0`** (COP0 rs=0x00/0x04) — added a plain 32-register
   `cop0[32]` file (no MMU/exception semantics) so `Status`-register
   save/restore around `di`/`ei` roundtrips correctly. First failure:
   `pc=00271900 word=40026000` (`mfc0 v0, $12` i.e. Status).

Also hardened `tools/eerun/main.cpp` to print `v0/v1/a0/a1` and every
recorded syscall number on both the success and `EeError` exit paths — this
is what let the syscall-number/register state be read directly from
evidence instead of guessed.

### A live-RAM vs. Ghidra static-image mismatch, noted and bypassed

Decompiling `0x00258B60` in Ghidra (`OSDSYS.elf`) returns a large, unrelated
function body (calls to `FUN_00268c00`/`FUN_0026b9d0`, stack ops at
`0x480(sp)`, etc.) that does **not** match the bytes actually present at
file/physical offset `0x00258B60` in `re/ram/clock/eeMemory.bin`, which is a
plain 4-instruction syscall trampoline (`addiu v1,zero,N` / `syscall` /
`jr ra` / `nop`, repeated every 16 bytes for syscalls 0x63, 0x64, 0x66,
-103, -104, -106...). Read directly from the RAM image with a small Python
script (not trusted from Ghidra) — see the raw dump in this session's
history. Per the project's live-evidence-over-decomp doctrine, the RAM
capture is authoritative here; this is flagged as a discrepancy between the
static Ghidra database and the live RAM capture's addressing for this
region, not resolved further (out of scope for this spike) — a note for
whoever next relies on Ghidra addresses in this range.

### STOP — genuine wall hit: an interrupt/Deci2-completion polling loop

`0x00233928` does **not** run to completion. After the ops above, execution
enters a tight loop (function around `0x00271980`-`0x002719e0`, called
repeatedly from the caller) that:

1. Computes `s0 = s1 + 0x1690` (a fixed struct address off a global base
   `s1`), then reads/writes fields of that struct (`s0+0x4`, `s0+0xc`).
2. Calls through a small wrapper at `0x0026F478` into the syscall
   trampoline at `0x00258D10` (`addiu v1,zero,0x7C` = **syscall 124**, which
   `pcsx2/R5900OpcodeImpl.cpp`'s bios-name table maps to **`Deci2Call`** —
   the PS2 debug/IOP communication link), returns, then:
3. Loops back via `bne v0,zero,-5` at `pc=0x002719e0` reading
   `lw v0, 0xc(s0)` at `0x002719dc` — i.e. **`while (*(s0+0xc) != 0) { ...
   call Deci2Call ...; }`**.

Nothing in the traced code path ever clears `*(s0+0xc)` on the taken
(normal) side of this loop — the only store that zeroes it is on the
`v0 < 0` error path (`sw zero, 0xc(s0)` at `0x002719bc`), which is itself
followed by `di` + an unconditional infinite spin (a fatal-error halt
pattern), not a real exit. On real hardware, this field is almost certainly
cleared by an **interrupt handler** reacting to the actual Deci2/IOP
response (the surrounding code installs handlers via the
`AddIntcHandler`/`SetVInterruptHandler`-style syscalls seen earlier in the
bios name table) — i.e. this loop's termination depends on interrupt-driven
hardware state this bare EE interpreter has no model for (no interrupt
controller, no IOP, no Deci2 link). Confirmed by direct instruction-level
reading of `re/ram/clock/eeMemory.bin` at the addresses above (all opcodes
and branch targets decoded by hand, not assumed).

Run without `--trace` (faster) confirms this concretely: `eerun` burns the
entire 200,000,000-instruction budget and throws `budget exceeded` at
`pc=002719D4`, having logged **millions of repeated `syscall 124` entries**
— i.e. the loop is not slow-but-finite, it is unbounded within this
interpreter's model. This is **STOP CONDITION (B)**: a genuine wall,
not a missing opcode — implementing more instructions will not make this
loop terminate; it needs either (a) a real interrupt-controller +
Deci2/IOP model (large, out of scope), or (b) discovering that this
specific loop is skippable/short-circuitable for the clock's purposes
(e.g. patching `s1`'s struct so `*(s0+0xc)` reads 0 from the start, if RE
confirms that's a debug-only code path OSDSYS only takes when a debug
station is attached — **not yet confirmed**, so not done here per the
no-invention rule).

### Call graph observed before the wall

```
0x00233928 (entry)
  -> ... FPU/COP0 prologue (swc1 x N, di, mfc0 Status) ...
  -> 0x00271900-ish region -> 0x00271980 loop:
       -> 0x0026F478 (Deci2Call wrapper)
            -> 0x00258D10 (syscall 124 = Deci2Call trampoline)
       <- loops on *(s1+0x1690+0xc) != 0, never observed to clear
```
`0x00232618` (the Task-4 leaf render entry) was **not reached** in this run
— the caller never gets past the Deci2 polling loop to whatever comes
after it, so the "does it call 0x232618 multiple times" question from the
brief is **not yet answerable**; the wall is upstream of that call site.

### State/seeding implications for Phase 2 planning

- The struct at `s1 + 0x1690` (12+ bytes, fields at `+0x4`, `+0xc` observed)
  is a live piece of state this run reads that is not naturally driven to
  a "done" value by anything modeled here. `s1` itself was not traced back
  to its origin this session (likely a fixed global base set well before
  this function, consistent with the project's other fixed-base-pointer
  patterns like `0x232618`'s `a2=0x001F0000`) — worth resolving in a
  follow-up so this struct can be seeded/faked past, if that turns out to
  be the right call.
- Syscalls 100 (`FlushCache`) and 124 (`Deci2Call`) are both confirmed by
  name via `pcsx2/R5900OpcodeImpl.cpp`'s bios table; no other syscall
  numbers were observed before the wall.
- No vertex-level subset match was attempted this session — the run never
  reaches packet-building code, so there is nothing yet to `vdiff`.

### Recommended Phase-2 first tasks (this spike's honest recommendation)

1. **RE the `Deci2Call` polling loop's real exit condition** — find what,
   on real hardware, writes `*(s0+0xc) = 0` (interrupt handler, or a
   same-frame follow-up syscall this run hasn't reached yet). This is the
   single blocker between here and resuming forward progress on
   `0x00233928`.
2. Once past it, re-run the same discovery loop toward the next wall (more
   missing opcodes are likely, e.g. more MMI ops, `lwl`/`lwr`/`swl`/`swr`,
   more COP1/COP2 forms) — none of those are expected to be hard, based on
   the pattern so far (every non-syscall gap this session was a
   straightforward, cheaply-testable instruction).
3. Investigate whether `0x00233928` is even the right caller to chase, or
   whether it's debug/deci2-instrumented scaffolding OSDSYS only runs when
   a dev station is attached (in which case a *different*, non-debug entry
   point may be the real per-frame driver — worth a quick Ghidra
   cross-reference check on who calls `0x00233928` and under what
   condition, before sinking more time into this exact path).

## Phase 2 spike 2 — sync-stub mechanism built, not yet aimed (2026-07-06)

[DUMP-MEASURED] Without a stub, 0x233928 spins forever in the Deci2 loop
(eerun exit=124 / 90s timeout, 0 calls to 0x232618). Confirms the wall.

[TOOL BUILT] Added `EeMemory::setReadOverride(vaddr,val)` + eerun `--ready-at
addr=val` so a specific RAM address can be forced to read a chosen value —
the legitimate way to short-circuit the interrupt-driven sync wait (we control
timing; this is NOT full-system emulation, same class as no-op'ing the DMA
kick). Also added MMI ops (PSUBB/PCPYLD/PAND/PXOR/PNOR, exact fn+sa encoding,
fail-fast otherwise) that 0x233928 hits; all unit-tested (suite 17/17).

[OPEN — next Phase 2 task] The mechanism is not yet AIMED: the concrete address
of the polled field *(s1+0x1690+0xc) needs s1's origin, which was not traced.
Loop waits for that field == 0. NEXT: trace where s1 is loaded before
0x271980 (read live regs / disasm the prologue), compute the address, run
`eerun ... 233928 --ready-at <addr>=0 --trace`, and REPORT honestly whether
0x233928 then drives 0x232618 per-quad (and the real vdiff --subset count) or
hits the next wall. Do this under the Phase 2 plan, not more ad-hoc spikes.

## Phase 2 spike 2b — driver + stub target fully mapped (2026-07-06, static disasm of RAM image)

[DUMP-MEASURED] Decoded 0x233928's prologue from re/ram/clock/eeMemory.bin:
- 0x233978: `addiu s0, s6, 0x5250` with s6=lui 0x37 -> **s0 = 0x00375250 = THE ROD
  ARRAY** (the tesselated quads). 0x233994: s1 = 0x00375230 (just below it).
- Calls in order: 0x2335e8, 0x232470, 0x230518, 0x22f720, 0x232da0, 0x235350,
  0x230fe8, 0x230518... It iterates/renders over the rod array. STRONG confirmation
  0x233928 IS the per-frame render driver we want to drive ("become the driver").
  (It does NOT call 0x232618 in its own body — 0x232618 is reached deeper, via one
  of these helpers, e.g. 0x232da0/0x235350.)

[DUMP-MEASURED] The Deci2 hang is a sub-call (function containing 0x271980).
Decoded its loop: s0 = s1 + 0x1690; `0x2719dc: lw v0, 12(s0)` = *(s1+0x1690+0xc);
`0x2719e0: bne v0,zero,0x2719d0` -> **`while (*(s1+0x1690+0xc) != 0) { a0 =
*(s1+0x1690); call 0x26f478(a0); }`**. STUB TARGET CONFIRMED: force
*(s1+0x1690+0xc) to read 0 (via --ready-at) to exit the loop.

[OPEN — next Phase 2 task, well-scoped] s1's concrete value in fn 0x271980 is
DYNAMIC (set/passed via the call chain, not a static global) -> needs a runtime
trace. NEXT: add "dump GPRs when PC==<addr>" to eerun (a read/exec watch), run
0x233928 until PC==0x2719dc, read s1, compute poll addr = s1+0x169c, then
`eerun ... 233928 --ready-at <addr>=0 --trace` and REPORT honestly whether the
driver then iterates the render helpers over the rod array (real vdiff --subset)
or hits the next wall. This is the Phase 2 plan's first task.

## Phase 2 task 1 — stub aimed, new (bigger) wall found: shared busy/done flag [DUMP-MEASURED]

[TOOL BUILT] `tools/eerun/main.cpp`: `--regs-at <hexpc>` exec-watch (dumps all
32 GPRs by name, capped at 5 hits, explicit `fflush` after each — needed,
stdout is fully buffered under redirection so a killed/long-running process
otherwise loses all interim output). Also `--max-instr N` (CLI override of the
200M budget, used for exploration) and the `EeError` path now prints a syscall
**count** (was one line per syscall — 18,897 of them drowned the output) plus
the `--trace` call-graph tally (previously success-path only, so a budget/wall
failure gave zero visibility into what was reached). No `EeInterpreter` core
change. Suite stays 17/17.

[DUMP-MEASURED] `--regs-at 2719dc` on `233928`: **s1 = 0x00410000**, confirmed
independently by disassembling `0x2718B8`'s own prologue (`lui v0,0x41` /
`move s1,v0` / `addiu v0,v0,0x1690`) — s1 is a fixed global, rebuilt fresh on
every call. `s0 = s1+0x1690 = 0x00411690` (matches). **pollAddr =
0x0041169C.**

[DUMP-MEASURED] `--ready-at 41169c=0 --trace --dump-stores ...` on `233928`:
escapes the targeted tight poll loop (0x2719D0-0x2719E0) as intended, but then
burns the entire 200M-instruction budget in a byte-scan/formatting loop at
`0x271948-0x27198C`, throwing `budget exceeded` at `pc=00271948`. The
`--trace` call tally shows **zero calls to any render helper**
(0x232da0/0x235350/0x230fe8/0x232618) — the run never gets there. Since the
run errors, the success-path store dump never fires: **no
`stores_233928.bin` was produced this session, no draws captured, no vdiff
run** (nothing to diff).

**Root cause, disassembly-confirmed**: `0x0041169C` is a **shared busy/done
flag**, read at two different sites inside the same function
(`0x002718B8`, called 18,897 times this run):
- `0x2718E4: lw v1,0xc(v0)` (v0=s0) — an **entry guard**: `bnez v1,epilogue`
  skips straight to return if a send is already in flight (real hardware's
  fast path, taken almost always).
- `0x271930: sw v0,0xc(a0)` (v0=1) — the function itself sets the flag when
  it starts a send.
- `0x2719DC: lw v0,0xc(s0)` — the **completion poll** the earlier session
  targeted, waiting for an interrupt handler to clear it back to 0.

Forcing every read of that address to 0 defeats the entry guard too, so
every one of the 18,897 calls takes the full send+`Deci2Call`+wait path
instead of the normal fast "busy, return" path — the entire budget goes into
an unbounded-looking debug-log flood (`0x26F4A0` x18,896, `0x268B00` x37,796,
inner formatter `0x26C61C`/`0x26C6FC` x302,341/x307,064), not into the
renderer. Re-ran identically to rule out a fluke: byte-for-byte same failure
signature both times — deterministic, not noise.

**Recommendation**: stub at the *read site* (PC-conditional), not the
*address* — only fake the completion-poll read at `0x2719DC`, leave the
entry-guard read at `0x2718E4` seeing the real (currently-0, i.e. "not busy")
value, so the fast path this function almost always wants is preserved.
Alternatively, RE whether this whole Deci2-print subsystem is
debug-station-only scaffolding (same open question already flagged for
`0x00233928` itself) — if so the entry guard's true idle value may not be 0
at all, meaning the stub value itself needs revisiting, not just its scope.
Full detail: `.superpowers/sdd/phase2-task1-report.md`.

## Phase 2 — per-rod render contract (2026-07-06) [DUMP-MEASURED]

Decision: stop fighting the Deci2 wall inside `0x00233928` — **become the
render driver**, calling its render helpers ourselves over the rod array.
This section maps the contract needed to do that. Full detail (tables,
struct dumps, field-by-field disasm) lives in
`.superpowers/sdd/phase2-render-contract.md`; summary here.

**The per-rod loop lives entirely inside `0x00233928`'s own body** (not in
some deeper callee) — disassembled `0x233928..0x233dd0` directly off
`re/ram/clock/eeMemory.bin`. Seven back-to-back loops, identical shape:

```
lw   v0, 0x150(s3)         ; s3 = current rod ptr, starts at rod array base
bnel v0, zero, skip          ; skip this rod if its +0x150 flag != 0
... set a0=rodPtr, a1=ctx or ctx+0xa0, f12/f13=ctx floats or 0.0 ...
jal  <render fn>
skip:
lw   a2, 4(s4)               ; a2 = rod COUNT, reloaded from ctx+4 (RUNTIME value)
slt  v0, s2, a2; bnez v0, loop_top
addiu s3, s3, 0x160           ; STRIDE = 0x160 (352 bytes) — NOT 0x50, confirmed x7
```

- **Stride = 0x160**, **bound = `*(ctx+4)`** (a runtime field, = 6 in the
  captured frame — not the "12 rods" guessed in the brief).
- **Per-rod skip flag** at `rodPtr+0x150`: nonzero = culled. Cross-checked
  live: rod0 (`0x375250`) flag=0 (visible), rod1 (`0x3753b0 = +0x160`)
  flag=1 (culled).
- Seven passes call, in order: `0x232da0` (matrix-apply prep, non-leaf, VU0-
  macro family, no GS write observed), `0x232e38` = **`draw_crystal_rod`,
  the real GS-packet emitter** (colored pass, then a zero-offset variant),
  `0x233328`/`0x2333b8` (per-rod aux, same VU0-matrix family), `0x232f80`
  (another draw variant), then `0x232da0` again. `0x232618` (previously
  known single-rect leaf) is called **once, directly, not in a loop** — it
  renders a fixed UI element, not a rod.

**`0x232e38` (`draw_crystal_rod`) is a LEAF — zero `jal` calls.** Reads
`*(0x00375230)` as the GS-packet write cursor, writes an 8-byte GIFtag-
shaped header (`0x54`/`0x100`), then loops 4x over `rodPtr + i*0x50`
(i=0..3 — matches the "4 vertex records of 0x50 bytes" already flagged in
`sp0-live-reads.md`, now pinned to its exact consumer): reads screen X/Y as
**already-computed floats** at `+0x10/+0x14` (the CPU-side transform already
ran before this captured frame — the projection matrices at
`0x29bd10/0x29bd50/0x29bd90` are NOT touched by this function and do not
need re-seeding for a harness that replays a captured frame), a per-vertex
scale at `+0x40`, and packed UV ints at `+0x30/0x34/0x38`. Also reads an
RGBA color from the caller's `a1` struct (`ctx+0xa0`, live value `08 08 08
80`) and a jitter offset from `f12/f13` (`ctx+0xb0/0xb4`, live value `0.01`).

**State to seed, all [DUMP-MEASURED] this session**:
- `ctx = 0x00296AB0` — found by byte-searching the whole 32MB image for the
  `jal 0x00233928` instruction word (`0x0C08CE4A`, 2 hits). Site 1
  (`0x0022CA50`) passes `a0 = s7+0x6ab0` with `s7=lui 0x29` disassembled at
  `0x0022C95C` → fixed global `0x00296AB0`. Cross-checked self-consistent:
  `ctx[0x6c]=1.0` (render-gate open), `ctx[4]=6` (sane rod count),
  `ctx[0xa0..ac]=08 08 08 80` (plausible RGBA), `ctx[0xb0/0xb4]=0.01`
  (plausible jitter). Site 2 (`0x0022CB18`) passes `a0=s2`, untraced —
  [OPEN], not needed since site 1 is confirmed sane.
- Packet-context struct `0x00375230` (separate from `ctx`!) — live fields
  `+0x00=0x70000060` (write cursor), `+0x04=0x70000000` (SPR bank base).
  Points into the EE Scratchpad RAM window `EeMemory` already emulates
  (Task 7). `0x22f720(ctxAddr)` initializes it (reads a bank-selector global,
  ORs with `0x70000000`) — call it once before the rod loop, or fall back to
  directly poking the two fields if that global proves troublesome.
- Rod array `0x00375250` / stride `0x160` / skip flag `+0x150` — needs no
  seeding, already correct in the captured frame.

**Validating result — the wall is unreachable from the render path.**
Grepped every `jal` target inside `0x232da0`, `0x232e38`, `0x233328`,
`0x2333b8`, `0x232f80`, `0x230518`, `0x235350`, `0x230fe8`, `0x22f720`, and
`0x2335e8` (the per-call setup fn `0x233928` invokes once at its top): **zero**
calls into the Deci2/debug family (`0x2718B8`/`0x271980`/`0x258D10`/
`0x26F4xx`/`0x26C6xx`). Calling these helpers directly, bypassing
`0x00233928` and `0x2335e8` entirely, sidesteps Phase 2 task 1's blocker —
**no sync-stub needed** for this path.

**Recommended harness** (new `eerun`-family driver, reusing
`EeInterpreter`/`EeMemory` as-is): load `eeMemory.bin`; `call(0x22f720,
a0=0x00375230)` once; loop `i=0..*(0x296AB0+4)-1`, skip if
`*(rodPtr+0x150)!=0`, else `call(0x232da0, rodPtr, 0x296AB0, 0)` then
`call(0x232e38, rodPtr, 0x296B50, f12=ctx[0xb0], f13=ctx[0xb4])` — start
with just this pair before adding the other five passes; `--dump-stores`,
`--decode` the SPR range `[0x70000000..finalCursor]` (already SPR-aware
since Task 7), `vdiff --subset` against `re/oracle/clock_sw_prims.json`,
report the real match count honestly and iterate pass-by-pass.

Full field tables, the second (untraced) `0x00233928` call site, and open
items: `.superpowers/sdd/phase2-render-contract.md`.

## Phase 2 task 2 — direct rod driver: real data emitted, no hardware-packet match yet [DUMP-MEASURED]

Built `eerun --drive-rods` (`tools/eerun/main.cpp`): `call(0x22f720,
a0=0x00375230)` once (returned a sane in-SPR cursor `0x70000010`, no fallback
poke needed), then for each non-culled rod (`*(rodPtr+0x150)==0`, bound
`*(ctx+4)`, stride `0x160`, `ctx=0x00296AB0`) sets `cpu.fpr[12]/fpr[13]` to
the live jitter floats (`ctx+0xb0/0xb4`, both `0.01`) and calls
`0x00232e38(a0=rodPtr, a1=ctx+0xa0)`. Zero new opcodes or `EeInterpreter`
changes needed — Task 6/7's SPR emulation plus the `0x00233928` spike's
FPU/COP1 ops already cover this call path. Suite stays 17/17.

**Result**: `rods processed=3 culled=3` (of `rodCount=6`), **336 real bytes**
written to SPR, no crash. `eerun --drive-rods --json cand_rods.json` then
`vdiff --subset clock_sw_prims.json cand_rods.json` → **`0/0 candidate draws
matched`** — `GsDumpParser::decodeGifData` reports 0 draws/0 kicks from the
336 bytes.

**Honest read, not a decoder bug**: spot-checking the raw bytes
(`EERUN_DUMP_SPR=1` env var added for this) shows `draw_crystal_rod` writes
**real, correct data** — a per-vertex word decodes as float `≈0.0201`
(matches the contract's independently-measured scale `~0.0199`), immediately
followed by 4 bytes `08 08 08 80` = **RGBA(8,8,8,128)**, exactly the contract's
static-disasm-measured color. This exact pattern repeats identically at all
3 rods' offsets. But the buffer is **not a hardware GIFtag stream**: the
contract's disasm shows only an 8-byte header (`sw a2,(v0)`/`sw a3,4(v0)`,
cursor advanced by 8) before per-vertex data, whereas a real GIFtag is 16
bytes; reading these bytes as a GIFtag decodes `nloop=0x54=84, nreg=16`
(implying 21,504 bytes of payload should follow — only 320 real bytes exist).
**Conclusion**: `draw_crystal_rod` is a real, correctly-executing emitter
producing a **pre-GIFtag internal staging record**, not the final
DMA-ready wire packet — translation to a real hardware GIFtag+PACKED stream
happens elsewhere (plausibly one of `0x230518`/`0x235350`/`0x230fe8`, called
between per-rod passes in `0x00233928`'s body, mirroring `0x232618`'s own
`F720→FB28→F7F8` begin/append/submit shape). Getting an actual vertex-level
oracle match needs either a dedicated decoder for this custom per-rod record
layout, or driving those translation helpers too. Full detail:
`.superpowers/sdd/phase2-task2-report.md`.

## Phase 2 task 1 — staging vs wire: the packetize route, decision [DUMP-MEASURED]

Goal: decide whether `draw_crystal_rod`'s staging record is directly decodable
or needs a real packetize call, and if the latter, name the exact fn+args+
destination. Method: Capstone MIPS64LE disasm straight off
`re/ram/clock/eeMemory.bin` (phys = vaddr & 0x1FFFFFFF, same convention as
every prior session), of the four between-pass helpers the render contract
names: `0x00230518`, `0x00235350`, `0x00230fe8`, `0x002324e8`.

**Verdict: not directly decodable — a real packetize call exists, and it is
`0x0022F7F8`, reached via `0x00235350` as a thin `a0=0x00375230` wrapper.**
`0x00230fe8` and `0x002324e8` are unrelated (dispatch/state-machine code and
sibling UI-rect emitters touching a *different* fixed buffer at
`0x00296df0`/`0x00296df0+0x71c0..0x71e0` — confirmed by disasm, neither reads
or writes `0x00375230`). `0x00230518` also targets the `0x00296df0` buffer
(builds register-only A+D packets — matches Task 6's decoded `TEST_1`/
`ALPHA_1` packet exactly: the switch/jump-table at `0x2c51b0` selects among
GS register addresses `0x48/0x44/0x42/0x68`) — a real packetizer, but for a
*different* packet family (blend/test-state setup), not the rod staging.

**`0x00235350`, disassembled in full**:
```
00235350: lui  a0, 0x37
00235354: j    0x22f7f8
00235358: addiu a0, a0, 0x5230   ; delay slot -> a0 = 0x00375230, unconditionally
```
It is a **pure tail-jump stub, zero other args** — calling it is exactly
`0x0022F7F8(a0=0x00375230)`.

**`0x0022F7F8`, disassembled in full — this is the packetize/finalize fn.**
Confirmed behavior, instruction-by-instruction:
- `t0 = *(structPtr+0x14)` — a **batch-start pointer**, a field NOT previously
  documented in the render contract (contract doc only had `+0x00`=cursor,
  `+0x04`=SPR base, `+0x08..+0x1c` noted "zeroed/unused" in the captured
  frame — that "unused" reading is now understood: it's unused **only until
  something snapshots it**, see below).
- `v0 = *(structPtr+0x00)` (current cursor), `a2 = *(t0)` (the **existing
  word already sitting at the batch-start address** — i.e. the very `0x54`
  placeholder header `draw_crystal_rod` wrote, per Task 2's finding).
- `v1 = (v0 - t0) >> 3, -2` — a byte-length-since-batch-start divided into
  8-byte units, i.e. **exactly the NLOOP/qword-count math a GIFtag finalize
  step needs**.
- Composes a new value from `a2`'s masked mode bits + the computed count,
  then **writes it back to the SAME address `t0`** (`sd a2,(t0)` /
  `sw v1,(t0)` region) — `0x0022F7F8` **patches the placeholder header
  IN PLACE at the batch-start address**, rather than copying/relocating
  the data to a separate wire buffer.
- Continues: zero-fills a trailing region (padding to a fixed slot size),
  then `jal 0x272fa0` / tail `j 0x272c10` — the same DMA-kick-family calls
  seen from `0x00232618`'s own path (Task 4: real `D2_MADR`/`D2_QWC`/
  `D2_CHCR` writes), confirming this finalize step also performs the actual
  GIF-DMA submit, not just header math.

**The wall for Task 2, precisely characterized**: `+0x14` (the batch-start
snapshot `0x0022F7F8` depends on) is **not** set by `0x0022F720` (confirmed —
disassembled `0x0022F720` in full this session: it writes `+0x00`, `+0x04`,
`+0x08`, `+0x0c`, never `+0x14`). It is set by the **emitter itself**,
immediately after calling `0x0022F720` — confirmed concretely by
disassembling the sibling per-rod draw variant `0x00232F80` (render
contract's pass #6), whose own body is:
```
00232fa4: jal 0x22f720            ; a0 = 0x00375230 (re-init cursor)
...
00232fb0: lw  v1, 0x5230($s1)     ; v1 = the FRESH cursor 0x22f720 just wrote
00232fcc: sw  v1, 0x14($s0)       ; *** snapshot: structPtr+0x14 = v1 ***
...                                 ; then writes its own header + vertex data
00233050: jal 0x22f7f8            ; a0 = 0x00375230 -> finalize/kick
```
**`draw_crystal_rod` (`0x00232E38`) never performs this snapshot.** The
current `eerun --drive-rods` harness calls `0x22f720` once up front and never
writes `+0x14`, so it stays at its captured-frame value of `0` (confirmed:
`re/ram/clock/eeMemory.bin`, phys `0x00375244`, reads `0x00000000`). Calling
`0x00235350`/`0x0022F7F8` on top of the current harness as-is would
dereference address `0` as "the existing header word" — wrong, not a valid
finalize.

**DECISION for Task 2** (fn + args + destination, per the brief's required
form):
1. After `call(0x0022F720, a0=0x00375230)`, **snapshot**
   `poke32(0x00375230+0x14, peek32(0x00375230+0x00))` — mirrors what
   `0x00232F80` does inline; this is the missing piece, not a new unknown.
2. Run the existing rod loop (`draw_crystal_rod` × N non-culled rods,
   unchanged from Task 2's harness).
3. `call(0x00235350)` (no args — it hardcodes `a0=0x00375230`), equivalently
   `call(0x0022F7F8, a0=0x00375230)` directly.
4. **Wire-packet destination: in place**, at the snapshotted batch-start
   address inside the emulated SPR window (same buffer `draw_crystal_rod`
   already writes into — no separate/relocated wire buffer was found in this
   call chain). `0x0022F7F8` patches that address's header word using the
   real cursor-delta byte count, then proceeds into the same DMA-kick call
   family (`0x272fa0`/`0x272c10`) `0x00232618`'s path uses.

**[OPEN] for Task 2 to close empirically, not asserted here**: the *exact*
resulting byte layout after the patch (whether it becomes a standards-shape
16-byte GIFtag `decodeGifData` can parse as-is, or a layout still needing a
small adapter) is not fully pinned by static disasm alone — `0x0022F7F8`'s
zero-fill/tag-word logic past the point read this session plausibly writes
more structure than was traced. Task 2 should implement steps 1-3 above,
`--dump-spr`/`--decode` the result, and report the real `vdiff --subset`
count honestly, same as every prior session here — this is a confirmed,
disasm-backed ROUTE, not a confirmed final byte match.

Full raw disassembly transcripts (all four helpers, `0x0022F720`,
`0x0022F7F8`, `0x00232F80`): `.superpowers/sdd/task-1-report.md`.

## Phase 2 task 2 — finalize implemented, but it reaches the SAME Deci2 wall [DUMP-MEASURED]

Implemented Task 1's decision in `tools/eerun/main.cpp`: snapshot
`*(0x00375230+0x14) = *(0x00375230+0x00)` right after the `0x22f720` init (mirrors
`0x00232F80`'s inline snapshot), run the unchanged 3-rod loop, then
`call(0x00235350)` (the `a0=0x00375230` tail-jump stub to `0x0022F7F8`). Also added
an `EERUN_MAX_INSTR` env override (temp diagnostic knob, no CLI flag yet) to binary-
search where a budget-exceeded run's PC actually sits.

**Result: the finalize call does NOT complete even at a 100,000,000-instruction
budget.** It throws `budget exceeded` with the `--trace` call tally showing:

```
call 00258D10 x1150850   <- Deci2Call syscall trampoline (same as before)
call 0026F478 x1150849   <- Deci2Call wrapper (same address, same as the 233928 spike)
```

This is **the exact same Deci2 debug-print flood** documented in "Phase 2 caller
spike: driving 0x00233928" above (there: `0x26F4A0`/`0x268B00`/`0x26C61C` millions
of times; here: `0x258D10`/`0x26F478` over a million calls each and still climbing
— confirmed unbounded within this budget, not merely slow, by the identical growth
shape). Binary-searching the budget (1000 / 100k / 1M / 1.2M / 7.6M / 50M / 100M
instructions) shows the PC advancing steadily through a tight retry/counter loop
around `0x0026E7FC-0x0026F478` as the budget grows — i.e. this is real forward
progress into the Deci2 subsystem, not a frozen/buggy PC (verified: round-number
budgets that are multiples of the loop's iteration length coincidentally landed on
the same PC, which is why the first few probes looked "stuck"; non-round budgets
`1234567`/`7654321` showed the PC moving, resolving that false lead).

**This corrects the render-contract doc's claim** ("Validating result — the wall
is unreachable from the render path... zero calls into the Deci2/debug family")
[FALSIFIED for the finalize call specifically]. That grep covered
`0x232da0/0x232e38/0x233328/0x2333b8/0x232f80/0x230518/0x235350/0x230fe8/0x22f720/
0x2335e8` but **not** `0x0022F7F8`'s own downstream tail-calls (`0x272fa0`/
`0x272c10`, the real GIF-DMA-kick family) — those, traced live this session, walk
into `0x0026E700` (the DMA-channel-busy poll, previously proven safe/bounded under
`0x232618`) and from there into a **different** call path than `0x232618` ever took,
which eventually reaches `0x0026F478`. `0x232618`'s own DMA kick (Task 4, this doc)
never hit this — the difference is state this driver hasn't seeded (most likely:
whatever field the `0x2718B8`-family busy/done flag reads, unseeded/zero here where
`0x232618`'s real caller had it in a different state).

**Not yet done this session** (honest stop, not a forced pass): finding the exact
read site of the busy/done flag inside *this* call chain and stubbing only that
read (mirroring Phase 2 task 1's "stub at the read site, not the address" fix,
which is documented but was never applied because that spike moved to the
direct-driver strategy instead). That is the next concrete step — same class of
fix already proven to work once, just needs re-aiming at this call chain's flag
address.

Files: `tools/eerun/main.cpp` (`+0x14` snapshot, `kFinalizeFn` call, temp
`EERUN_MAX_INSTR` env override — not yet a real CLI flag).

## Phase 2 task 2 — cfc2 fix, finalize completes, byte layout still undecodable [DUMP-MEASURED]

The section above's "SAME Deci2 wall" diagnosis was **[FALSIFIED]**, same session,
by finishing the discovery loop instead of stopping at the budget-exceeded
symptom. Root cause: a genuine missing opcode, not a hardware-wait wall.

**The bug.** `cfc2 $a2, $29` — word `0x4846E800` at `pc=0x0026E7E4`, inside the
finalize call's downstream DMA-kick chain (`0x0022F7F8` → `0x272fa0`/`0x272c10`
→ `0x0026E700`, a generic "wait for DMA channels + VU0/VU1 idle" helper) — reads
VU0 control register 29 (VPU-STAT) into `$a2`, then a poll loop masks it against
`0x100` (VBS1, the VU1-busy bit) to decide whether to keep spinning. The
existing `EeInterpreter.cpp` COP2 handler (case `0x12`) had no discrimination
between this scalar-transfer form and the VU0 macro-arithmetic form — both use
the exact same rs/rt/rd/sa bit positions in this game's real instruction
stream, with rs always in 0-15 for BOTH forms here (no reserved high bit
separates them, contrary to the standard MIPS COP0/COP1 CO-bit convention). The
word was silently misrouted into the macro path (treated as a `VADDx.z` with
`vfs=vf29, vfd=vf0`), which never writes `$a2` — the register kept whatever
leftover value it held, and the VU1-busy poll spun on garbage instead of seeing
0 (not busy, correctly, since this codebase never runs VU1 microcode).

This exact word had already been seen in an EARLIER session (see "Ops added
this session" item 2 above, `pc=0026E7E4 word=4846E800`) and was *also*
misclassified there as a macro op — the bug predates this task.

**The fix** (`src/ee/EeInterpreter.cpp`, case `0x12`): special-case
`sa==0 && fn==0 && rs==0x02` as `cfc2 rt, id` (`id` in `rd`), returning 0 for
`gpr[rt]` — matching real hardware, since no VU1 microcode ever runs in this
codebase and VU0 macro arithmetic executes synchronously inline (no async
"busy" state to model). The discriminator is deliberately narrow: a genuine
macro op targeting `vfd=0` (`sa==0`) would write to VF0, which is
hardwired-read-only `(0,0,0,1)` on real hardware, so compiled code never emits
that combination meaningfully — `sa==0 && fn==0` is safe to treat as dead-macro
territory and reroute to the transfer form. Only `cfc2` is handled; other
transfer forms (`mfc2`/`mtc2`/`ctc2`/`bc2`) are not yet hit and remain
unimplemented (would throw via the existing macro-path fallthrough if their
`rs` value collided with a real macro word — none have, so far).

New test 21 in `tests/EeInterpreterTest.cpp`: hand-assembled `0x4846E800`,
seeds `$a2` with a nonzero sentinel, asserts it becomes exactly 0 after
`call()` — a no-op mistake (the pre-fix behavior) fails this. Suite 17/17.

**Result after the fix:** `eerun --drive-rods` now runs to completion with the
default instruction budget (no override needed) — no budget-exceeded, no
Deci2 anything:

```
drive-rods: rods processed=3 culled=3, SPR cursor 70000010 -> 70000160 (336 bytes emitted)
drive-rods: after finalize(0x235350): SPR cursor -> 70000170 (352 bytes total)
drive-rods: decoded 0 draws, 0 kicks, 1 giftags
drive-rods: wrote 0 prims -> re/oracle/cand_rods.json
```

**Byte-level diagnosis of the still-honest 0-draws result.** Diffing the raw
SPR bytes immediately before vs. after the finalize call (`EERUN_DUMP_SPR_PRE`/
`EERUN_DUMP_SPR` env vars, both added to `tools/eerun/main.cpp`) shows finalize
changes exactly ONE byte across the whole 336-byte rod region: byte 0 goes
`0x54` → `0x56` (a +2 delta) — matching the disasm's "new header word = old +
computed count" patch at the batch-start address. It also appends one new
16-byte quadword at the very end (offset 336-351): `00 00 00 70 00 00 00 00
00 00 00 00 00 00 00 00` (low 32 bits = `0x70000000` = `EeMemory::kSprBase`
itself — looks like an address/pointer constant, not GS register data).

Feeding the whole 352-byte blob to `GsDumpParser::decodeGifData` (which
assumes a standard hardware GIFtag: low64 = NLOOP/EOP/PRE/PRIM/FLG/NREG,
high64 = REGS) reads the very first bytes as the tag: `NLOOP=0x56=86`,
`NREG=0→16` (PACKED, since the NREG nibble is literally 0 and 0 means 16 per
spec), `FLG=0` (PACKED). That implies `86*16=1376` quadwords (22016 bytes) of
payload — vastly more than the 336 bytes actually present. `decodeGifData`'s
inner PACKED loop has no bounds check against `size` (only the OUTER
`while (p+16<=size)` loop does), so it walks straight past the buffer,
consuming the "tag" in one shot and never reaching the trailing quadword as a
tag of its own; a handful of the 16-"register" slots per loop happen to land
on `desc=1` (RGBAQ) by coincidence (matching the real `08 08 08 80` color
bytes), most land on `desc=0` (spurious PRIM resets) — no `desc∈{4,5}` (XYZ2
vertex kick) is ever hit, so `out.prims` stays empty: 0 draws, 0 kicks, exactly
matching the observed counts.

**[OPEN], stated honestly:** NLOOP=86 (or 84 pre-patch) does not correspond to
any real per-rod chunk size (112 bytes = 7 quadwords per rod × 3 rods = 336
bytes total). Either (a) this internal staging buffer's header fields are NOT
at the standard NLOOP/PRE/PRIM/FLG/NREG bit positions `decodeGifData` assumes
— i.e. this is not literally a hardware-shaped GIFtag even after `finalize`,
or (b) a further real-hardware-side step (past what this interpreter's
`0x272fa0`/`0x272c10` tail-calls actually execute, since DMA hardware itself
isn't modeled) is needed before the bytes are genuinely GIFtag-shaped. This
was NOT resolved this session — reported honestly rather than fudged.
`vdiff --subset re/oracle/clock_sw_prims.json re/oracle/cand_rods.json` =
**0/0 candidate draws matched** (0 candidates decoded).

Files: `src/ee/EeInterpreter.cpp` (cfc2 special case), `tests/EeInterpreterTest.cpp`
(test 21), `tools/eerun/main.cpp` (`EERUN_DUMP_SPR_PRE` env var added for the
pre/post-finalize byte diff).

## Phase 2 — staging format DECODED (2026-07-06) [DUMP-MEASURED]

Pivot (user-approved): decode the measured staging format directly (reading a
measured data format, not re-deriving logic). Dumped SPR bytes via
EERUN_DUMP_SPR=1 (saved .superpowers/sdd/phase2-staging-bytes.txt). Layout:

Per rod = **112 bytes = 16-byte header + 4 vertices × 24 bytes**. 3 rods
processed (3 culled) this frame. Header line: `56/54 00 00 00 00 00 00 00 |
00 00 00 01 00 00 00 00` (NOT a GIFtag — decoder misreads byte0 as NLOOP=0x56;
finalize patches byte0 0x54->0x56). Per-vertex 24-byte layout [DUMP-MEASURED,
cross-checked vs sp0-live-reads screen(1915,2118) + contract §2]:
- +0x00, +0x04: floats (intermediate/world, e.g. 0x3CA50356 ≈ 0.0201)
- +0x08: **RGBA** (`08 08 08 80` = 8,8,8,128) [confirmed]
- +0x0c: float
- +0x10: **screen X|Y in 12.4** — low16 = X (0x772B/16 = 1906.7),
  high16 = Y (0x8441/16 = 2116.1) [confirmed vs sp0 screen coords]
- +0x14: 0xFFFFF010 marker (Z/flag)
Trailer (finalize): one quadword `00 00 00 70 ...` = 0x70000000 pointer.

COMPARISON CAVEAT [HYPOTHESIS]: the oracle's rod draws are 60-vert TRI-STRIPs
(type 4, TME=1) colored ~(210,230,230,64); our staging rods are 4-vert chunks
colored (8,8,8,128). Granularity + color DIFFER — draw-level --subset likely
won't match even with correct geometry. The honest validation is a
COORDINATE-CLOUD check (do our decoded rod X/Y, after GS OFX/OFY offset, land
on oracle vertices?) plus rendering the rods for the visual (Milestone A).

## Phase 2 — staging format DIRECTLY DECODED, geometry VALIDATED [DUMP-MEASURED]

Implemented `decodeStagingDirect()` in `tools/eerun/main.cpp` (new
`--staging-json <path>` flag on `--drive-rods`): parses the 112-byte-per-rod
staging record directly per the layout mapped above (NOT `decodeGifData`,
which misreads it as a hardware GIFtag), emitting one `GsPrimitive`
(`PRIM.type=4`, 4 verts, RGBA from `+0x08`) per rod, written via the shared
`GsDumpParser::writeJson` (same schema as the oracle/`gsdump --json`).

**OFX/OFY found empirically**: `OFX=25788 raw (1611.75px)`,
`OFY=31656 raw (1978.5px)`. Brute-force search over every
`(decoded_px − oracle_vertex_px)` pair across the 7 distinct decoded
rod-vertex positions (3 rods share a hub vertex) x ~20.5k oracle vertices
(`re/oracle/clock_sw_prims.json`), scoring each candidate offset by how many
of the 7 points land within 2px of *any* oracle vertex. Winner scores 7/7,
with one vertex landing **0.003px** from an exact oracle vertex — far too
precise to be coincidental. Not read from a live GS register capture of this
exact frame (none exists in `re/` this session); the empirical fit's internal
consistency is the evidence, reported honestly as that class of confidence.

**Coordinate-cloud validation** (new `vdiff --cloud` mode,
`tools/vdiff/vdiff.mjs`): checks each candidate vertex individually against
the flattened oracle vertex cloud (right check when candidate/oracle
granularity+color differ, unlike `--subset`).

```
node tools/vdiff/vdiff.mjs --cloud re/oracle/clock_sw_prims.json re/oracle/cand_rods_staging.json 2
vdiff --cloud: 12/12 candidate vertices within 2px of an oracle vertex
```

**12/12 decoded rod vertices (3 rods x 4) land within 2px of an oracle
vertex** — 4 of the 12 within 0.1px, the rest 1.2-2.0px. **Verdict: the rod
geometry IS correct.** This validates `draw_crystal_rod`'s screen-space
output against the oracle independent of the still-open "how the staging
record becomes a real wire GIFtag" question. Full detail, including the
per-vertex distances and caveats on residual 1-2px error:
`.superpowers/sdd/phase2-staging-decode-report.md`.

## Phase 2 TIGHTEN — real XYOFFSET substituted, 12/12 FALSIFIED [DUMP-MEASURED]

The OFX/OFY above were a FIT (empirical search), not a register read — flagged
at the time as needing independent confirmation. This pass replaces the fit
with the real GS XYOFFSET the rod draws actually use.

`GsDumpParser::writeJson` (`tools/gsdump/GsDumpParser.cpp`) now emits
`"xyoffset":{"ofx":N,"ofy":N}` per draw (the value was already decoded into
`GsPrimitive::xyoffset`, just not serialized). Regenerated
`re/oracle/clock_sw_prims.json` and located the actual rod draws: `PRIM.type=4`
(TRI_STRIP), 60 verts, `TEX0.TBP0=11264/TBW=2/PSMCT32/128x128`, vertex color
~(210,230,230,64), `FRAME.FBP=0` — 40 such draws in 4 clusters of 10 (dump
indices {1-16},{949-964},{1900-1915},{2851-2866}).

**Real value**: `OFX=27648 raw (1728.0px)` (all 40 rod draws agree exactly).
`OFY` alternates between `30976` and `30984` raw (1936.0/1936.5px, a 0.5px
shift) across the four clusters — consistent with PS2 interlaced-field
vertical jitter, not noise; used `OFY=30984` (first cluster).

**Real vs fitted — NOT close**: ΔOFX ≈ 1860 raw = **116.25px**, ΔOFY ≈
672-680 raw = **42.0-42.5px**. Far outside coincidence/rounding range.

Substituted the real value into `decodeStagingDirect()`'s `kOfxRaw`/`kOfyRaw`
(`tools/eerun/main.cpp`) and reran:

```
bin\eerun.exe re\ram\clock\eeMemory.bin --drive-rods --json re\oracle\cand_rods.json --staging-json re\oracle\cand_rods_staging.json
node tools\vdiff\vdiff.mjs --cloud re\oracle\clock_sw_prims.json re\oracle\cand_rods_staging.json 2
vdiff --cloud: 1/12 candidate vertices within 2px of an oracle vertex
```

**1/12, not 12/12.** Distances range 1.4-16.9px (down from the fit's 0.003-2px
band). **Honest verdict: the earlier 12/12 was substantially a fit artifact,
not an independent proof.** With ~20k oracle vertices and 2 free translation
parameters, landing 12 points within 2px by exhaustive search is weak
evidence of correct absolute placement.

What survives: `draw_crystal_rod`'s per-vertex 24-byte layout and RGBA
extraction (from disassembly, not fit) are unchanged. What's falsified:
absolute screen-space placement using the previously-fitted offset was never
actually validated. The 1-12/12 gap with correctly-ordered-magnitude
neighborhoods (decoded points land in the clock's general on-screen region,
not off by hundreds of px) suggests either a missing linear transform between
`draw_crystal_rod`'s raw output and the final GIFtag, or a time/jitter
mismatch between the `eeMemory.bin` capture and the `.gs` dump capture (not
guaranteed to be the same simulated frame). Full writeup, per-vertex distance
table, and open questions: `.superpowers/sdd/phase2-tighten-report.md`.

**Status: rod screen-space placement is OPEN/CONCERNS, not validated.**
Geometry-decode mechanics (byte layout, RGBA) still stand; absolute placement
does not.

## Phase 2 — validated vs the SAME-FRAME screenshot (2026-07-06) [DUMP-MEASURED]

Key correction: `re/ram/clock/eeMemory.bin` is NOT the pure clock visor — its own
`Screenshot.png` shows the **System Configuration MENU** ("Configuração do Sistema /
Ajuste do Relógio", timestamp 2026/07/05 20:11:09) with 4 floating crystal CUBES +
background radial rods. So the correct same-frame reference is that screenshot, NOT
clock_sw.gs (a different screen captured 3 days earlier). This also explains the 1/12
vs clock_sw: wrong screen AND wrong frame.

Overlaid the 3 decoded objects' vertices (real GS offset 27648/30976, Y×480/224) onto
Screenshot.png via tools/overlay_check.mjs → re/oracle/rods_on_screenshot.png. RESULT:
the markers land on and TRACE the bottom-center crystal cube's outline — a genuine
geometric correspondence against the correct same-frame reference (no fit, same frame).
draw_crystal_rod renders the crystal-prism objects (cubes/rods share the draw; recall
0x375250 = 12 dial rods + 4 menu cubes). HONEST SCOPE: partial — 3 of 6 objects, one
cube region, visual (not pixel-rigorous) match. Validates the staging DECODE produces
correct geometry for this frame. To see the full crystal-clock DIAL, a RAM capture of
the actual VISOR mode (not the menu) is needed.
