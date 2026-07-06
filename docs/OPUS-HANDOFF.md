# Opus Handoff — CrystalClockVK Restart

> Audit 2026-07-05: claims status-tagged per master-strategy spec §6.

> Audience: the managing agent (Opus) taking over after the Phase 0 audit.
> Fable produced the audit + cut plan; Opus executes cuts, then drives the GS-spec rebuild,
> delegating bulk work to smaller agents (Sonnet/Haiku). Token discipline: fan-out agents
> never run on the top-tier model.

## 0. Current state (UPDATED 2026-06-12 — Phase 0 COMPLETE, on main)
- Audit DONE (`docs/PHASE0-AUDIT.md`). Phase 1 architecture spec APPROVED
  (`docs/superpowers/specs/2026-06-12-phase1-gs-vk-architecture-design.md`).
- **Phase 0 surgical cuts EXECUTED + merged to main** (11 commits) `[HYPOTHESIS]` (project-state
  claim, not independently re-verified by this audit). Builds green; app renders
  black + ImGui (intended exit). `gs/` constants live-verified `[LIVE-VERIFIED]`, `RenderOrchestrator` stubbed,
  `PipelineBuilder`→`setBlendState`, poison docs deleted, MEMORY/CLAUDE rewritten.
- Foundation VERIFIED LIVE (`docs/FOUNDATION-STATUS.md`) `[LIVE-VERIFIED]`: ghidra (`program="OSDSYS.elf"`),
  CrystalOSD decomp, pcsx2-mcp (bundled patched `pcsx2-qt.exe` + DebugServer), `clock_viewer.gs`
  captured + validated, `pcsx2-ref` GS source cloned.
- User approved this direction; do NOT re-litigate the audit.

### → IMMEDIATE NEXT: Phase 1 W1 (GS dump parser)
Parse `C:\Users\dell04\Documents\PCSX2\snaps\clock_viewer.gs` (decompressed, 6.3 MB) into the GS
command stream (spec §4.1). Format grounded in `pcsx2-ref`:
- `pcsx2-ref\pcsx2\GS\GSDump.h` — `0xFFFFFFFF` + `GSDumpHeader` (9×u32) + state freeze +
  `[id/1][data]` packets (0=Transfer GIF, 1=VSync, 2=ReadFIFO2, 3=Regs). Read `GSDump.cpp::AddHeader`
  for exact field offsets. GIFtag/register decode: `GSRegs.h`. Swizzle/PSM: `GSLocalMemory.cpp`.
- First milestone: header fields + transfer/primitive counts from `clock_viewer.gs` (a quick Node
  pass validates the format before the C++ `tools/gsdump/`). `.gs.zst` → Node `zlib.zstdDecompressSync`.

### Build on this machine (Windows)
Vulkan SDK `C:/VulkanSDK/1.4.350.0` does NOT propagate to a running process — set inline:
`VULKAN_SDK="C:/VulkanSDK/1.4.350.0" PATH="$PATH:/c/VulkanSDK/1.4.350.0/Bin"`. ImGui submodule:
`git submodule update --init --recursive 3rdparty/imgui`. Presets are macOS-pathed → plain config:
`cmake -B build && cmake --build build --config Debug -j` (VS generator). Binary `bin/CrystalClockVK.exe`.

## 1. Mission invariants (never violate)
1. Style = GS rasterizer. Replicate `(A-B)*C/128+D` blend, COLCLAMP, dither, framebuffer-feedback
   refraction, additive glow, 12.4 fixed-point — in shaders, driven by DECODED state, never guessed.
2. Spec = the byte-perfect OSDSYS decomp (CrystalOSD repo). GS-packet builders
   (`pktSetAlphaBlend`, `pktSetTEST_1`, `pktSetAD`, `sceGsPutDrawEnv`) are the style source.
3. Precision = machine-readable: PCSX2 GS dumps + SW-renderer reference frames + numeric
   pixel-diff. Photo comparison is banned.
4. No VU1 microcode exists in clock/opening `[DECOMP-SOURCED]`. "VU work" = porting the `sceVu0*` macro lib
   (42 funcs, known PS2SDK semantics) `[DECOMP-SOURCED]`.
5. Any numeric constant entering `gs/` or shaders needs provenance: decomp address, GS dump
   packet, or live-trace register. Cite it in the commit body.

## 2. Order of operations
1. **Execute the cut plan** (link above), task by task.
2. **Architecture brainstorm WITH THE USER** — explicitly requested; do not skip to code.
   Decision agenda in §4.
3. **Phase 1** workstreams (§3) after the user signs off the architecture.

## 3. Phase 1 workstreams + acceptance criteria

### W1 — GS dump parser (highest value, zero deps)
Capture an OSDSYS clock frame in PCSX2 (GS dump). Write a standalone parser (suggest
`tools/gsdump/`) for GIFtags + A+D register writes + PRIM/UV/XYZ/RGBAQ streams.
**Accept:** machine-readable listing (JSON) of every primitive with full register state
(ALPHA, TEST, TEX0, FRAME, XYOFFSET, COLCLAMP, DTHE/DIMX) for one frame; cross-referenced
against the decomp builder that emitted it.

### W2 — sceVu0* subset port
Via ghidra-mcp on hddosd.elf + CrystalOSD decomp, port to `src/gs/` (pure, unit-tested):
`RotTransPers(N)`, `ClipScreen/ClipAll`, `LightColorMatrix`/`NormalLightMatrix`,
`ApplyMatrix`, `CameraMatrix/ViewScreenMatrix`, `FTOI4/ITOF4` (12.4 = ×16).
**Accept:** unit tests feeding known inputs match PCSX2 live-register outputs.

### W3 — GS→VK translator
Map the SUBSET of GS state OSDSYS uses to VK pipelines/shaders. Input: `GsAlpha/GsTest/...`
structs (already rebuilt clean). Output: `VkPipelineColorBlendAttachmentState` when fixed-function
is exact, shader-emulated blend via `VK_KHR_dynamic_rendering_local_read` when not
(GS multiplies by C/128, Vulkan factors are /255 — decide per mode; see §4.3).
**Accept:** for every ALPHA config found by W1, a documented VK equivalent + a unit test
comparing blended pixel values against the GS formula in 8-bit integer math.

### W4 — First rendered element + pixel-diff loop
Target: opening alpha quads (`func_0021E950` reference) — simplest real element `[DECOMP-SOURCED]`.
Build the diff harness: render same frame in PCSX2 SW renderer (bit-accurate) and in VK,
compare numerically.
**Accept:** automated per-channel diff report; agreed threshold met (define with user — start
mean abs error per channel, report max too).

Then iterate outward: fog → crystal/refraction → rods (strategy doc §7).

## 4. Open decisions — brainstorm agenda with the user
1. **Swapchain color space**: current `B8G8R8A8_UNORM + SRGB_NONLINEAR` lets the compositor
   gamma-lift linear GS pixels (audit finding). Options: pass-through colorspace, explicit
   gamma in final blit, or accept sRGB and encode accordingly. Affects every pixel-diff.
2. **Internal render resolution**: GS renders 640×448 (NTSC interlaced context). Native-res
   target vs upscale policy, and where pixel-diff happens (must be at GS-native res).
3. **Blend emulation strategy per mode**: fixed-function where mathematically exact vs
   local_read shader blend everywhere (uniformity + dither/COLCLAMP control, but costlier).
4. **Dither**: GS DIMX 4×4 ordered dither — emulate in fragment shader? Only if OSDSYS
   enables DTHE (W1 answers this — check before building anything).
5. **Translator placement**: `gs/` is Vulkan-free by directive; translator needs both worlds.
   Suggest new layer `src/gsvk/` (GS structs in, VK state out).
6. **EE-logic layer language**: strategy doc says ported C/C++ builds the same GS packets;
   confirm scope (full packet stream emission vs direct state structs).

## 5. Salvage snippets (from amputated files)
Screen-space UV derivation (was `shaders/Crystal.vert:44-47`) — the only reusable shader logic:
```glsl
vec2 ndc = gl_Position.xy / gl_Position.w;
fragScreenUV = ndc * 0.5 + 0.5;
```
Trace-confirmed rod colors + GP floats now live in `src/gs/GsConstants.hpp` (Task 1 of cut plan) `[LIVE-VERIFIED]`.

## 6. Tooling map
- **ghidra-mcp** (MANDATORY for `gs/` logic): pseudocode from `hddosd.elf`. NOTE: registered in
  user-level config, not project `.mcp.json` — verify availability at session start.
- **pcsx2-mcp** (project `.mcp.json`): live EE memory/registers, BPs, dumps. Paused PC always
  reads `0x00081fc0` (BIOS idle) — set BP inside OSDSYS code (real prologue, `addiu sp,-N`)
  to capture context. GP = ~~`0x002AF070`~~ `[FALSIFIED → live/correct gp is 0x002CFEF0, live BP 2026-06-12]`. Any address
  derived from the stale GP above is itself stale — see MEMORY.md live-render-chain note.
- **CrystalOSD decomp**: CONFIRMED at `C:\CodingProjects\Personal\CrystalOSD` (the `D:\...`
  references in old docs are WRONG — that path does not exist). `asm/clock/` has
  `clock_orb_rendering_func.s`; `asm/graph/` has the GS packet builders (`pktSetAlphaBlend.s`
  etc.). See `docs/FOUNDATION-STATUS.md` for the full verified map.
- **Ghidra program selection**: two programs are open; `hddosd.elf` is active by default but
  clock addresses (0x0022xxxx) live in `OSDSYS.elf` `[LIVE-VERIFIED]`. ALWAYS pass `program="OSDSYS.elf"` for
  clock decompiles or you silently hit the wrong binary.
- **RenderDoc**: in-app trigger button already wired (`RenderDocWrapper`).
- **PCSX2 SW renderer**: ground-truth frames for W4.

## 7. Trust map for repo docs (post-cut)
| Doc | Trust |
|---|---|
| `docs/OSDSYS-DECOMP-1to1-STRATEGY.md` | Master spec (rod stride fixed to 0x140) `[HYPOTHESIS — possibly superseded; later measurement gives stride 0x160 for the 12-rod dial + 4 menu cubes, not on the audit's confirmed-falsified list, flagged not corrected]` |
| `docs/PHASE0-AUDIT.md` | Audit record |
| `MEMORY.md` live-trace section | Ground truth (addresses, GP floats, rod struct, colors) |
| `docs/clock_patent/*` | Refraction METHOD only — never numeric spec |
| `docs/ghidra_analysis/vu0_decode.md` | Quarantined — raw decode ok, GLM reconstruction unverified |
| Anything else claiming GS primitives/addresses | Deleted in Phase 0; if found, treat as poison |

## 8. Working conventions
- Build: `cmake -B build && cmake --build build`; binary `bin/CrystalClockVK.exe`.
- Commits: `Type(Scope): Imperative ≤72` (Fix/Feat/Refactor/Perf/Build/Docs/GS ×
  Core/Renderer/App/GS/Shaders/CI/Project).
- Code: English only, zero comments except complex GS math, PascalCase boundaries,
  `gs/` Vulkan-free.
- Keep `MEMORY.md`/`CLAUDE.md` updated as findings land; new trace data goes in MEMORY with
  date + capture method.
- Subagents: Sonnet for audits/searches/ports, Haiku for trivial sweeps. Top-tier only for
  synthesis/management decisions.
