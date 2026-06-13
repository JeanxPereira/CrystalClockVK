# Pixel-diff gate

Numeric fidelity gate for the GS render (the project bans eyeballing/photos).
Compares our rendered display frame against the ground-truth PCSX2 frame embedded
in the `.gs` dump. Three steps:

```sh
# 1. Extract the ground-truth display frame embedded in the dump (640x480 RGBA8).
node tools/pixeldiff/extract_ref.mjs <dump.gs> ref.rgba

# 2. Render one frame and dump the native FBP0 (640x224 RGBA8), then exit.
bin/CrystalClockVK.exe <dump.gs> --dump-rgba ours.rgba

# 3. Numeric diff (mean/max abs err + % over threshold) + heatmap PNG.
node tools/pixeldiff/pdiff.mjs "ours.rgba 640x224" "ref.rgba 640x480" heat.png
```

Notes:
- Our FBP0 is one 224-tall GS framebuffer; the embedded screenshot is the full
  640x480 display. `pdiff` resamples (nearest, proportional), so the vertical
  field/overscan mismatch adds some error — treat absolute numbers as a trend,
  not a pass/fail bit, until height alignment is tightened.
- Reference is RGBA8 (verified: RGB-vs-RGB error < RGB-vs-BGR), matching our dump.
- The dump-and-exit path renders exactly one frame (deterministic: VRAM is
  re-seeded from the freeze each frame), so the gate is reproducible.
