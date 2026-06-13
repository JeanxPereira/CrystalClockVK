# Foundation Status — verified 2026-06-12 (Opus)

> Pre-development house check. Goal: prove the ground-truth tooling works BEFORE refactoring,
> so the rebuild reads OSDSYS from evidence, never guesses. Verdict: the decomp/Ghidra path is
> proven solid; the live-emulation MCPs are not wired on this machine.

## ✅ Proven working

### ghidra-mcp — reads OSDSYS without guessing (CONFIRMED)
Two programs loaded: `OSDSYS.elf` (2009 funcs, base 0x001f0000) and `hddosd.elf` (4451 funcs,
base 0x00200000, **currently active**).
- **Clock addresses (0x0022xxxx) resolve in `OSDSYS.elf`.** Decompiled the rod render loop
  (function `FUN_00225be8`, contains the 0x00225E80 the docs call "master render"). The GS
  style is literally in the pseudocode — no interpretation needed:
  - `(iVar8 >> 7) * iVar5` → the GS `C/128` blend divide.
  - `iVar14 + 0x7f` → GS rounding bias (+127).
  - alpha ramp clamped to `128.0` → GS alpha is 0–128, not 0–255.
  - `(*pfVar7 + 2048.0) * 16.0` → XYOFFSET (+2048) and 12.4 fixed-point (×16).
  - This **confirms `GS_FIXED_POINT_SCALE = 16`** (cut plan Task 1), not the old 65536.
- **OPERATIONAL NOTE:** active program is `hddosd.elf`. Every clock decompile MUST pass
  `program="OSDSYS.elf"` explicitly or it silently targets the wrong binary.
- Ghidra `executable_path` shows macOS paths (`/Users/jeanxpereira/...`) — project was authored
  on macOS, now bridged on this Windows box. Decompilation works regardless.

### CrystalOSD decomp — the spec, located and real
**Path: `C:\CodingProjects\Personal\CrystalOSD`** (the `D:\...` in old MEMORY/handoff is WRONG).
- `asm/clock/` — render funcs incl. `clock_orb_rendering_func.s`.
- `asm/graph/` — the style spec, already isolated as named functions:
  `pktSetAlphaBlend.s`, `pktSetTEST_1.s`, `pktSetAD.s`, `pktSetCLAMP_1.s`, `pktSetSCISSOR_1.s`,
  `vif1SetAlphaBlend.s`, `vif1SetTEST_1.s`, `vif1SetZTest.s`, `gsAllocBuffer.s`.
  `pktSetAlphaBlend` builds GS A+D packets for registers 0x42 (ALPHA) — readable, currently
  `nonmatching` (disassembled, not yet C-matched).
- `src/` — partial C ports exist (cdvd full, clock partial). Most render funcs still asm-only;
  Ghidra pseudocode is the practical read path until matched.
- Build needs `ee-gcc 2.9-991111` (`$MATCH_CC`) for byte-match; modern ps2dev won't match.
  Source binary: `OSDSYS_A_XLF_decrypted_unpacked.elf`.

## ✅ pcsx2-mcp — NOW WORKING (fixed + live-verified 2026-06-12)
The `.mcp.json` path was stale (`D:\DownloadLibrary\...`, a different machine). Corrected to
**`C:\CodingProjects\Personal\PCSX2-MCP\pcsx2-mcp-server\dist\index.js`** (the user's bundle).
After a Claude restart the tools loaded and connected live: DebugServer (127.0.0.1:21512) +
Pine (28011). Verified end-to-end: pause, read_registers (128-bit), read_memory, disassemble,
execution breakpoints (idle BP fired; clock-render BP fired). Game ID `20080220` confirms the
v2.30 USA BIOS match.
- **MUST run the bundled patched `C:\CodingProjects\Personal\PCSX2-MCP\pcsx2-qt.exe`** — it
  carries the DebugServer patch. A stock PCSX2 has no DebugServer and will never connect.
- **GOTCHA:** do NOT reset the emulator with OSDSYS execution BPs armed — during early boot
  those addresses hold pre-load garbage and the EE recompiler can crash
  ("Jump to unmapped recLUT page"). Arm BPs AFTER boot, or re-enter the clock screen to catch
  the screen-build code. The crash is transient; reset again and it boots clean.

## ❌ renderdoc-mcp — DISCARDED (2026-06-12, user-confirmed)
RenderDoc ships only a Python 3.6 module (v1.44 still bundles `python36.dll`); the MCP does
in-process `import renderdoc` and requires Python ≥3.10 — an irreconcilable ABI wall without a
from-source RenderDoc build against modern Python. Not worth it for a debug-convenience tool
that is NOT ground truth. Use the in-app capture button + the qrenderdoc GUI when needed.

### ✅ GS dumps — CAPTURED + validated (2026-06-12)
Two frames captured in `C:\Users\dell04\Documents\PCSX2\snaps\` (each with a matching `.png`
reference screenshot — the ground-truth visual + primitive pair):
- **`clock_viewer.gs`** (6.17 MB, from `..._203208.gs.zst`) — **the crystal clock (Visor mode):
  radial ring of prism rods around the central glow. THIS IS THE PHASE-1 TARGET.**
- `config_menu.gs` (6.33 MB, from `..._203126.gs.zst`) — System Config screen (floating cubes,
  selected cube glows). Deferred / future scope.

Both are valid PCSX2 GSDump: serial `20080220-175343` (matches our BIOS), screenshot 640×480
(PS2 NTSC), ~1.2 MB GS state freeze + GIF transfer stream. **Ground-truth pipeline
(capture → decompress → parse) proven viable.** No zstd CLI / python-zstandard on the box —
decompress via Node 26 `zlib.zstdDecompressSync` (`/tmp/unzst.mjs`).

## 🔬 Live render-chain correction (captured 2026-06-12, zero guessing)
A live breakpoint on the crystal screen DISPROVED the docs' "master render = 0x00225E80".
That address (and 0x00225be8) NEVER execute per-frame — they are builder/init or stale.
The **real per-frame rod render is `0x00232618`** (BP hit at +0x28 = 0x00232640), reached via
this backtrace:
```
0x00221408 → 0x00221060 → 0x00221558 → 0x0022e738 → 0x0022c8d0 → 0x00233928 → 0x00232618
```
Live registers at the hit (use to re-seed Phase-1 RE; heap addresses are per-boot):
- `s3 = 0x00375250` = ROD_GROUP_A_ADDR ✅ (MEMORY §7 correct here, confirmed live)
- `s0 = 0x20297220` = GS packet build buffer (uncached; matches the `DAT_20297xxx` writes in the decompile)
- `v1 = 0x1000A000` = VIF1 FIFO → render submits via VIF1/GIF DMA
- `gp = 0x002CFEF0` ⚠️ ≠ MEMORY §7's `0x002AF070` — every GP-relative ABSOLUTE address in §7
  (the angle-step / refraction-displacement float table) must be RE-RESOLVED; only the offsets are stable.
- Rod group @0x375250 holds float positions (~-11.4, 47.6) + 12.4 fixed-point screen coords
  (`0x784d`/16 = 1924.8px, matching float `0x44f09a18`).
**This is the canonical example of the no-guessing pipeline working: a documented constant,
disproven live, corrected from evidence. Treat MEMORY §7 render addresses as suspect until
re-confirmed this way.**

## Environment note
Project moved macOS (`jeanxpereira`) → Windows (`dell04`). Stale on this box: CMakePresets
Vulkan SDK paths (macOS), pcsx2-mcp `D:\` path, decomp `D:\` references. PCSX2 likely needs
Windows setup.

## Dependency reality for Phase 1
- **W1 (GS dump parser)** needs: ONE captured OSDSYS clock `.gsdump`. Then fully offline.
- **W2 (sceVu0* port)** needs: ghidra-mcp (✅ works) + decomp (✅). Unblocked now.
- **W3 (GS→VK translator)** needs: W1 output + the clean `gs/` structs. Unblocked after W1.
- **Live pcsx2-mcp** is only required for resolving runtime globals (MEMORY §7-style work),
  not for W1/W2/W3 core. Nice-to-have, not a blocker.
- **renderdoc** is a debugging aid for W4 divergence, replaceable by the manual RenderDoc GUI.
