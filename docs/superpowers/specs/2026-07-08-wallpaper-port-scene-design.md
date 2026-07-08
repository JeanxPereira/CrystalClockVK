# Design: Wallpaper-Engine "PS2 Clock" Port to the VK Core

> Status: APPROVED (brainstorming gate, 2026-07-08). Supersedes the GS/EE 1:1
> reverse-engineering track as the active direction. The RE work is archived,
> not deleted from history.

## 1. Motivation & Doctrine Change

The GS/EE decomp track (byte-perfect OSDSYS replication via a GS/EE interpreter)
stalled on the projection/position-writer bug and repeated dead ends. A public
Wallpaper Engine scene ("Playstation 2 Clock", workshop id `1979606285`) is a
**clean, fully readable** recreation of the same visual: all scene logic is in
plain JavaScript and all shaders are readable GLSL. Source of truth for this
track is that unpacked scene at
`C:\Users\dell04\Downloads\1979606285\scene-repkg\`.

**New doctrine:** faithfully port that scene into our existing Vulkan core, with
freedom to diverge where it improves the result. We keep the generic VK wrapper,
drop the PS2-emulation machinery, and never guess the look — we read it from the
scene's JS/GLSL instead of from GS dumps.

Divergences intentionally built in from day one:
- The crystal/prism model is generated **procedurally in code**, not loaded from
  the scene's `.mdl` files.
- The clock rotation rule is **pluggable**; the WE semantics are the default, but
  we can swap in our own rule without touching the renderer.

## 2. Scope

Full scene in a single implementation effort (no staged milestones):
prisms + refraction, orbs + trails + glow, cloud tunnel background, background
particles, post FX (film grain + blur + optional downsample), date/time text,
and the layered white-noise audio.

Non-goals: the Wallpaper Engine editor property system, LED source output, camera
shake/parallax (disabled in the scene), and the HLSL shader path.

## 3. Source-Scene Facts (read from the scene, not guessed)

These are the numeric/behavioral facts the port must reproduce (default values;
all overridable in `SceneParams`).

### Camera (`Camera (script)`)
- Eye starts at `(0,0,250)`, eases toward a FOV-dependent target
  (`(0,0,38)` at 53° FOV) with accel-limited velocity (accel `100`, stop within
  `0.1`). `up = (0,1,0)`, center `(0,0,0)`.
- FOV 53° default; near 1, far 10000.
- `clearcolor = (0.125, 0.098, 0.204)`, `ambientcolor = (0.302,…)`,
  `skylightcolor = (1,1,1)`.

### Prisms (`Prisms (script)` + `crystal.vert/frag`)
- `numPrisms = 12`, ring radius `prismDist = 6.5`, `prismScale = 0.9`.
- Ring layout: prism `i` at Euler `(0, 0, i*360/12)` then translate `+6.5` in Y
  (i.e. evenly spaced around the Z axis).
- Parent transform per frame (CLOCK mode, WE default rule):
  - `yaw  = -(seconds + ms/1000) * 6`  → one full turn per minute.
  - `roll = -(hours % 12) * 30`.
  - Built as Euler `(0, yaw, roll)` order `ZYX`, re-expressed as `XYZ` Euler.
- Each prism additionally spins on its local axis at `(0, 20, 0)` deg/s (`ZYX`).
- Two draw passes share the `crystal` shader (both **additive**, depth
  test+write, `cullmode normal`):
  - `prism` (inner): `Light = -1`.
  - `prism_main`: `Light = (minutes + seconds/60)/60 * 10` (glow fills over the
    hour).
  - `start_color = (0.478, 0.333, 0.780)`, `end_color = (0.298, 0.780, 0.757)`,
    tint = `mix(start,end, tri(g_Time/20s))` (20 s to cycle and back).
  - Textures: slot0 `util/clouds_256`, slot2 `_rt_FullFrameBuffer`, slot3
    `_rt_Reflection`.
- `crystal.frag` behavior to reproduce: rim light `1 - max(0,dot(view,normal))`;
  height-based emissive `smoothstep(h*0.95, h*0.95+0.5, Light)*0.5` plus rim plus
  `step(0,Light)*0.25`; refraction `refract(view,normal,0.5).xy / screenPos.z`
  sampling the framebuffer copy, scaled by `2*(0.75 + emissive*4)`; a fake
  "reflection" from the cloud texture; final `mix(refract, diffuse, diffuse.r*0.2)`
  tinted, plus reflection, times fade-alpha.
- `crystal.vert`: `v_Height = a_Position.y` (crystal must be modeled along +Y);
  `v_ScreenPos = clip.xyw`; `v_ViewDir = eye - worldPos`; ambient =
  `mix(skylight, ambient, dot(n, up)*0.5+0.5)`.

### Orbs (`Orbs (script)`, `particles/orb.json` + trail/glow)
- `numOrbs = 7`, `orbDist = 4.5`, parent rotates `(0,110,90)` deg/s; each orb's
  Z angle scales with `(index+1)`. Default color `(0.64, 0.94, 1.0)`. Trail resets
  after a >0.25 s pause to avoid jagged trails.

### Cloud tunnel (`Clouds` → `ps2menu.frag`)
- Cylindrical tunnel from 4 weighted noise samples of `util/noise`
  (`(scale,weight,speed)` = `(0.25,4,0.035)`,`(0.5,2,0.04)`,`(1.5,1,0.0435)`,
  `(5.5,1,0.0465)`), `TIMESCALE 0.4`, `ZOOM 0.65`, brightness `0.7…1.0`,
  `col = bg * noise * min(1, .1+.9r) * 0.6`, additive. `bg ≈ (0.68,0.54,1.0)`.

### Post (`Post Processing`)
- Film grain (`util/noise`, strength `~0.25–0.38`, scale 10, exponent 0.5), then
  optional downsample 2x/4x by `resolution_scale`. Text layers also carry a
  precise gaussian blur effect.

### Text (`Current Date`, `Current Time`)
- Font `NimbusSanL-Reg.ttf`. Date `YYYY/MM/DD` top-left, time `H:MM:SS AM/PM`
  (or 24h) top-right. Positions lerp with FOV. Hidden by default
  (`show_clock_text = false`), alpha follows `fade` capped at 0.9.

### Audio (`Sounds (script)`, `startup.ogg`)
- 5 white-noise layers, each with randomized delay/duration and a smoothstep
  volume envelope; volumes ~0.03–0.30. One-shot `startup.ogg` at `~0.4` volume.

## 4. Architecture

Strict separation preserved from the current project: logic modules stay
Vulkan-free and unit-testable; only the render/app layer touches VK.

### Keep (reusable base — unchanged)
- `core/`: `VulkanContext`, `WindowContext`, `RenderDocWrapper`.
- `renderer/`: `PipelineBuilder` (with `setBlendState`), `PassRecorder`,
  `ResourceManager`, `SwapchainManager`, `DescriptorAllocator`, `ShaderLoader`,
  `UIRenderer`, `FrameData`, `DeletionQueue`.

### Remove (after `git tag archive/gs-ee-re`)
- `src/gs/`, `src/ee/`, `src/gsvk/`, `src/clock/`.
- `src/app/GsScene.*`, `src/app/GsRenderer.*` and GS-specific shaders
  (`gsclock.*`, `rod_flat.*`). `RenderOrchestrator`/`TimeSync` are rewritten.

### New modules

**`scene/` — pure logic, no VK symbols (mirrors the WE scripts):**
- `SceneClock` — wall-clock time → parent Euler angles + `Light` glow value.
  Holds a `RotationRule` strategy; `WeDefaultRule` reproduces the WE semantics,
  and the interface lets us swap in a custom rule.
- `PrismField` — computes the 12 prism world transforms each frame (ring layout →
  parent transform → per-prism local spin). Emits an array of model matrices +
  per-prism `Light`/alpha.
- `OrbField` — 7 orb positions + trail state.
- `SceneCamera` — accel-limited eye easing, FOV → view/projection matrices.
- `SceneParams` — colors, tint period, fade, mode (CLOCK/ORBS), options.

**`mesh/` — procedural geometry:**
- `PrismMesh` — generates a faceted, elongated crystal along +Y (n-gon cross
  section with pointed tips) producing interleaved position/normal/uv + indices.
  Replaces the `.mdl` assets; parameterized so the facet count/profile is tunable.
- `Quad` — fullscreen and tunnel quads.

**`assets/` — resource loading:**
- `TextureLoader` — decode `clouds_256`, `orb`, `trail`, `noise` PNGs (stb_image)
  → `VkImage` via `ResourceManager`.
- `FontAtlas` — bake `NimbusSanL-Reg.ttf` (stb_truetype / ImGui) for the text
  overlay.

**`audio/`:**
- `NoiseMixer` — 5 layered noise voices + envelope logic + one-shot startup, on
  SDL3's audio device.

**`app/` — rewritten:**
- `RenderOrchestrator` — owns the offscreen targets, the pass order, and the
  per-frame descriptor wiring. Drives `scene/` for state, `mesh/`/`assets/` for
  resources, and records the passes below.
- `TimeSync` — provides frame time + wall-clock, replacing the GS variant.

## 5. Render Graph (per frame)

All rendering targets an offscreen HDR color image (+ depth); post resolves to the
swapchain.

1. **Cloud tunnel** — fullscreen `ps2menu` over `clearcolor` → HDR color.
2. **Framebuffer copy** — copy/blit HDR color into a sampled "scene" image. This
   feeds both `_rt_FullFrameBuffer` and `_rt_Reflection` in the crystal shader
   (the scene mirrors them; a single copy is sufficient for our port).
3. **Prisms** — for each of the 12 prisms, two additive draws (inner `Light=-1`,
   main `Light=minute`) with depth test+write, sampling the copy + clouds.
4. **Orbs + trails + glow** — additive particle draws (ORBS/CLOCK-dependent).
5. **Background particles** — additive dust.
6. **Text overlay** — date/time from `FontAtlas`, blurred per the scene.
7. **Post** — film grain + gaussian blur (+ optional downsample) → swapchain.

Refraction approach (chosen over true planar reflection): the step-2 framebuffer
copy is the cheapest faithful reproduction of the WE trick, which itself samples
the frame's own render targets. Trade-off: refraction only "sees" what was drawn
before the prisms (tunnel + particles), exactly as in the source.

## 6. Shaders (GLSL → Vulkan GLSL)

Port `crystal.vert/frag`, `ps2menu.vert/frag`, particle, `filmgrain`, and
`blur_precise_gaussian`. Replace Wallpaper Engine macros:
- `mul(v, M)` uses **row-vector** convention (v·M). GLM is column-major, so
  either transpose on upload or reorder to `M * v`. Pick one convention project-
  wide and document it in the shader header.
- `texSample2D` → `texture`; `CAST2/CAST3/CAST3X3` → GLSL constructors/`mat3`;
  `DecompressNormal` → inline (normal maps are unused in this scene — the
  `NORMALMAP` combo is off, so `v_Normal` path only).
- Uniforms become UBO/push-constants; `varying` → `in/out`; `gl_FragColor` → an
  `out vec4`. Bindings managed by `DescriptorAllocator`.

## 7. Testing

- `scene/` and `mesh/` are pure and unit-tested: prism ring positions, parent
  rotation at known timestamps (WE default rule), camera easing convergence,
  `Light` value vs. minute, `PrismMesh` manifold/normal sanity.
- Renderer validated by eye against `preview.gif` and by RenderDoc capture of the
  pass order (no numeric pixel-diff against PCSX2 — that reference belonged to the
  retired GS track).

## 8. Open Decisions (resolved defaults)

- **Clock rotation rule:** default = WE semantics (yaw `-sec*6`, roll
  `-(hour%12)*30`, 1 turn/min), implemented behind `RotationRule` so a custom
  rule can replace it later. (Confirmed at the gate.)
- **Assets:** reuse the scene's extracted bitmaps (`clouds_256`, `orb`, `trail`,
  `noise`) and `NimbusSanL-Reg.ttf`; geometry is procedural.
- **Old code:** archived under tag `archive/gs-ee-re`, then `gs/ee/gsvk/clock`
  removed from the active branch.
