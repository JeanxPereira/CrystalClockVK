# Pixel-diff gate

Numeric fidelity gate for the GS render (the project bans eyeballing/photos).
Reference = GSRunner (bit-accurate PCSX2 software renderer) replaying the same
dump, compared at the NATIVE framebuffer resolution (640x224, no resample).

```sh
# 1. Reference: replay the dump in GSRunner with per-draw RT dumps, then take
#    the LAST rt1_00000 PNG (final FBP0 state) as the native reference.
#    (GSRunner: C:\CodingProjects\Personal\pcsx2-gsrunner, deps in bin\ on PATH.)
pcsx2-gsrunner.exe -renderer sw -dump rt -dumpdir out\ -logfile out\log.txt <dump.gs>
node tools/pixeldiff/png2rgba.mjs out\<last>_rt1_00000_C_32.png ref.rgba

# 2. Render one frame and dump the native FBP0 (640x224 RGBA8), then exit.
bin/CrystalClockVK.exe <dump.gs> --dump-rgba ours.rgba

# 3. Numeric diff (mean/max abs err + % over threshold) + heatmap PNG.
node tools/pixeldiff/pdiff.mjs "ours.rgba 640x224" "ref.rgba 640x224" heat.png
```

Notes:
- `extract_ref.mjs` (embedded 640x480 screenshot) remains as a fallback when
  GSRunner isn't available; its 480->224 resample inflates absolute numbers.
- The dump-and-exit path renders exactly one frame (deterministic: VRAM is
  re-seeded from the freeze each frame), so the gate is reproducible.
- Both dumps' framebuffer alpha is real fragment alpha now — force A=255
  before viewing raw RGBA as PNG or the image "looks white".
