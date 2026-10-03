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

| GS feature (from `facts/`) | Vulkan, `GsParity` mode | Expected parity |
|---|---|---|
| Display and work buffers (by FBP) | One `RGBA8_UINT` image per buffer at native size; the display has two, alternating | exact |
| 12.4 coordinates with the screen offset | Vertex shader snaps to 1/16 pixel and shifts half a pixel (the GS samples at integer coordinates, Vulkan at pixel centres); the driver has 8 subpixel bits | exact at edges (spike confirms, sprites included) |
| Depth, `GREATER` / `GEQUAL` (`ZBUF_1` `0x8C`: 32-bit format, values up to 16777215 by the screen matrix) | `D32_SFLOAT` holding z / 2^24; hardware compare | exact at vertices; interior values are interpolated by the GPU, so a test can flip where two surfaces nearly coincide (measured) |
| Blend `((A − B) · C >> 7) + D`, alpha 0x80 = 1, `FIX`, `COLCLAMP` 1 | Fragment shader in integers on the destination read back (see "Ordering inside a draw") | exact |
| `DTHE` 0 | No dithering | exact |
| Texture address modes, `TEX1` `0x61` bilinear | `texelFetch` with integer addressing; GS modes and bilinear weights written in the shader | exact for equal coordinates; see interpolation |
| Gouraud colour, ST/Q | `noperspective` varyings (GS vertices are already on the screen); S/Q and T/Q divided per fragment | colour ±1 on gradients; a coordinate that lands across a texel boundary picks another texel, so textured passes can differ by more than 1 on isolated pixels (measured) |
| Line strip with `AA1` (orb trail) | Thin quad per segment in the shader | risk: measured in slice 0 |
| Text fans and sprites with CLUT | Triangles; CLUT resolved at texture load | exact texel, ±1 on gradients |

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

Slice 0 measures both on a capture's worst pass.

### Tolerance

The tolerance is a budget per cause, not one number: for each cause in the table, the largest
difference per channel and the share of pixels allowed to differ. Slice 0 and slice 1 measure
them; the measured budgets are then written here and become the gate. A difference outside every
budget, or with no cause in the table, fails.

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
| Pixels | Headless `GsParity` render of one frame, every target | GS local memory after the software renderer replays the same frame's `.gs` | the measured budgets | parity tool |
| Native | Headless `Native` render | Same frames | report only, no gate | parity tool |

The parity report gives, per pass and per target: pixels that differ, maximum difference per
channel, and the cause from the renderer table. A difference with no known cause fails. Each slice
ends with a side-by-side image (ours, gsrunner, difference map) written to disk.

Debug builds run with validation layers; every pass carries its name as a debug label.

## Slices

| Slice | Content | Done when |
|---|---|---|
| 0. Spike | The oracle: GS local memory out of the software renderer for one frame of a `.gs` (through `pcsx2-gsrunner`, or Watson with the software renderer and `watson_gs_read` / `extract_buffers.mjs`). Then, against it: edges of triangles and sprites, shader blend with both ordering methods, depth, one bilinear textured quad, the `AA1` line | Every row of the renderer table has a measured difference; the ordering method is chosen |
| 1. One still frame | The clock frame of one `whole2` capture: background, rods (refracted, textured, reflection, extra passes), orbs (trail, sprites), blur, tint, vignette. The `FrameDescription` comes from the `parity/` decoder | Every target within the budgets; side-by-side image; the budgets written into this document |
| 2. Live clock | `scene/` runs the logic from real time; a captured sequence compared frame by frame | Logic and geometry exact; pixels within tolerance |
| 3. Screens around | Main menu, System Configuration with cubes, Clock Adjustment, transitions, text | Same criteria on `whole2` / `whole3` captures of those screens |

Slices 0 and 1 decide whether the approach is convincing. If not, the renderer section is revisited
before slice 2.

## Out of scope

MoltenVK and macOS; the `Native` mode beyond a toggle; upscaling; CRT filter; the opening (VU1
towers, illegal-disc scene); PAL; languages other than English; ROM 2.30 as a target.

## Risks

- Ordering inside a draw (above). Fallback: split draws.
- `AA1` line coverage. Fallback: declare it in the tolerance with its measured size.
- Texel-boundary and depth-flip differences that are visible, not just countable. Fallback:
  compute the interpolation of the affected pass in the fragment shader from the primitive's
  vertices, with the software renderer's arithmetic, and record the cost.
- The refraction reads a work buffer that earlier passes drew, so a difference there is carried
  into the display. The report compares every target, so the first target that differs is known.
- Reading GS local memory out of the oracle. Fallback order: `pcsx2-gsrunner`, then Watson with
  the software renderer (to confirm: which renderer Watson runs, and that `watson_gs_read`
  returns the software renderer's memory).
