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
