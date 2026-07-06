# Crystal Clock — Port Function Map (evidence-based)

> Audit 2026-07-05: claims status-tagged per master-strategy spec §6.

> Goal: generate the clock's GS command stream procedurally (no dump), feeding the
> SAME faithful GsRenderer that already replays dumps at 5–10% pixel-diff. This map
> is the complete inventory of OSDSYS functions to port, each VERIFIED in Ghidra
> (program=OSDSYS.elf, base 0x001f0000). Status: RESOLVED / PARTIAL / UNVERIFIED.
>
> **This replaces the invented procedural look (prism/tunnel/spots shaders were
> guesses, not read from the GS — deleted approach). Nothing here is assumed;
> every entry is decompiled.**

## Per-frame render spine (VERIFIED 2026-07-03, direct decompile)

`ui_render_3d_objects @ 0x00223f78(float param_1 transitionT, param_2, int* clockState)`
is the whole per-frame draw. `clockState[0]` = rotation phase; `[1]` = rod count;
`[0x1b]` = transition trigger; `[0x28..]` = pass-2 matrix ctx; `[0x2c]/[0x2d]` =
pass-3 per-group angle offsets. Rod arrays: **0x375250 (group A) + 0x377e50 (group
B)**, stride **0x160**, field **+0x150** = front/back-face pass selector. [FALSIFIED, layout richer → live: groups of 4×0x50-byte vertex records (world xyz + uv + screen XY float AND 12.4 int + pass flag) + normal record — the tessellated quads themselves; see sp0-live-reads.md] [DECOMP-SOURCED]/[HYPOTHESIS] (direct Ghidra decompile per this section's header; not independently cross-checked in this file against a live trace)

Base rod angle: `fVar18 = clockState[0] * in_f1`. Steady-state (else branch) draws
these passes IN ORDER. **⚠️ The "GS-state call" labels below were WRONG in my first
pass — the TODO decompile session (below) falsified them: `FUN_002324e8/00232538`
are a WaitSema DMA-sync, `FUN_00230fe8` is the icon-browser state machine (not
even in the render path per its callers), `FUN_00230518` is a generic DMA/queue
trampoline whose payload won't decompile. So the between-pass calls are
buffer/DMA sync + submit, NOT blend-register writes.** [FALSIFIED → self-corrected in place; matches known-falsified items 2 (`FUN_002324e8`/`FUN_00232538`), 3 (`FUN_00230fe8`), 4 (`FUN_00230518`)] The per-pass ORDER and the
emit calls + angle steps ARE verified: [HYPOTHESIS] (decompile-verified per this section, not independently re-confirmed here)

| # | Pass | Emit call | Angle step |
|---|------|-----------|-----------|
| 1 | surface base | `FUN_00232da0` (rods +0x150==0) | — |
| 2 | additive | `FUN_00232e38` (draw_crystal_rod) | `fGpffff832c=0.20` |
| 3 | refraction | `FUN_00232e38` | `fGpffff8330=0.40` + `clockState[0x2c/0x2d]` |
| 4 | back surface | `FUN_00232da0` (rods +0x150!=0) | — |
| 5 | back highlight | `FUN_00232da0` (…, 0xff) | — |

[HYPOTHESIS] (this 5-pass enumeration is decompile-sourced here; note CLOCK-SYSTEM-MAP.md §7 cites a differently-worded "5 passes" from `rod-pipeline.md` — not confirmed to be the identical enumeration, flagged as ambiguous)

The `DAT_002973a0` / `DAT_002973c0` 16-byte blocks copied into the packet buffer
`0x375230` per pass ARE real GS register templates (their bytes match the dump's
per-pass ALPHA/TEX register values) [DUMP-MEASURED]. **`FUN_0022f720` is NOT their writer** (that
was falsified — it's browser icon-selection logic); the template copy is inline in
`ui_render_3d_objects` itself (the `*puRam00375230 = DAT_002973a0; …` stores). [FALSIFIED → self-corrected in place; matches known-falsified item 5]
`FUN_00235350()` kick: its tail shares the same orbit code as the light spots;
the actual GIF/VIF1 DMA submit was NOT located (see gaps). [FALSIFIED → self-corrected in place; matches known-falsified item 6]

## Rod draw — `draw_crystal_rod @ 0x00232e38` (VERIFIED)

Calls exactly three, then the built vertices go to the GS packet buffer: [DECOMP-SOURCED]/[HYPOTHESIS]
- **`FUN_002732d8(0x29bd10, 0x29bcf0)`** = rotation_build — 2× cross-product
  orthonormal basis. Ported: `ps2clock::BuildRotationMatrix` (Projection.cpp). PARTIAL
  (handedness flagged in RotationBasisTest). [HYPOTHESIS]
- **`FUN_002730a8(fov, 1.0, fovVariant, 2048.0, 2048.0, 1.0, …, 0x29bd50)`** =
  projection_build — GS-native perspective embedding the viewport. Constants confirmed
  live here: `0x45000000`=2048.0 (far/scale), `0x3f800000`=1.0 (aspect) [LIVE-VERIFIED]; FOV =
  `uGpffff8480` (4:3) / `uGpffff8484` (16:9), selected by `iGpffff8d18`. Ported:
  `ps2clock::BuildProjectionMatrix`. PARTIAL (FOV gp-global still needs a live value). [HYPOTHESIS] (this is the underlying static analysis for the doc's later "W1 projection" claim, which is PROVISIONAL per the known-falsified/contested list — see §"Port-by-contract step 1" below)
- **`FUN_002738a0(0x29bd90, 0x29bd50, 0x29bd10)`** = transform+emit — applies the
  matrices to the rod's model vertices and appends GS XYZ/UV/RGBA to the packet.
  UNVERIFIED internals (the actual vertex→GS-packet write). NEXT to decompile.

## TODO decompile session (2026-07-03, direct Ghidra decompile, program=OSDSYS.elf)

[DECOMP-SOURCED] This whole section is the primary evidence source for known-falsified items 2, 3, and 4 (it documents the falsification directly, in place — no further correction needed beyond tagging).

**Headline correction: the "GS state setter" hypothesis is falsified for 3 of 4
candidates, and the crux transform+emit function is sitting on a misclassified
data region, not code.** Details below, each with decompile evidence. Several
entries required a second pass because the first decompile output didn't match
the calling convention implied by the call site — this turned out to be a real,
reproducible phenomenon (see "Shared-tail / overlap anomaly" note at the end),
not tool misuse. `program="OSDSYS.elf"` was passed on every call; `switch_program`
+ re-decompile of one function (FUN_00230fe8) confirmed results are stable and
not an hddosd.elf mixup.

### GS state setters — hypothesis REJECTED for FUN_00230fe8; PARTIAL for the other three

[DECOMP-SOURCED] [FALSIFIED → matches known-falsified items 2 (`FUN_002324e8`/`FUN_00232538`), 3 (`FUN_00230fe8`), 4 (`FUN_00230518`); this section is the origin evidence for those corrections, already self-corrected in place]

- **`FUN_002324e8@0x002324e8`** (body 0x2324e8-0x232537, `void(void)`). Calls
  `FUN_00241cc0(6)`, then computes `iVar1=((s0 - *(s1+0xc54))/2)*0x10` and
  `iVar2=((s0 - *(s1+0xc50))/2)*0x10` (a center-offset in 12.4 fixed point —
  `*0x10` = `<<4`), then falls through (no `jr ra` — straight-line code) into
  the body of **FUN_00232538**. Confirmed by `disassemble_bytes` 0x2324e8-0x232560:
  the instruction stream is continuous across the two "function" boundaries.
  PARTIAL — real code, real 12.4 fixed-point math, but the call it makes at the
  end is `WaitSema` (see below), not a GS register write.
- **`FUN_00232538@0x00232538`** (body 0x232538-0x232597, decompiled signature
  `(undefined8,int,undefined8,undefined8,undefined8,undefined8)`). This is the
  **same code FUN_002324e8 falls into** — Ghidra has registered it as a second,
  independent function entry into what is really one shared tail. It builds
  `a0=s0+0x2350, a1=param_2+0x880, a2=s0+0x24d0, a3=param_2+0x958, t2=0x3e8(1000),
  t3=0x1a8(424)` and calls **`jal 0x00248920`**. Decompiling 0x00248920 directly
  gives `void WaitSema(void) { syscall(0x44); }` — a genuine, correctly-named
  PS2 EE kernel semaphore-wait syscall stub (1 implicit arg, sema id in a0). So
  the real behavior of FUN_002324e8/FUN_00232538 is: **compute a semaphore id
  from a viewport/rect field pair, then WaitSema(id)** — a DMA/packet-buffer
  sync wait (very plausibly waiting on the previous GIF/VIF1 packet's completion
  semaphore before the caller starts writing a new one), **not an ALPHA/TEST/AD
  register write**. PARTIAL — confirmed it's a sync primitive, not confirmed
  which semaphore or why the id is derived from those two clockState fields.
- **`FUN_00230518@0x00230518`** (body is literally 2 instructions: `j 0x002401c8`
  / delay-slot `addiu sp,sp,0x10` — a tail-call trampoline, confirmed by
  `disassemble_function`). Decompiling *through* the jump target (0x002401c8)
  shows: read a clockState-ish struct's dimensions (`*(s4+0x10)`, hi16/lo16 split
  → iVar1,iVar2), compute `iVar3=iVar1*iVar2*4` (a byte size = w*h*4, i.e. an
  RGBA framebuffer/texture byte count), zero 4 struct fields, write `0x1860200`
  into `*unaff_s3_lo`, then call **`FUN_0026753c(0x1860200, s4+0x14, iVar3)`**.
  `FUN_0026753c` itself ~~**fails to decompile** ("Control flow encountered
  unimplemented instructions" → `halt_unimplemented()`)~~, [FALSIFIED → live disasm: plain 128-bit memcpy(dst,src,n)] so its real behavior is
  UNVERIFIED — the call shape (fixed tag constant, src pointer, byte count)
  is consistent with a generic DMA-queue/GIF-tag submit helper, but that is a
  hypothesis, not a verified fact. `get_xrefs_to(0x00230518)` returns **26
  callers spanning the whole OS** (browser code, `j_InitTLB`, `FUN_00217028`,
  several unrelated menu functions, plus 6 sites in `ui_render_3d_objects`) —
  this is not a clock-specific or even GS-specific routine; it's a generic
  low-level utility called from everywhere. UNVERIFIED (decompiler can't reach
  the real payload; caller list rules out "GS blend setter" as its purpose).
- **`FUN_00230fe8@0x00230fe8`** (get_function_by_address body 0x230fe8-0x23106f,
  135B, but the decompiler's real CFG — cross-checked against raw
  `disassemble_bytes` at the same address, which matches line-for-line — extends
  well past that, to ~0x231bc8). Real decompile: reads `iGpffff8b54` (a focus/
  selection index), calls `FUN_0024b310/320/330/350` (disc/media-check family —
  names and call shape strongly resemble `sceCdRead`-adjacent status polling),
  walks a `0x6a0`-stride table at `gp0xffff8eb8[iGpffff8dd8]` and a `0x160`-stride
  table at `DAT_002ada20` (icon/game-list entries — the `0x160` stride is a
  coincidental match with the rod struct stride, **not the rod array**), calls
  `FUN_00201d90/FUN_00201e58` (pad-button-state checks, arg = button bitmask)
  repeatedly, and drives menu-highlight state (`iGpffff8c04/8c08/8c0c/8c10`,
  `FUN_002407e8` "set highlight"). **This is OSDSYS's icon-browser / controller-
  polling state machine. It does not touch any GS register, the 0x375230 packet
  buffer, or ALPHA/TEST/FRAME fields anywhere in its body.** REJECTED as a
  "GS state setter" — the render-spine's characterization of
  `FUN_00230fe8(2,1,2)` as a "GS-state call" needs revisiting; the call is real
  (confirmed present in `ui_render_3d_objects`'s decompile) but its callee does
  browser/input housekeeping that happens to run once per clock frame, not
  blend-state setup.

### Packet/DMA

[DECOMP-SOURCED] [FALSIFIED → matches known-falsified items 5 (`FUN_0022f720`) and 6 (`FUN_00235350`); this section is the origin evidence, already self-corrected in place]

- **`FUN_0022f720@0x0022f720`** (body 0x22f720-0x22f7f7, decompiled signature
  `(undefined8,undefined8,undefined4,int)`). Real decompile is **OSD-browser
  icon/selection logic** (`FUN_00205830` disc-type check, `FUN_00243c30`
  icon-data build, globals `uRam002c8a3c/40/8c/88`, `DAT_0029b478` an 8-entry
  table, font-color table writes `DAT_0029b4f0..508`) — none of it references
  0x375230 or any GS register. This contradicts the hypothesis that it "writes
  a GS register template into buffer 0x375230." The **actual template blit is
  inlined in the caller** (`ui_render_3d_objects`), immediately after each
  `FUN_0022f720(0x375230)` call: `puRam00375230[0..3] = {DAT_002973a0 (or c0),
  ...ac/c4/c8/cc}`, i.e. the caller copies the 16-byte register template itself;
  `FUN_0022f720` does not do it. `get_xrefs_to` confirms the same caller set as
  the render spine documented (`ui_render_3d_objects` ×6, `FUN_002246f0`, plus
  ~9 more browser-adjacent sites) — the call site is real, but what runs there
  is browser housekeeping, not a packet-buffer initializer. UNVERIFIED /
  contradicted; needs a live trace to settle (static decompile is unambiguous
  but doesn't match the assumed role).
- **`FUN_00235350@0x00235350`** (body 0x235350-0x23535f per the function table,
  but real CFG — confirmed via `disassemble_bytes`, first two `jal`s at
  0x235354/0x235360 match the decompile exactly — is much larger). Real
  decompile: an 8-iteration loop building a small per-index array
  (`DAT_0029c3c0`/`DAT_0029c3e0`/`UNK_0029c380`, calling `FUN_00243368`), **then
  falls into a sin/cos orbit-position update loop that is structurally
  byte-for-byte identical to `FUN_002354c8` and `FUN_0020eda0`** (see Light
  spots below): 7 iterations, radius falloff `(7-i)^2 * fGpffff81d8`, angle
  wrap against `fGpffff81b0..81e4`, position = `sin(angle)*radius*scale,
  cos(angle)*radius*scale`. **No DMA/GIF/VIF1 kick is visible anywhere in this
  function's decompile.** This directly conflicts with the render spine's
  label "`FUN_00235350()` kicks the built packet to the GS." Either the kick
  happens in code this decompile didn't reach (unlikely — no unreached branches
  in the printed CFG) or the render-spine's identification of "the kick" at
  this address needs re-verification against a live GS-dump trace. Flagging
  as a correction, not silently overwriting the earlier verified section.
- **0x375230 buffer / DMA path to VIF1/GIF**: not located this session — the
  actual `sceGsPutDrawEnv`/`sceDmaSend`-equivalent call was not identified
  (the two candidates suggested by the TODO, FUN_0022f720 and FUN_00235350,
  both turned out to be something else on inspection). UNVERIFIED, open. [HYPOTHESIS]

### Surface pass draw

[DECOMP-SOURCED]/[HYPOTHESIS] (direct decompile, internally consistent, but not independently live-confirmed)

- **`FUN_00232da0@0x00232da0`** (`(int,undefined8,long,long)`, the `+0x150` face
  pass, `param_4`=alpha-ish flag matching the render spine's `(…, 0xff)` call
  in pass 5). Decompile is clean and internally consistent: computes a per-rod
  gradient/brightness factor `fVar3` from `(rowIndex - param_1)` divided by a
  bucket-count constant (13/10/6/5 depending on `param_4` and an 8-vs-other
  branch on `in_v0_lo`), applies an extra hover-highlight scale when
  `unaff_s0_lo == iGpffff8b3c` (currently-hovered rod global) `&&
  iGpffff8b48==1`, stores the result at `rod+0x60`, then **tail-calls
  `FUN_002738e8(&stack0x20, rodPtr+0x30, rodPtr+0x10)`** and writes
  `rod+0xa8 = alphaArg`. PARTIAL — the gradient/highlight math is fully
  readable; the actual GS emit happens in `FUN_002738e8`, which lands in the
  same broken code region as `FUN_002738a0` (next section) and could not be
  verified.

### Transform+emit — `FUN_002738a0` / `FUN_002738e8`: address holds a misclassified DATA region, not code

[DECOMP-SOURCED]/[HYPOTHESIS] (disassembly-confirmed the tail-jump edge and the data-vs-code anomaly; the "why" — overlay/bank switch vs. stale Ghidra classification — remains unresolved hypothesis) This is the most important finding of the session. `draw_crystal_rod`
(`FUN_00232e38`) really does end with a genuine tail-jump to 0x002738a0 —
confirmed by direct disassembly of `FUN_00232e38`'s last instructions:
```
ld ra,0x70(sp) ... lwc1 f20,0x80(sp)
j 0x002738a0
_addiu sp,sp,0x90      ; delay slot, stack dealloc before the jump
```
with `a0/a1/a2` set up just before to `0x29bd90/0x29bd50/0x29bd10` — exactly
the args the render-spine doc already recorded. So the call graph edge is real.
But ~~**decompiling 0x002738a0 fails** ("Control flow encountered bad instruction
data" → `halt_baddata()`), and raw `disassemble_bytes` across 0x2738a0-0x273930
decodes as ~30 consecutive `cop0`/`cop1` "instructions" whose operand fields
increase **smoothly and monotonically** word-to-word (`0x2a8aefd, 0x2ca9f55,
0x2ec5e24, 0x306f55d, …`) — the unmistakable signature of a float/fixed-point
**data table** being disassembled as if it were code, not real instructions
(real branchy code doesn't produce a smooth arithmetic progression across
consecutive words, and COP0 privileged opcodes have no business appearing
in a tight run inside application code). `FUN_00232da0`'s tail call target
`FUN_002738e8` sits 0x48 bytes into this *same* garbage stream — confirmed by
disassembling it directly. **Conclusion: in the current Ghidra project, the
address that both `draw_crystal_rod` and `FUN_00232da0` jump to for vertex
transform+emit is not recoverable as code — it is either genuinely a data
table at that VMA (and the real function lives elsewhere, reached through
a mechanism not visible in the static call graph — e.g. an overlay/bank
switch, plausible given OSDSYS's known use of overlays for its different
screens), or the Ghidra database's code/data classification is stale for this
region and needs `reanalyze`/manual undefine-and-redisassemble before this
function can be read at all.** UNVERIFIED — this remains the single biggest
blocker to the port; static decompilation cannot resolve it further. A live
PCSX2 trace (single-step through the tail-jump with a breakpoint at
0x002738a0, per `docs/FOUNDATION-STATUS.md`'s tooling) is the only way forward.~~ [FALSIFIED → 2026-07-05 live: 0x002738a0 = sceVu0MulMatrix, 0x002738e8 = sceVu0ApplyMatrix (COP2 macro code Ghidra can't decompile); they compose/apply matrices only — NOT a vertex emitter. See sp0-live-reads.md]. Where the doc calls FUN_002738a0 "transform+emit", the real vertex→GS-packet emitter is the 0x0022FD00 family (0x22F720/0x22FB28/0x22FBE8/0x22F7F8, DMA-chain builders at 0x22fd58+), per sp0-live-reads.md.

### Geometry setup

[DECOMP-SOURCED]/[HYPOTHESIS]

- **`FUN_002335e8@0x002335e8`** (`(undefined8,undefined8,undefined8,undefined8,
  int param_5)`). Much smaller than hypothesized: body is exactly
  `*(param_5+0x130) = param_3; uRam002c8b3c = 0; *(param_5+0x140) = 0;`. Given
  the call site `FUN_002335e8(&stack0x1c0, &stack0x1c0+4, &stack0x1c8, 0x375250,
  clockState)`, this stores a stack-buffer pointer into `clockState+0x130` and
  resets `clockState+0x140` plus one global — a small bookkeeping reset, **not**
  the screen-scale computation. RESOLVED as to what this function does. The
  screen-scale math the TODO attributed to it (`fStack1c0/1c4 * fGpffff8318`)
  is real but lives in the **caller** (`ui_render_3d_objects`), executed right
  after this call returns, on the same stack slots this function just touched.
- **`FUN_002367c0@0x002367c0`**. Trivial: `return uGpffff8cc4;`. RESOLVED — a
  one-line accessor returning a global. Caller (`ui_render_3d_objects`) treats
  the return value as a `float[16]` pointer and both reads and writes a full
  4x4 matrix through it, so `uGpffff8cc4` is confirmed to be a **scratch
  matrix-workspace pointer** (name TBD — "current transform matrix" is the
  working label).
- **`FUN_00236a80@0x00236a80`** (body 0x236a80-0x236c93, 531B — decompiler
  showed only the guarded-init branch; confirmed by disassembly that this is
  real, not overlap/garbage). Structure: `if (iGpffff8cc8 != 1) { one-time init:
  FUN_00245bf0(1,1); FUN_00248538(); FUN_00272840(...); FUN_002726d0(...,0x106)
  /* Ghidra: does not return */ }`. Disassembly of the **skip-target tail**
  (0x236c74-0x236c93, the path taken once `iGpffff8cc8==1`, i.e. every frame
  after the first) is *exclusively* register-restore + `jr ra` — no float
  instructions, no use of the caller's x/y/z args at all. **REJECTED as "the Y
  translate"**: on the steady-state path (which is what runs every frame in
  the clock loop) this function is a no-op; the 3 float args the caller passes
  (`0, angle*26*t, 0`) are dead on that path. If a translate happens, it isn't
  here — either it's baked into whatever `FUN_00245bf0`/`FUN_00272840` set up
  once at startup, or the TODO's premise (this being a per-frame translate
  call) is wrong and it needs re-deriving from a live trace.

### Light spots — RESOLVED position formula, draw function still open

[DECOMP-SOURCED]/[HYPOTHESIS] (formula readable from static decompile; not independently live/dump-confirmed in this file)

- **`FUN_0020eda0@0x0020eda0`** (`(undefined8,undefined8,int param_3)`).
  **This is the light-spot position updater** — confirmed by a literal, direct
  write to the exact array address the TODO named: `*(float*)(idx*0x10 +
  0x34c830) = sin(angle)*radius*scale; *(float*)(idx*0x10 + 0x34c834) =
  cos(angle)*radius*scale;` for `idx` in a 7-iteration loop (`param_3..param_3+6,
  capped at 7`), i.e. covering 7 of the 8 `0x10`-byte slots (slot 0 is written
  by whatever calls this with `param_3=0` as a seed, or by a sibling routine).
  `radius = (7-idx)^2 * fGpffff81d8`, so trailing entries shrink — a **trailing
  after-image / comet-tail falloff**, matching the patent digest's "after-image"
  concept. Angle is advanced by `fGpffff81a0/81a4/81a8` per index and wrapped
  into `[fGpffff81d8, fGpffff81e0]` via the `81b0..81e4` constant family.
  RESOLVED (formula fully readable). `get_xrefs_to` shows exactly **one caller,
  `0x00211490`**, which is *not* inside `ui_render_3d_objects` — ~~this update
  runs once per frame from a separate top-level tick function, independent of
  the clock's render passes~~ [FALSIFIED for the crystal-clock screen → 2026-07-05 live: exec BP never fires there, 0x34c830 static {0,0,1160.0}; updater belongs to another screen/mode].
- **`FUN_002354c8@0x002354c8`** and the **tail of `FUN_00235350`** (see Packet/
  DMA above) contain the **same algorithm** as `FUN_0020eda0`, byte-for-byte
  structurally identical, but reading/writing through stack-passed pointers
  (`in_stack_00000020/30/40/50`) instead of the hardcoded `0x34c830` literal —
  i.e. a parameterized version of the same orbit/trail update, applied to a
  *different* buffer, called from within the clock's per-pass loop in
  `ui_render_3d_objects` (via `FUN_00235350`'s call sites). PARTIAL — formula
  confirmed identical to the resolved one above; which buffer it targets and
  why the clock path needs its own copy of this update is UNVERIFIED.
- **Draw function**: not located this session. `get_xrefs_to(0x34c830)`
  returns **no references** — the array is only ever touched through computed
  pointer arithmetic (as seen in `FUN_0020eda0`), so static xref search cannot
  find its consumer. Whatever reads `0x34c830` to emit GS sprites/points for
  the light spots remains UNVERIFIED/unlocated.

### Not attempted this session (unchanged from prior TODO)
- **sceVu0\* math lib** primitives, **swirl/orb wireframe**, **text/glyph
  blit**, **rod-array generation** (+0x00 origin / +0x140 normal writer) — out
  of this session's explicit function list; still open.
- `osd_decode_bcd_time@0x00221610` — unchanged, RESOLVED (prior session).

### Shared-tail / overlap anomaly (methodology note, applies to several entries above)

[DECOMP-SOURCED] Multiple addresses in this cluster (0x2324e8/0x232538; 0x230518/0x2401c8;
0x235350/0x2354c8/0x20eda0) turned out to be **Ghidra function-table entries
that don't correspond to independent, self-contained functions** — they're
either mid-body labels of a shared tail (confirmed via `disassemble_bytes`
showing a continuous, non-branching instruction stream across the declared
boundary) or genuinely separate functions that happen to implement the same
algorithm against different buffers. In every case this was caught by
cross-checking `decompile_function` output against raw `disassemble_bytes`/
`disassemble_function` at the same address and against the call site's actual
argument setup — decompile output alone was not trustworthy for boundary
correctness in this binary. Recommend the same cross-check for any further
work in this address range (0x2324e8-0x236c93 and 0x2738a0+ especially).

## Port target
Generated geometry (the 12-rod dial we have) [DUMP-MEASURED] (12-rod dial + 4 menu cubes, 16 slots total, per known-corrected count — supersedes the earlier "8 rods on a circle" hypothesis) → these ported passes emit a GS command
stream (GsPrimitive list with the REAL register templates + textures 11520/11200/…)
→ existing GsRenderer draws it faithfully. Validate: render generated clock at a
fixed time, pixel-diff vs GSRunner (the same gate, must reach the dump's 5–10%). [DUMP-MEASURED]

## ⚠️ STATIC-RE WALL (2026-07-03) — [FALSIFIED 2026-07-05: the wall is DOWN]

~~The TODO session hit a hard limit: **the transform+emit function `FUN_002738a0`
(the vertex → GS-packet crux that BOTH `draw_crystal_rod` and `FUN_00232da0`
tail-jump into) is a data table misclassified as code in this Ghidra DB** — its
bytes decode as ~30 monotonic cop0/cop1 ops; the decompiler aborts.~~
[FALSIFIED → 2026-07-05 live native disasm: 0x002738a0 = sceVu0MulMatrix,
0x002738e8 = sceVu0ApplyMatrix — COP2 macro-mode code this Ghidra config can't
decompile, NOT data, NOT an emitter. The real vertex→GS-packet emitter is the
0x0022FD00 family (0x22F720/0x22FB28/0x22FBE8/0x22F7F8 + DMA-chain builders at
0x22fd58+); method = live RAM + PCSX2 native disasm. See sp0-live-reads.md.]
The GIF/VIF1 DMA kick and the light-spot draw consumer were also not statically
locatable (computed-pointer dispatch) [HYPOTHESIS] (unresolved gap, consistent
with known-falsified item 6's "the real GS kick is UNLOCATED"). ~~So "port every
function by decompiling" is BLOCKED for exactly the functions that emit pixels.~~
[FALSIFIED → the block was a Ghidra-decompiler limitation, not an RE limit.]

**But the port does not need those internals — it needs their CONTRACT, and both
ends are observable.** `FUN_002738a0`'s inputs (rod model verts + rotation +
projection matrices) and its outputs (the GS packet vertices) are BOTH known: the
outputs are literally the parsed vertices in the dump (prims_sw.json — screen XY
in 12.4, UV, RGBA, per pass) [DUMP-MEASURED]. So we reproduce transform+emit BY CONTRACT:
  rod model verts → (rotation_build × projection_build, both already ported) →
  GS screen XY/UV/RGBA → GsPrimitive with the dump's per-pass register template
  + the real texture (11520/11200/8960…).
and VALIDATE numerically against the dump's actual rod vertices for the captured
time. The dump is the oracle; the emit math is fit to it, not guessed.

### The 3 real gaps (need a live PCSX2 trace, NOT more static decompile)
1. **Rod MODEL geometry**: the source prism cross-section verts + UVs each rod's
   0x160 struct feeds into transform+emit. Live-read the rod struct or the model
   template; or reconstruct from the dump's screen verts + the inverse projection.
2. **Per-pass register templates exact bytes**: read `DAT_002973a0`/`c0` from the
   dump/live (16B each) and confirm against the dump's ALPHA/TEX per pass.
3. **Light-spot + swirl draw**: `FUN_0020eda0` gives the spot POSITIONS (resolved);
   the GS emit for them + the swirl LINE_STRIP still need a live trace or dump-side
   reconstruction.

Methodology caution (subagent): addresses in 0x2324e8–0x236c93 and 0x2738a0+ are
shared-tail / overlapping table entries — cross-check `decompile_function` against
raw `disassemble_bytes` before trusting any single decompile in that range.

## Port-by-contract step 1 result (2026-07-03, MEASURED, not guessed)

[DUMP-MEASURED] This whole section is the underlying single-rod projection-fit work; per the known-falsified/contested list, "W1 projection" claims (1-rod projection fit) are PROVISIONAL — an underdetermined single-rod regression fit, not evidence-grade. Treat the measurements below as DUMP-MEASURED but the fit conclusions as PROVISIONAL.

Attempted the screen-space projection inversion of the rods from clock_sw.gs:
- A rod = a dense mesh of ~44 additive 4-vert quads (TBP0=11520) + ~44 feedback-
  refraction quads (TBP0=6720=FBP210) + subtractive/other = **~212 textured quads
  per rod**, NOT a simple prism. Base vertex RGB is near-black (8,8,8 / 40,40,40);
  the crystal look is the accumulated GS multi-pass, not a flat color.
- Dial hub found by grid-search (min rod tangential spread): **screen (313,115)**,
  rod half-width ~7px.
- **Same-model-rotated test (MEASURED):** rotating rod@180° by +30° onto rod@210°
  drops mean nearest-neighbor vertex distance from **41.7px → 15.8px**. Confirms the
  rods ARE one model rotated, BUT the 15.8px residual (> a rod's width) means a
  pure 2D screen-space rotation is NOT render-faithful.
- Residual causes: perspective foreshortening (tilted dial — 2D rotation can't
  capture it), per-rod own-axis spin, and the still-unknown FOV (blocks a proper
  3D inverse).

**CONCLUSION:** the 2D inversion proves the model is single+rotated but can't hit
render precision. [PROVISIONAL — W1 projection, single-rod fit] The clean unblock is a LIVE PCSX2 read of the rod MODEL geometry
(the source verts transform+emit consumes, pre-projection) — exact, no inversion
error. Same kind of live session already done for rotation speed / clockState.
NEXT: live-read the rod model (rod struct 0x160 @ 0x375250, or the model template).

## Rod tessellation — INFERRED FROM DUMP (2026-07-03, one rod fully characterized)

[DUMP-MEASURED] One physical rod (rod@210° in clock_sw.gs) = **338 textured GS quads across 5
layers**. The rod is a parametric bar (~146px radial × ~20px wide) covered by
large overlapping quads (NOT fine tessellation), each drawn 3-4× for additive/
subtractive intensity accumulation:

| layer | texture | blend (A B C D) | draws | distinct positions | role |
|-------|---------|-----------------|-------|--------------------|------|
| refraction | 6720 = FBP210 | 0 1 0 1 src-over | 62 | 18 | refracts the framebuffer background |
| crystal-sub | 11520 | 2 0 0 1 subtractive | 100 | 27 | crystal facet texture |
| crystal-add | 11520 | 0 2 0 1 additive | 62 | 18 | crystal facet texture |
| layer4 | 11200 | 0 1 0 1 src-over | 76 | 12 | — |
| layer5 | 11584 | 2 0 0 1 subtractive | 38 | 12 | — |

~87 distinct quad positions total (overlapping across layers). Additive-layer
detail: quads grow radially outward (rw 21→91px), UVs TILE the 64×64 crystal
texture along the length (s/q 0.64→2.69 — wraps), base vertex RGB near-black
(8,8,8 / 40,40,40) so the look is the accumulated multi-pass blend, not flat
colour. The rod's parametric source (origin/dir/screen) is live-readable at
0x375250+0x00/0x10/0x20 (rod struct 0x160). The tessellation FUNCTION
(transform+emit) is undecompilable, so the quad layout + UV tiling must be
reproduced from THIS measured pattern.

### Reproduction plan (validatable increments)
1. One layer, one rod: emit the ~18 additive-crystal quads of a parametric bar
   (origin/dir/len/width from the rod struct) with the tiling UVs → GsPrimitives
   → GsRenderer. Pixel-diff that rod's additive contribution vs the dump.
2. Add the other 4 layers (refraction samples FBP feedback — GsRenderer already
   does this; subtractive/src-over layers).
3. 12 rods at time-driven angles; then light-spots (FUN_0020eda0 formula RESOLVED)
   + swirl + text. Gate: full generated clock pixel-diff vs GSRunner ≤ dump's 5-10%.
