# Native Vulkan renderer of the OSD clock — design

Date: 2026-10-03. Status: proof of concept.

## Goal

Rewrite the PS2 OSD crystal clock and the screens around it as native C++ and Vulkan code: a scene
with named objects and named passes, free to change later, that reaches visual parity with the
PS2 Graphics Synthesizer at native resolution within a declared, measured tolerance.

The GS is scaffolding, not foundation. Every GS rule the parity needs lives in one renderer mode
(`GsParity`) and one test-only folder (`parity/`); both can be deleted without touching the scene.

The proof of concept answers one question: are the native passes, in `GsParity` mode, convincing
and inside the tolerance?

## Decisions

| Decision | Choice |
|---|---|
| Product | Native-exact first; a scaled mode later as a resolution multiplier |
| Scope | The clock, the main menu, System Configuration with its cubes, Clock Adjustment, the transitions between them, and their text |
| Parity | Visually indistinguishable with a declared, measured tolerance; not byte-identical |
| Platform | Windows, Vulkan 1.4, AMD RDNA2 (RX 6750 XT) baseline. MoltenVK is future work |
| Canon build | HDD OSD 1.10U; ROM 2.30 is reference only where the builds differ |
| Video | NTSC, English |
| Source of truth | `facts/` (rules), `References/model/` (JS model), Watson captures (`whole2`, `whole3`) |
| Oracle for pixels | PCSX2 v2.9.94 software renderer replaying the `.gs` dumps (every `whole2` / `whole3` capture has one). Compared on GS local memory (display and work buffers), not on the presented image, which passes through the emulator's deinterlacer. The software renderer is itself a model of the GS, not the hardware |
| First plan | Slices 0 and 1 only; slices 2 and 3 are planned after their verdict |

## Architecture

| Layer | Role | Depends on |
|---|---|---|
| `core/` | vk-bootstrap, SDL3, RenderDoc (as today) | — |
| `renderer/` | Executes a `FrameDescription` in Vulkan; modes `GsParity` and `Native` | `core/` |
| `scene/` | Plain C++, no Vulkan: clock state, camera, rod and orb placement, ramps, transitions, menus, text layout. Builds one `FrameDescription` per frame | nothing |
| `app/` | Real time, input, screen sequence, presentation | all above |
| `parity/` | Test-only: decodes a capture's GS packets into a `FrameDescription`, and quantizes any `FrameDescription` to GS units for comparison | `scene/` |

`gs/` (swizzle, VRAM, texture decoder) and the hand-made refraction in `app/` and `shaders/`
predate `facts/` and are removed. The `renderer/` wrapper (PassRecorder, PipelineBuilder,
DescriptorAllocator, ResourceManager, VMA) stays. `CLAUDE.md` changes its layer list accordingly.

### The seam: `FrameDescription`

Native units only; nothing in it is a GS register.

```
FrameDescription
  field       : 0 | 1     (the OSD draws interlaced: the second field is offset half a pixel down,
                           `XYOFFSET` + 8 in `clock-gs-state.md`; `Native` mode may ignore it)
  targets[]   : { id (Display | named work buffer), width, height, clearColour? }
  passes[]    : Pass
Pass
  name        : RodRefraction | RodTextured | RodReflection | ExtraPass | OrbTrail | OrbSprite |
                BackgroundTube | Blur | Copy | Tint | FadeOverlay | Vignette | Bars |
                ConfigCube | Text | MenuPanel   (slice 3 may add names from its facts pages)
  target      : target id
  blend       : { op: Opaque | Add | Subtract | Fade | Fixed, constant }   (from ALPHA_1 meanings)
  depth       : { test: Always | Greater | GreaterEqual, write }
  texture     : { source: Image(name) | Target(id), address: Repeat | Clamp | RegionClamp(rect)
                  | RegionRepeat, filter: Nearest | Bilinear }?
  primitive   : Triangles | TriangleStrip | TriangleFan | Sprites | LineStrip
  vertices[]  : { position: float2 (pixels), depth: float, colour: float4, uv: float2, q: float }
```

Pass names follow the facts pages (`clock-frame.md`, `clock-rod-draw.md`,
`clock-extra-passes.md`, `clock-frame-rest.md`, `config-cubes.md`, `text.md`). The blend
operations are one per `ALPHA_1` value those pages list, named by what it does.

The decoder in `parity/` gives the renderer real frames before any scene code exists: slice 1
renders `FrameDescription`s decoded from a capture. The same decoder is the geometry test of
`scene/` later (decoded capture against the scene's frame, both quantized to GS units). It is
test scaffolding; the application never decodes packets.

## Renderer: GS features and their Vulkan form

| GS feature (from `facts/`) | Vulkan, `GsParity` mode | Measured parity |
|---|---|---|
| Display and work buffers (by FBP) | One `RGBA8_UINT` image per buffer at native size; the display has two, alternating | exact |
| 12.4 coordinates with the screen offset | Rows walked on the CPU as the software renderer's `DrawTriangle` does; a row is drawn as a rectangle over exactly its pixels | exact (differ 0 on every non-AA group) |
| Depth, `GREATER` / `GEQUAL` (`ZBUF_1` `0x8C`: 32-bit format, values up to 16777215 by the screen matrix) | `r32ui` storage image compared in the fragment shader inside the interlock; Z per pixel is the software renderer's double arithmetic | exact (depth differ 0 on every group); not `D32_SFLOAT` with hardware compare, whose interpolated Z flipped tests |
| Blend `((A - B) * C >> 7) + D`, alpha 0x80 = 1, `FIX`, `COLCLAMP` 1 | Fragment shader in integers on the destination read inside the interlock | exact |
| `DTHE` 0 | No dithering | exact |
| Texture address modes, `TEX1` `0x61` bilinear | Integer addressing with the GS modes; bilinear weights are the four bits under the integer part, `lerp16_4` | exact |
| Gouraud colour, ST/Q, sprite UV | Stepped per row as `CDrawScanline` does in blocks of four (colour in 16 bits, S T Q floats with `divps`-rounded division, sprite coordinates stepped); S, T and Q rounded down as PCSX2 `FlushPrim` does on sprites and flat-Z draws | exact |
| Edge antialiasing `PRIM.AA1` on lines and triangles | Edge pixels walked on the CPU by the software renderer's DDA and coverage, drawn as one-pixel rows in the same ordered draw; `AA1` is on the rods' refracted faces as well as the orb trails | exact except edge pixels whose bilinear taps fall outside the draw's texture rectangle (see Tolerance) |
| Text fans and sprites with CLUT | Triangles; CLUT resolved at texture load | skipped by the fixture maker (paletted textures), taken from the oracle |

`GsParity` is a set of specialization constants. `Native` turns them off: float blend, standard
samplers, free resolution. In the proof of concept `Native` is a toggle, nothing more.

### Ordering inside a draw

Shader blending reads the destination, so overlapping primitives must see each other's writes in
primitive order. Measured on the baseline (RX 6750 XT, driver 26.8.1, Vulkan 1.4.315):
`VK_EXT_rasterization_order_attachment_access` is absent; `VK_EXT_fragment_shader_interlock`
(`fragmentShaderPixelInterlock`) and `VK_KHR_dynamic_rendering_local_read` are present.

- First choice: the target bound as a storage image, read and written inside
  `pixel_interlock_ordered`. Order per pixel is the primitive order by definition; no draw is
  split.
- Fallback: attachment local read with a barrier between draws, splitting a draw where its
  primitives overlap (cost: draw count, not parity).

Used: the storage-image method, with the target and depth images declared `coherent`; no draw is
split. Proven by two tests: 64 overlapping sprites blended in order, and 200 screen-sized sprites
under `Z >=` (without `coherent` this test read stale values in 15 to 25 pixels per run; the clock
then showed 659 stale pixels in its `aa z-gequal` group). The fallback is not built.

### Tolerance

The oracle is PCSX2's software renderer (`pcsx2-gsrunner`, SSE4.1 build, block width 4), not a
hardware GS. Parity means agreeing with that renderer, whose rules were read from its C path and
confirmed in its JIT. Measured on three captures at frame 0 (`hddosd-110U-whole3-clock`,
`hddosd-110U-whole3-config`, `hddosd-110U-whole2-to-clock`, 188 / 360 / 359 passes, each pass
compared from the oracle's buffers before it, then the whole frame chained): every group without a
cause below differs on 0 pixels, colour and depth.

| Cause | Share of the group's pixels | Largest difference | Capture |
|---|---|---|---|
| Interpolation of colour, texel boundary, depth interpolation | 0 | 0 | all three (computed per pixel by the software renderer's arithmetic) |
| STQ rounding (`GSState::FlushPrim` rounds S, T, Q down on sprites and flat-Z draws; an input rule of the oracle, applied by the renderer) | 0 | 0 | config, to-clock |
| Edge antialiasing | 0 | 0 | all three |
| PCSX2 software texture cache (oracle artifact, not imitated): the cache converts only the draw's texture rectangle, so a bilinear tap outside it reads zero | `aa z-gequal` 5.9e-6 (6 of 1018298); `aa z-always` 2.6e-6 (2 of 797569) | 103 (`aa z-gequal`, clock); 28 (`aa z-always`, to-clock) | clock, config, to-clock |

Chained whole frame: clock fb0000 2 of 143360 (103), fb1a40 2 (103), fb2300 0; config fb08c0 0,
fb1a40 3 (52), fb2300 0; to-clock fb0000 0, fb1a40 2 (28), fb2300 0. The texture cache is the only
nonzero cause: a model of it (a zero-filled buffer updated per draw over the draw's texture
rectangle) brought every group on all three captures to 0 and was not kept. The gate is
`tools/ParityTool/budgets.json`, run as the CTest `ParityClock`; a group missing from the file, a
group or chained target over its budget, or a validation error fails. A difference with no cause
in the table fails.

## Scene

`scene/` ports the verified logic with names, not addresses: clock state (current rod, eased
angles, progress `t`, colour walk), camera and screen matrix, rod and orb placement, orb ring
feeding and fade ramp, scale easing, transition schedule from T0, menu and configuration state,
text layout from the language table. Each function's comment names the facts page it implements.

EE float behaviour (round toward zero, no denormals, `cvt.w.s` truncation) is a small `EeFloat`
helper used only where `facts/` requires it, with the EE's own `sinf` / `cosf` and the sine table
(`References/model/ee_libm.mjs`, `clock_math.mjs`), which the exact geometry test needs. In
`Native` mode it becomes plain `float` and the standard library.

## Data flow per frame

1. `app/` reads real time and input.
2. `scene/` steps the state and builds the `FrameDescription`.
3. `renderer/` records the passes in order (`GsParity` or `Native`).
4. The display target is presented.

## Testing

| Level | Compares | Against | Criterion | Runs in |
|---|---|---|---|---|
| Logic | `scene/` state per frame | State fixtures exported from captures by the JS model | exact | unit tests |
| Geometry | `FrameDescription` → `parity/` → GS values | Packets of `whole2` / `whole3` captures | exact | unit tests |
| Pixels | Headless `GsParity` render of one frame, every target | The oracle: `pcsx2-gsrunner -dump rt,z,a,i` per draw, decoded by `tools/parity/make_fixture.mjs` | the measured budgets | parity tool, CTest `ParityClock` |
| Native | Headless `Native` render | Same frames | report only, no gate | parity tool |

The parity report gives, per pass and per target: pixels that differ, maximum difference per
channel, and the cause from the renderer table. A difference with no known cause fails. Each slice
ends with a side-by-side image (ours, gsrunner, difference map) written to disk.

Debug builds run with validation layers; every pass carries its name as a debug label.

## Slices

| Slice | Content | Done when |
|---|---|---|
| 0. Spike | The oracle: per-draw buffers out of the software renderer for one frame of a `.gs` (`pcsx2-gsrunner -dump rt,z,a,i`, decoded by `tools/parity/make_fixture.mjs`). Then, against it: edges of triangles and sprites, shader blend with both ordering methods, depth, one bilinear textured quad, the `AA1` line | Every row of the renderer table has a measured difference; the ordering method is chosen |
| 1. One still frame | The clock frame of one `whole2` capture: background, rods (refracted, textured, reflection, extra passes), orbs (trail, sprites), blur, tint, vignette. The `FrameDescription` comes from the `parity/` decoder | Every target within the budgets; side-by-side image; the budgets written into this document |
| 2. Live clock | `scene/` runs the logic from real time; a captured sequence compared frame by frame | Logic and geometry exact; pixels within tolerance |
| 3. Screens around | Main menu, System Configuration with cubes, Clock Adjustment, transitions, text | Same criteria on `whole2` / `whole3` captures of those screens |

Slices 0 and 1 decide whether the approach is convincing. If not, the renderer section is revisited
before slice 2.

## Out of scope

MoltenVK and macOS; the `Native` mode beyond a toggle; upscaling; CRT filter; the opening (VU1
towers, illegal-disc scene); PAL; languages other than English; ROM 2.30 as a target.

## Risks

Answered by slices 0 and 1: ordering inside a draw (storage images in the pixel interlock,
`coherent`); `AA1` coverage (implemented, exact); texel-boundary and depth-flip differences
(rows computed per pixel with the software renderer's arithmetic); reading the oracle
(`pcsx2-gsrunner`).

Still open:
- The oracle is PCSX2's software renderer, not hardware; where it models hardware by its own
  admission (the STQ rounding), parity is with the model.
- Cost: the CPU walk makes about 42 MB of row data per frame and 160-byte vertices (the parity tool
  takes about 3 s per frame in Debug). Move the spans to a storage buffer before `GsParity` runs
  live.
- Refused by the fixture maker, so not covered: `ABE` with `AA1`, paletted textures (26 to 125
  passes per capture, taken from the oracle), `PABE`, `FBA`, frame masks.
- The STQ rule takes "every vertex has the same Z" as PCSX2's `m_eq.z`; PCSX2 derives it from a
  float min and max, which could differ for large Z values not met in these captures.
- The refraction reads a work buffer that earlier passes drew, so a difference there is carried
  into the display; the report compares every target, so the first target that differs is known.
