# Docs Audit Index — SP0 Task 2

> Audit 2026-07-05: claims status-tagged per master-strategy spec §6.

Every in-scope doc was swept claim-by-claim and tagged
`[LIVE-VERIFIED]` / `[DUMP-MEASURED]` / `[DECOMP-SOURCED]` / `[HYPOTHESIS]` /
`[FALSIFIED → correction + evidence]`. Falsified text stays visible
(struck through) with the correction beside it — per spec §6, corrections
edit the source doc in place; append-only correction layers are banned.

Counts below: "FALSIFIED fixes" = inline `[FALSIFIED → ...]` corrections applied
(occurrences, from the spec §5.2 known-falsified list only); "HYPOTHESIS" =
remaining unverified-claim tags after the sweep.

| File | Audited | FALSIFIED fixes | HYPOTHESIS tags |
|------|---------|-----------------|-----------------|
| `CLAUDE.md` | 2026-07-05 | 0 (path fixes: pcsx2 ref path corrected; CrystalOSD path marked STALE) | 0 |
| `docs/FOUNDATION-STATUS.md` | 2026-07-05 | 3 | 1 |
| `docs/OPUS-HANDOFF.md` | 2026-07-05 | 1 | 2 |
| `docs/OSDSYS-DECOMP-1to1-STRATEGY.md` | 2026-07-05 | 0 | 15 |
| `docs/PHASE0-AUDIT.md` | 2026-07-05 | 0 | 40 |
| `docs/W4-GS-ACCURACY-PLAN.md` | 2026-07-05 | 0 | 13 |
| `docs/ghidra_analysis/CLOCK-SYSTEM-MAP.md` | 2026-07-05 | 4 | 33 |
| `docs/ghidra_analysis/PORT-FUNCTION-MAP.md` | 2026-07-05 | 5 | 12 |
| `docs/ghidra_analysis/menu-ui.md` | 2026-07-05 | 0 | 22 |
| `docs/ghidra_analysis/orbs-particles.md` | 2026-07-05 | 5 | 26 |
| `docs/ghidra_analysis/rod-pipeline.md` | 2026-07-05 | 6 | 22 |
| `docs/ghidra_analysis/runtime-trace.md` | 2026-07-05 | 0 | 18 |
| `docs/ghidra_analysis/settings-config.md` | 2026-07-05 | 0 | 7 |
| `docs/ghidra_analysis/sp0-live-reads.md` | born tagged (new this session) | 0 | 2 |
| `docs/ghidra_analysis/vu0-math-pipeline.md` | 2026-07-05 | 0 | 26 |
| `docs/ghidra_analysis/vu0_decode.md` | 2026-07-05 | 0 | 14 |
| `docs/ghidra_analysis/w0-angle-steps.md` | 2026-07-05 | 0 | 4 |
| `docs/ghidra_analysis/w0-blend-decode.md` | 2026-07-05 | 2 (+ W0 blend remap marked CONTESTED) | 6 |
| `docs/ghidra_analysis/w0-orbit-motion.md` | 2026-07-05 | 0 | 7 |
| `docs/ghidra_analysis/w0-projection-constants.md` | 2026-07-05 | 0 (W1 projection marked PROVISIONAL) | 4 |
| `docs/ghidra_analysis/w2-rod-generation.md` | 2026-07-05 | 1 | 19 |
| `docs/ghidra_analysis/w2-rod-geometry-live.md` | 2026-07-05 | 7 | 11 |

Out of scope (not audited, by design): `docs/superpowers/` (design specs/plans),
`docs/clock_patent/` (external reference material).

## Ambiguities left as HYPOTHESIS (candidates for a future falsified-list update)

Flagged during the sweep but NOT on the authorized §5.2 list, so tagged
`[HYPOTHESIS]` instead of FALSIFIED (never upgrade without evidence):

- Rod struct stride 0x140 vs 0x160 conflict (`OPUS-HANDOFF.md` trust-map vs
  `PHASE0-AUDIT.md` own corrections vs the 0x160 used in the live reads).
- `CLOCK-SYSTEM-MAP.md` §2 render-pass table (`0x00225760` / `0x00211558`)
  conflicts with its own §7 note that the real orbs render is `FUN_00225be8`.
- `orbs-particles.md`: `FUN_00230518(0,3)` labeled "sets Z-test / depth mode" —
  same function as falsified item "blend setter" but different wording; flagged
  contested, not force-corrected.
- `rod-pipeline.md`: "3936 draws" vs the dump-measured 3948 — close but not
  identical; flagged as discrepancy, not silently rewritten.
- `menu-ui.md`: `FUN_0020a730` "GS alpha setter via vtable dispatch" —
  unverified, same risk class as the falsified GS-setter labels.
- `w0-orbit-motion.md`: "2 physics orbits / 3-4 render orbs" conclusion.
- `PHASE0-AUDIT.md`: carries its own correction set (ROD_STRUCT_SIZE,
  ANGLE_STEP_*, fGpffff8c28) with no recorded evidence trail.
