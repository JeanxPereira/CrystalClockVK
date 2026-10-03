# Native Renderer, Slices 0 and 1 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render one captured frame of the PS2 OSD clock with a Vulkan renderer that follows the GS rules, compare it draw by draw with PCSX2's software renderer, and write the measured tolerance into the spec.

**Architecture:** A Node tool runs `pcsx2-gsrunner` (software renderer) on a GS dump and keeps, per draw, the frame and depth buffers before and after. A second Node tool turns the same dump into a fixture: a `FrameDescription` in native units plus textures and start buffers. A headless C++ tool loads the fixture, renders each pass with `GsParityRenderer` (targets as storage images, blending in the fragment shader inside an ordered pixel interlock) and reports the difference per draw.

**Tech Stack:** C++23, Vulkan 1.4, vk-bootstrap, VMA, `VK_EXT_fragment_shader_interlock`, nlohmann/json, stb_image, CMake/CTest; Node 25 (`node --test`) for the tools; `pcsx2-gsrunner` v2.9.94 built by `Watson/Emulator/Build.ps1`.

**Spec:** `docs/superpowers/specs/2026-10-03-native-vulkan-renderer-design.md`

## Global Constraints

- English only in code, file names and documents. No comments except where GS arithmetic needs one.
- Commit messages: `Type(Scope): Short imperative description`, at most 72 characters, no attribution lines of any kind.
- Never `git push`. Never `git add` a directory: name the files (a hook refuses otherwise).
- Do not touch `src/main.cpp`, `src/renderer/UIRenderer.cpp`, `.mcp.json`, the SDL tag line of `CMakeLists.txt`, `src/app/`, `src/gs/` or the old shaders: the old application keeps building untouched in this plan.
- Platform: Windows, AMD RX 6750 XT, Vulkan 1.4. No MoltenVK code paths.
- Canon build HDD OSD 1.10U, NTSC. Capture used throughout: `D:/CodingProjects/Watson/Runtime/captures/hddosd-110U-whole3-clock.gs`, frame 0 (188 draws, buffers 640 × 224).
- Generated data goes under `References/fixtures/` (ignored by git through `References/.gitignore`).
- GS rules are read from `facts/` and from the software renderer at `D:/CodingProjects/Watson/References/pcsx2/pcsx2/GS/Renderers/SW/` (`GSRendererSW.cpp`, `GSRasterizer.cpp`, `GSDrawScanline.cpp`). Read it for the rule; write our own code.
- A difference with no known cause is a failure, never a tolerance.

## Rulings made while planning

- The spec's `parity/` decoder is two Node tools (`tools/parity/`) plus a C++ loader (`src/parity/`): Watson already parses dumps, and the application never decodes packets.
- The oracle is `pcsx2-gsrunner -dump rt,z,a,i` (verified: it writes buffers before and after each draw). Watson's live `watson_gs_read` is not needed.
- `FrameDescription` in these slices holds primitive lists only (`Triangles`, `Sprites`, `Lines`), colours as 0–255 floats and depth as an integer, so the parity renderer recovers the integers exactly. Strips, fans and normalized colours are the scene's concern in slice 2.
- `Blend` is the general equation `((a − b) × c) + d` with named terms, not the spec's five named operations; the names are derived for reports.
- Draws the renderer does not support (paletted text in this frame) carry a `skip` reason; the tool takes the oracle's result for them and counts them.
- The barrier fallback for ordering is built only if the ordering test of Task 6 fails.
- `AA1` (edge antialiasing) is on the orb trails and on the rods' refracted faces. Task 9 measures it with edges ignored and decides.

## Review Focus

- A fixture whose primitives do not line up with the oracle's draws: the maker must fail, never emit a shifted frame.
- A texture that is also the pass's own target: the renderer must refuse.
- An oracle directory from an earlier run of another frame: the oracle tool must clear its output first.
- A pass with an unsupported state bit that is not flagged `skip`: the maker must list every bit it checks and fail on values it does not know.
- A `gsrunner` that never produces files (missing DLL path): the tool must time out with the reason, not hang.

## File Structure

| File | Responsibility |
|---|---|
| `tools/parity/oracle.mjs` | Run gsrunner on one frame of a dump; per-draw buffers and state |
| `tools/parity/make_fixture.mjs` | Dump + oracle → `frame.json`, textures, start buffers |
| `tools/parity/side_by_side.mjs` | Report + raw images → PNG panels (ours, oracle, difference) |
| `tools/parity/*.test.mjs` | `node --test` for the two tools |
| `src/scene/FrameDescription.hpp` | The native frame structures |
| `src/core/GpuDevice.hpp` | Handles a renderer needs, with or without a window |
| `src/core/HeadlessContext.{hpp,cpp}` | Instance and device without a surface, interlock enabled |
| `src/core/VmaUsage.cpp` | The VMA implementation for the new library |
| `src/parity/Fixture.{hpp,cpp}`, `src/parity/StbImage.cpp` | Load a fixture and oracle PNG pairs |
| `src/parity/Compare.{hpp,cpp}` | Differences between images |
| `src/renderer/GsParityRenderer.{hpp,cpp}` | Targets, textures, one draw per pass |
| `shaders/GsParity.vert`, `shaders/GsParity.frag` | GS rules in integers |
| `tools/ParityTool/main.cpp` | Fixture → per-draw and chained report |
| `tests/Check.hpp`, `tests/*.cpp` | CTest executables |

---

### Task 1: Oracle tool

**Files:**
- Create: `tools/parity/oracle.mjs`
- Test: `tools/parity/oracle.test.mjs`

**Interfaces:**
- Produces: `runOracle(dump: string, outDir: string, frame = 0): Promise<{ draws: number, files: number }>`. In `outDir`, per draw `NNNNN` (from 00001): `NNNNN_context.txt`, `NNNNN_vertex.txt`, `NNNNN_f<frame>_rt0_<block>_C_32.png` (+ `_alpha.png`), `..._rt1_...`, `..._rz0_<block>_Z_32.png` (+ `_alpha.png`), `..._rz1_...`.

- [ ] **Step 1: Write the failing test**

```js
// tools/parity/oracle.test.mjs
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { runOracle } from './oracle.mjs';

const DUMP = 'D:/CodingProjects/Watson/Runtime/captures/hddosd-110U-whole3-clock.gs';

test('one clock frame gives 188 draws, and the same bytes twice', async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'oracle-'));
  const a = await runOracle(DUMP, path.join(root, 'a'), 0);
  const b = await runOracle(DUMP, path.join(root, 'b'), 0);
  assert.equal(a.draws, 188);
  assert.equal(b.draws, 188);
  assert.equal(a.files, b.files);
  for (const name of fs.readdirSync(path.join(root, 'a'))) {
    assert.ok(fs.readFileSync(path.join(root, 'a', name)).equals(fs.readFileSync(path.join(root, 'b', name))), name);
  }
});

test('a missing dump is refused with its path', async () => {
  await assert.rejects(runOracle('D:/nowhere/none.gs', path.join(os.tmpdir(), 'oracle-none'), 0), /no dump at D:\/nowhere\/none\.gs/);
});
```

- [ ] **Step 2: Run it to see it fail**

Run: `node --test tools/parity/oracle.test.mjs`
Expected: FAIL, `Cannot find module ... oracle.mjs`

- [ ] **Step 3: Write the tool**

```js
// tools/parity/oracle.mjs
// Per-draw oracle of one frame of a GS dump: PCSX2's software renderer, through pcsx2-gsrunner,
// writes the frame and depth buffers before and after every draw, with the draw's state.
//   node tools/parity/oracle.mjs <dump.gs> <out dir> [frame]
import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const WATSON = (process.env.WATSON_ROOT ?? 'D:/CodingProjects/Watson').replace(/\\/g, '/');
const TREE = `${WATSON}/References/pcsx2`;
const RUNNER = `${TREE}/build/pcsx2-gsrunner/Release/pcsx2-gsrunner.exe`;
const POLL_MS = 500;
const QUIET_POLLS = 4;
const TIMEOUT_MS = 120000;

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

export async function runOracle(dump, outDir, frame = 0) {
  if (!fs.existsSync(RUNNER)) throw new Error(`no gsrunner at ${RUNNER}: run Watson/Emulator/Build.ps1`);
  if (!fs.existsSync(dump)) throw new Error(`no dump at ${dump}`);
  const frames = `${outDir}.frames`;
  fs.rmSync(outDir, { recursive: true, force: true });
  fs.rmSync(frames, { recursive: true, force: true });
  fs.mkdirSync(outDir, { recursive: true });
  fs.mkdirSync(frames, { recursive: true });

  // gsrunner dumps draws only when -dumpdir is given too, and does not exit by itself.
  const args = ['-renderer', 'sw', '-swthreads', '0', '-surfaceless', '-noshadercache',
    '-dumpdir', frames, '-dump', 'rt,z,a,i', '-dumprangef', `${frame},1`, '-dumpdirsw', outDir,
    '-logfile', `${outDir}.log`, dump];
  const env = { ...process.env, PATH: `${TREE}/deps/bin;${TREE}/bin;${process.env.PATH}`.replace(/\//g, '\\') };
  const child = spawn(RUNNER, args, { cwd: path.dirname(RUNNER), env, stdio: 'ignore' });
  let exited = false;
  child.on('exit', () => { exited = true; });

  let last = -1, quiet = 0;
  const started = Date.now();
  try {
    for (;;) {
      await sleep(POLL_MS);
      const count = fs.readdirSync(outDir).length;
      quiet = count > 0 && count === last ? quiet + 1 : 0;
      last = count;
      if (quiet >= QUIET_POLLS) break;
      if (exited && count === 0) throw new Error(`gsrunner exited without writing a draw; see ${outDir}.log`);
      if (Date.now() - started > TIMEOUT_MS) throw new Error(`gsrunner wrote ${count} files in ${TIMEOUT_MS / 1000} s and did not settle; see ${outDir}.log`);
    }
  } finally {
    if (!exited) child.kill();
    fs.rmSync(frames, { recursive: true, force: true });
  }

  const names = fs.readdirSync(outDir);
  const draws = names.filter((name) => name.endsWith('_context.txt')).map((name) => name.slice(0, 5)).sort();
  for (const draw of draws) {
    for (const part of ['_vertex.txt', '_rt1_', '_rz1_']) {
      if (!names.some((name) => name.startsWith(draw) && name.includes(part))) throw new Error(`draw ${draw} has no ${part} file in ${outDir}`);
    }
  }
  return { draws: draws.length, files: names.length };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const [dump, outDir, frame] = process.argv.slice(2);
  if (!dump || !outDir) { console.error('usage: node tools/parity/oracle.mjs <dump.gs> <out dir> [frame]'); process.exit(2); }
  const done = await runOracle(dump, outDir, Number(frame ?? 0));
  console.log(`oracle: ${done.draws} draws, ${done.files} files in ${outDir}`);
}
```

- [ ] **Step 4: Run the test**

Run: `node --test tools/parity/oracle.test.mjs`
Expected: PASS, 2 tests. (About 20 s.)

- [ ] **Step 5: Commit**

```bash
git add tools/parity/oracle.mjs tools/parity/oracle.test.mjs
git commit -m "Feat(Project): Add per-draw oracle from the software renderer"
```

---

### Task 2: Fixture maker

**Files:**
- Create: `tools/parity/make_fixture.mjs`
- Test: `tools/parity/make_fixture.test.mjs`

**Interfaces:**
- Consumes: `runOracle` (Task 1); Watson's `parseGsDump(file, out, options)` and `decodeRegister(name, value)` from `D:/CodingProjects/Watson/Server/dist/gs/`; `word32(bp, bw, x, y)` from `References/scripts/extract_buffers.mjs`.
- Produces: `makeFixture(dump: string, outDir: string, frame = 0): Promise<{ passes: number, skipped: number, dropped: number }>` and, in `outDir`: `oracle/` (Task 1), `start/<target>.rgba`, `textures/<id>.rgba`, `frame.json`:

```
{ capture, frame, field,
  targets:  [{ id: "fb0000", width, height, start: "start/fb0000.rgba" }],
  depthStart: "oracle/00001_f00000_rz0_01180_Z_32",
  textures: [{ id, width, height, file }],
  passes:   [{ index, name, target, primitive: "Triangles"|"Sprites"|"Lines",
               scissor: [x0, y0, x1, y1],
               blend: null | { a, b, c, d, fixed },          a,b,d: "Source"|"Destination"|"Zero"; c: "SourceAlpha"|"DestinationAlpha"|"Fixed"
               antialias: bool,
               depth: { test: "Never"|"Always"|"GreaterEqual"|"Greater", write: bool },
               texture: null | { source: { target } | { image }, width, height,
                                 coordinates: "Texel"|"Projective",
                                 addressU: { mode, min, max }, addressV: { mode, min, max },   mode: "Repeat"|"Clamp"|"RegionClamp"|"RegionRepeat"
                                 filter: "Nearest"|"Bilinear",
                                 alpha: { mode: "Texel" } | { mode: "Constant", value, zeroWhenBlack } },
               skip: null | "reason",
               vertices: [[x, y, depth, r, g, b, a, s, t, q]],
               oracle: { colour: "oracle/..._rt1_..._C_32", depth: "oracle/..._rz1_..._Z_32" } }] }
```

- [ ] **Step 1: Write the failing test**

```js
// tools/parity/make_fixture.test.mjs
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { makeFixture } from './make_fixture.mjs';

const DUMP = 'D:/CodingProjects/Watson/Runtime/captures/hddosd-110U-whole3-clock.gs';

test('the clock frame becomes 188 passes lined up with the oracle', async () => {
  const out = path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'fixture-')), 'f0');
  const done = await makeFixture(DUMP, out, 0);
  const frame = JSON.parse(fs.readFileSync(path.join(out, 'frame.json'), 'utf8'));

  assert.equal(done.passes, 188);
  assert.equal(frame.passes.length, 188);
  assert.deepEqual(frame.targets.map((t) => t.id).sort(), ['fb0000', 'fb1a40', 'fb2300']);
  for (const target of frame.targets) {
    assert.equal(target.width, 640);
    assert.equal(target.height, 224);
    assert.equal(fs.statSync(path.join(out, target.start)).size, 640 * 224 * 4);
  }

  const skipped = frame.passes.filter((p) => p.skip);
  assert.equal(skipped.length, 26);
  assert.ok(skipped.every((p) => p.skip === 'texture format 0x14'), 'only the paletted text is skipped');

  const first = frame.passes[0];
  assert.equal(first.index, 1);
  assert.equal(first.primitive, 'Sprites');
  assert.equal(first.texture, null);
  assert.equal(first.vertices.length, 2);

  const copy = frame.passes[2];
  assert.equal(copy.target, 'fb1a40');
  assert.deepEqual(copy.texture.source, { target: 'fb0000' });
  assert.deepEqual(copy.texture.alpha, { mode: 'Constant', value: 127, zeroWhenBlack: true });
  assert.equal(copy.texture.addressU.mode, 'RegionClamp');
  assert.equal(copy.texture.addressU.max, 639);
  assert.equal(copy.texture.filter, 'Bilinear');
  assert.equal(copy.depth.test, 'Always');
  assert.deepEqual(copy.vertices[1].slice(0, 2), [640, 224]);

  const lines = frame.passes.filter((p) => p.primitive === 'Lines');
  assert.equal(lines.length, 14);
  assert.ok(lines.every((p) => p.antialias && p.blend === null && p.depth.test === 'Greater'));

  for (const pass of frame.passes) {
    assert.ok(fs.existsSync(path.join(out, `${pass.oracle.colour}.png`)), pass.oracle.colour);
    assert.ok(pass.oracle.colour.includes(`_${pass.target.slice(2).padStart(5, '0')}_`), `pass ${pass.index} target and oracle file agree`);
    const size = { Triangles: 3, Sprites: 2, Lines: 2 }[pass.primitive];
    assert.equal(pass.vertices.length % size, 0);
    assert.ok(pass.vertices.every((v) => v.length === 10 && v.every(Number.isFinite)));
  }
  for (const texture of frame.textures) assert.equal(fs.statSync(path.join(out, texture.file)).size, texture.width * texture.height * 4);
});
```

- [ ] **Step 2: Run it to see it fail**

Run: `node --test tools/parity/make_fixture.test.mjs`
Expected: FAIL, `Cannot find module ... make_fixture.mjs`

- [ ] **Step 3: Write the maker**

```js
// tools/parity/make_fixture.mjs
// One frame of a GS dump as a fixture for the native renderer: the frame in native units
// (frame.json), the textures and start buffers it reads, and the oracle's result per draw.
//   node tools/parity/make_fixture.mjs <dump.gs> <out dir> [frame]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { runOracle } from './oracle.mjs';
import { word32 } from '../../References/scripts/extract_buffers.mjs';

const DIST = (process.env.WATSON_DIST ?? 'D:/CodingProjects/Watson/Server/dist').replace(/\\/g, '/').replace(/\/$/, '');
const { parseGsDump } = await import(`file:///${DIST}/gs/parse.js`);
const { decodeRegister } = await import(`file:///${DIST}/gs/registers.js`);

const TERM = ['Source', 'Destination', 'Zero'];
const FACTOR = ['SourceAlpha', 'DestinationAlpha', 'Fixed'];
const ZTEST = ['Never', 'Always', 'GreaterEqual', 'Greater'];
const WRAP = ['Repeat', 'Clamp', 'RegionClamp', 'RegionRepeat'];
const SIZE = { point: 1, line: 2, linestrip: 2, triangle: 3, tristrip: 3, trifan: 3, sprite: 2 };
const ORACLE_KIND = { TRIANGLE: ['Triangles', 3], SPRITE: ['Sprites', 2], LINE: ['Lines', 2] };
const VRAM_BYTES = 0x400000;
const STATE_TAIL = (16 + 4) * 4 + 4;

const hex = (n, width) => n.toString(16).padStart(width, '0');

function fields(state, name) {
  if (!(name in state)) throw new Error(`the draw state has no ${name}`);
  const decoded = decodeRegister(name, BigInt(state[name]));
  return new Proxy(decoded, { get(target, key) {
    if (typeof key === 'string' && !(key in target)) throw new Error(`${name} has no field ${key}; it has ${Object.keys(target).join(', ')}`);
    return target[key];
  } });
}

/** The native description of a draw's state, and why it cannot be drawn if it cannot. */
export function describeState(state, targetOfBlock) {
  const prim = fields(state, 'PRIM'), frame = fields(state, 'FRAME'), zbuf = fields(state, 'ZBUF');
  const test = fields(state, 'TEST'), alpha = fields(state, 'ALPHA'), scissor = fields(state, 'SCISSOR');
  const reasons = [];
  if (frame.PSM !== 0) reasons.push(`frame format 0x${hex(frame.PSM, 2)}`);
  if (frame.FBMSK !== 0) reasons.push('frame mask');
  if (test.ATE) reasons.push('alpha test');
  if (test.DATE) reasons.push('destination alpha test');
  if (prim.FGE) reasons.push('fog');
  if (fields(state, 'FBA').FBA) reasons.push('alpha correction');
  if (fields(state, 'PABE').PABE) reasons.push('per-pixel alpha blending');
  if (fields(state, 'DTHE').DTHE) reasons.push('dithering');
  if (!fields(state, 'COLCLAMP').CLAMP) reasons.push('colour wrap');

  let blend = null;
  if (prim.ABE) {
    if (alpha.A > 2 || alpha.B > 2 || alpha.C > 2 || alpha.D > 2) reasons.push('reserved blend term');
    else blend = { a: TERM[alpha.A], b: TERM[alpha.B], c: FACTOR[alpha.C], d: TERM[alpha.D], fixed: alpha.FIX };
  }

  let texture = null;
  if (prim.TME) {
    const tex0 = fields(state, 'TEX0'), tex1 = fields(state, 'TEX1'), clamp = fields(state, 'CLAMP'), texa = fields(state, 'TEXA');
    if (tex0.PSM !== 0 && tex0.PSM !== 1) reasons.push(`texture format 0x${hex(tex0.PSM, 2)}`);
    if (tex0.TFX !== 0 || tex0.TCC !== 1) reasons.push('texture function other than modulate with alpha');
    if (tex1.MMIN > 1 || tex1.MMAG !== tex1.MMIN) reasons.push('minification filter differs from magnification');
    const id = `t${hex(tex0.TBP0, 4)}-${tex0.TBW}-${tex0.PSM}-${tex0.TW}x${tex0.TH}`;
    texture = {
      source: targetOfBlock.has(tex0.TBP0) ? { target: targetOfBlock.get(tex0.TBP0) } : { image: id },
      block: tex0.TBP0, pages: tex0.TBW,
      width: 1 << tex0.TW, height: 1 << tex0.TH,
      coordinates: prim.FST ? 'Texel' : 'Projective',
      addressU: { mode: WRAP[clamp.WMS], min: clamp.MINU, max: clamp.MAXU },
      addressV: { mode: WRAP[clamp.WMT], min: clamp.MINV, max: clamp.MAXV },
      filter: tex1.MMAG ? 'Bilinear' : 'Nearest',
      alpha: tex0.PSM === 1 ? { mode: 'Constant', value: texa.TA0, zeroWhenBlack: !!texa.AEM } : { mode: 'Texel' },
    };
  }

  return {
    block: frame.FBP * 32, pages: frame.FBW,
    scissor: [scissor.SCAX0, scissor.SCAY0, scissor.SCAX1, scissor.SCAY1],
    blend, antialias: !!prim.AA1, smooth: !!prim.IIP,
    depth: { test: test.ZTE ? ZTEST[test.ZTST] : 'Always', write: !zbuf.ZMSK },
    texture, skip: reasons.length ? reasons.join('; ') : null,
    field: (Number(BigInt(state.XYOFFSET) >> 32n) & 0xf) === 8 ? 1 : 0,
  };
}

function oraclePrimitives(text, draw) {
  const kind = /^vertex: # (\w+)/m.exec(text)?.[1];
  if (!ORACLE_KIND[kind]) throw new Error(`draw ${draw}: the oracle lists primitive ${kind}, which the fixture does not carry`);
  const [primitive, size] = ORACLE_KIND[kind];
  const points = [...text.matchAll(/^\s+- \{X:\s*(-?[\d.]+), Y:\s*(-?[\d.]+), Z:\s*(\d+)/gm)].map((m) => [Number(m[1]), Number(m[2]), Number(m[3])]);
  if (points.length === 0 || points.length % size) throw new Error(`draw ${draw}: ${points.length} oracle vertices do not make ${primitive}`);
  const out = [];
  for (let i = 0; i < points.length; i += size) out.push(points.slice(i, i + size));
  return { primitive, size, primitives: out };
}

const same = (wanted, got) => wanted.length === got.length && wanted.every((p, i) => p[0] === got[i].px && p[1] === got[i].py && p[2] === got[i].z);

export async function makeFixture(dump, outDir, frame = 0) {
  if (frame !== 0) throw new Error('only frame 0 of a dump has its start memory in the dump');
  const oracleDir = path.join(outDir, 'oracle');
  await runOracle(dump, oracleDir, frame);
  const names = fs.readdirSync(oracleDir);
  const draws = names.filter((name) => name.endsWith('_context.txt')).map((name) => name.slice(0, 5)).sort();
  const oracleFile = (draw, part) => {
    const found = names.filter((name) => name.startsWith(`${draw}_f`) && name.includes(part) && !name.endsWith('_alpha.png'));
    if (found.length !== 1) throw new Error(`draw ${draw}: ${found.length} oracle files match ${part}`);
    return `oracle/${found[0].replace(/\.png$/, '')}`;
  };

  const parsed = path.join(outDir, 'parsed.jsonl');
  parseGsDump(dump, parsed, {});
  const stream = [];
  for (const line of fs.readFileSync(parsed, 'utf8').split('\n')) {
    if (!line) continue;
    const record = JSON.parse(line);
    if (record.type !== 'draw' || record.frame !== frame) continue;
    const size = SIZE[record.primitive];
    if (!size) throw new Error(`draw ${record.index}: primitive ${record.primitive}`);
    for (let i = 0; i < record.indices.length; i += size) stream.push({ vertices: record.indices.slice(i, i + size).map((at) => record.vertices[at]), state: record.state });
  }
  fs.rmSync(parsed);

  // The oracle's draws are flushes of the same primitive stream; it drops the ones it culls.
  let cursor = 0, dropped = 0;
  const matched = [];
  for (const draw of draws) {
    const wanted = oraclePrimitives(fs.readFileSync(path.join(oracleDir, `${draw}_vertex.txt`), 'utf8'), draw);
    const found = [];
    for (const primitive of wanted.primitives) {
      while (cursor < stream.length && !same(primitive, stream[cursor].vertices)) { cursor++; dropped++; }
      if (cursor === stream.length) throw new Error(`draw ${draw}: primitive ${found.length} of the oracle is not in the dump's stream after ${dropped} dropped`);
      found.push(stream[cursor++]);
    }
    const key = JSON.stringify(found[0].state);
    if (!found.every((p) => JSON.stringify(p.state) === key)) throw new Error(`draw ${draw}: its primitives do not share one state`);
    matched.push({ draw, primitive: wanted.primitive, found, state: found[0].state });
  }

  const blocks = new Map();
  for (const m of matched) {
    const f = fields(m.state, 'FRAME'), s = fields(m.state, 'SCISSOR');
    const at = blocks.get(f.FBP * 32) ?? { width: f.FBW * 64, height: 0, pages: f.FBW };
    at.height = Math.max(at.height, s.SCAY1 + 1);
    blocks.set(f.FBP * 32, at);
  }
  const targetOfBlock = new Map([...blocks.keys()].map((block) => [block, `fb${hex(block, 4)}`]));

  const data = fs.readFileSync(dump);
  const headerSize = data.readUInt32LE(4), stateSize = data.readUInt32LE(12);
  const vram = data.subarray(8 + headerSize + stateSize - STATE_TAIL - VRAM_BYTES, 8 + headerSize + stateSize - STATE_TAIL);
  const pixels = (block, pages, width, height) => {
    const out = Buffer.alloc(width * height * 4);
    for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) vram.copy(out, (y * width + x) * 4, (word32(block, pages, x, y) % 0x100000) * 4, (word32(block, pages, x, y) % 0x100000) * 4 + 4);
    return out;
  };

  fs.mkdirSync(path.join(outDir, 'start'), { recursive: true });
  fs.mkdirSync(path.join(outDir, 'textures'), { recursive: true });
  const targets = [...blocks].map(([block, at]) => {
    const id = targetOfBlock.get(block);
    fs.writeFileSync(path.join(outDir, 'start', `${id}.rgba`), pixels(block, at.pages, at.width, at.height));
    return { id, width: at.width, height: at.height, start: `start/${id}.rgba` };
  });

  const textures = new Map();
  let field = 0;
  const passes = matched.map((m) => {
    const state = describeState(m.state, targetOfBlock);
    field ||= state.field;
    const flat = !state.smooth || m.primitive !== 'Triangles';
    const vertices = [];
    for (const p of m.found) {
      const last = p.vertices[p.vertices.length - 1];
      for (const v of p.vertices) {
        const colour = flat ? last.rgba : v.rgba;
        const depth = m.primitive === 'Sprites' ? last.z : v.z;
        const st = state.texture?.coordinates === 'Texel' ? [v.u, v.v, 1] : [v.s, v.t, v.q];
        vertices.push([v.px, v.py, depth, ...colour, ...st]);
      }
    }
    let texture = null;
    if (state.texture) {
      const { block, pages, ...rest } = state.texture;
      texture = rest;
      if (rest.source.image && !state.skip && !textures.has(rest.source.image)) {
        const file = `textures/${rest.source.image}.rgba`;
        fs.writeFileSync(path.join(outDir, file), pixels(block, pages, rest.width, rest.height));
        textures.set(rest.source.image, { id: rest.source.image, width: rest.width, height: rest.height, file });
      }
    }
    return {
      index: Number(m.draw), name: `draw-${m.draw.slice(2)}`, target: targetOfBlock.get(state.block), primitive: m.primitive,
      scissor: state.scissor, blend: state.blend, antialias: state.antialias, depth: state.depth, texture, skip: state.skip, vertices,
      oracle: { colour: oracleFile(m.draw, '_rt1_'), depth: oracleFile(m.draw, '_rz1_') },
    };
  });

  const frameJson = { capture: path.basename(dump, '.gs'), frame, field, targets, depthStart: oracleFile(draws[0], '_rz0_'), textures: [...textures.values()], passes };
  fs.writeFileSync(path.join(outDir, 'frame.json'), JSON.stringify(frameJson));
  return { passes: passes.length, skipped: passes.filter((p) => p.skip).length, dropped };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const [dump, outDir, frame] = process.argv.slice(2);
  if (!dump || !outDir) { console.error('usage: node tools/parity/make_fixture.mjs <dump.gs> <out dir> [frame]'); process.exit(2); }
  const done = await makeFixture(dump, outDir, Number(frame ?? 0));
  console.log(`fixture: ${done.passes} passes (${done.skipped} skipped), ${done.dropped} primitives the oracle culled, in ${outDir}`);
}
```

- [ ] **Step 4: Run the test**

Run: `node --test tools/parity/make_fixture.test.mjs`
Expected: PASS. If a `fields` error names a missing register field, read `D:/CodingProjects/Watson/Server/src/gs/registers.ts` for the field's real name and use it; the test's values do not change.

- [ ] **Step 5: Make the working fixture and commit**

Run: `node tools/parity/make_fixture.mjs D:/CodingProjects/Watson/Runtime/captures/hddosd-110U-whole3-clock.gs References/fixtures/hddosd-110U-whole3-clock/f0`
Expected: `fixture: 188 passes (26 skipped), N primitives the oracle culled, in References/fixtures/...`

```bash
git add tools/parity/make_fixture.mjs tools/parity/make_fixture.test.mjs
git commit -m "Feat(Project): Add fixture maker from a GS dump and its oracle"
```

---

### Task 3: FrameDescription, fixture loader, build targets

**Files:**
- Create: `src/scene/FrameDescription.hpp`, `src/parity/Fixture.hpp`, `src/parity/Fixture.cpp`, `src/parity/StbImage.cpp`, `tests/Check.hpp`, `tests/FixtureTest.cpp`
- Modify: `CMakeLists.txt` (append at the end)

**Interfaces:**
- Produces:

```cpp
namespace scene {
enum class Primitive { Triangles, Sprites, Lines };
enum class BlendTerm { Source, Destination, Zero };
enum class BlendFactor { SourceAlpha, DestinationAlpha, Fixed };
enum class DepthTest { Never, Always, GreaterEqual, Greater };
enum class AddressMode { Repeat, Clamp, RegionClamp, RegionRepeat };
enum class Filter { Nearest, Bilinear };
enum class Coordinates { Texel, Projective };
struct Blend { BlendTerm a, b; BlendFactor c; BlendTerm d; uint8_t fixed; };
struct Depth { DepthTest test; bool write; };
struct Address { AddressMode mode; int32_t min, max; };
struct TextureAlpha { bool constant; uint8_t value; bool zeroWhenBlack; };
struct Texture { std::string source; bool sourceIsTarget; uint32_t width, height; Coordinates coordinates; Address addressU, addressV; Filter filter; TextureAlpha alpha; };
struct Scissor { int32_t x0, y0, x1, y1; };
struct Vertex { float x, y; uint32_t depth; float r, g, b, a; float s, t, q; };
struct Pass { uint32_t index; std::string name, target; Primitive primitive; Scissor scissor; std::optional<Blend> blend; bool antialias; Depth depth; std::optional<Texture> texture; std::string skip; std::vector<Vertex> vertices; };
struct Target { std::string id; uint32_t width, height; };
struct FrameDescription { uint32_t field; std::vector<Target> targets; std::vector<Pass> passes; };
}
namespace parity {
struct Image { uint32_t width{0}, height{0}; std::vector<uint8_t> rgba; };
struct DepthImage { uint32_t width{0}, height{0}; std::vector<uint32_t> depth; };
struct OracleStep { std::string colour, depth; };
struct Fixture {
    std::filesystem::path root;
    scene::FrameDescription frame;
    std::map<std::string, Image> targetStart, textures;
    std::vector<OracleStep> oracle;      // parallel to frame.passes
    std::string depthStart;
    Image oracleColour(size_t pass) const;
    DepthImage oracleDepth(size_t pass) const;
    DepthImage startDepth() const;
};
Fixture loadFixture(const std::filesystem::path& directory);
Image loadPngPair(const std::filesystem::path& prefix);       // <prefix>.png RGB + <prefix>_alpha.png
}
```

- [ ] **Step 1: Write the failing test**

```cpp
// tests/Check.hpp
#pragma once
#include <cstdio>
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); return 1; } } while (0)
```

```cpp
// tests/FixtureTest.cpp
#include "Check.hpp"
#include "parity/Fixture.hpp"
#include <algorithm>
#include <set>

int main(int argc, char** argv) {
    CHECK(argc == 2);
    const parity::Fixture fixture = parity::loadFixture(argv[1]);
    const auto& frame = fixture.frame;

    CHECK(frame.passes.size() == 188);
    CHECK(fixture.oracle.size() == 188);
    CHECK(frame.targets.size() == 3);
    for (const auto& target : frame.targets) {
        CHECK(target.width == 640 && target.height == 224);
        CHECK(fixture.targetStart.at(target.id).rgba.size() == 640u * 224u * 4u);
    }

    const scene::Pass& copy = frame.passes[2];
    CHECK(copy.primitive == scene::Primitive::Sprites);
    CHECK(copy.target == "fb1a40");
    CHECK(copy.texture && copy.texture->sourceIsTarget && copy.texture->source == "fb0000");
    CHECK(copy.texture->alpha.constant && copy.texture->alpha.value == 127 && copy.texture->alpha.zeroWhenBlack);
    CHECK(copy.texture->addressU.mode == scene::AddressMode::RegionClamp && copy.texture->addressU.max == 639);
    CHECK(copy.texture->filter == scene::Filter::Bilinear);
    CHECK(!copy.blend);
    CHECK(copy.vertices.size() == 2 && copy.vertices[1].x == 640.0f && copy.vertices[1].y == 224.0f);

    CHECK(std::count_if(frame.passes.begin(), frame.passes.end(), [](const scene::Pass& p) { return !p.skip.empty(); }) == 26);

    // The copy of the display into a work buffer takes its alpha from the constant 127 or 0:
    // if the alpha file were scaled, other values would appear.
    const parity::Image afterCopy = fixture.oracleColour(2);
    CHECK(afterCopy.width == 640 && afterCopy.height == 224);
    std::set<uint8_t> alphas;
    for (size_t i = 3; i < afterCopy.rgba.size(); i += 4) alphas.insert(afterCopy.rgba[i]);
    for (uint8_t a : alphas) CHECK(a == 0 || a == 127);

    // Depth words are little-endian in the PNG pair: after the background (pass 2) the largest
    // depth is one of its vertices' range, far below 2^24.
    const parity::DepthImage depth = fixture.oracleDepth(1);
    CHECK(depth.depth.size() == 640u * 224u);
    const uint32_t deepest = *std::max_element(depth.depth.begin(), depth.depth.end());
    CHECK(deepest > 0 && deepest < (1u << 24));
    return 0;
}
```

- [ ] **Step 2: Add the build targets and see the test fail to build**

Append to `CMakeLists.txt`:

```cmake
# ──────────────────────────────────────────────────────────────────────────────
# NATIVE RENDERER (parity tool and tests; the application above is unchanged)
# ──────────────────────────────────────────────────────────────────────────────
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(json)

add_library(ClockRenderer STATIC
    src/parity/Fixture.cpp
    src/parity/StbImage.cpp
)
target_include_directories(ClockRenderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty)
target_link_libraries(ClockRenderer PUBLIC nlohmann_json::nlohmann_json)

set(CLOCK_FIXTURE ${CMAKE_SOURCE_DIR}/References/fixtures/hddosd-110U-whole3-clock/f0)

enable_testing()
add_executable(FixtureTest tests/FixtureTest.cpp)
target_link_libraries(FixtureTest PRIVATE ClockRenderer)
add_test(NAME FixtureTest COMMAND FixtureTest ${CLOCK_FIXTURE})
```

Run: `cmake -S . -B build; cmake --build build --config Debug --target FixtureTest`
Expected: FAIL, `Cannot find source file: src/parity/Fixture.cpp`

- [ ] **Step 3: Write the header of the frame**

`src/scene/FrameDescription.hpp`: the `scene` block of the Interfaces above, verbatim, under `#pragma once` with `<cstdint>`, `<optional>`, `<string>`, `<vector>`.

- [ ] **Step 4: Write the loader**

`src/parity/Fixture.hpp`: the `parity` block of the Interfaces above, under `#pragma once` with `"scene/FrameDescription.hpp"`, `<filesystem>`, `<map>`.

```cpp
// src/parity/StbImage.cpp
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
```

```cpp
// src/parity/Fixture.cpp
#include "parity/Fixture.hpp"
#include <nlohmann/json.hpp>
#include <stb_image.h>
#include <fstream>
#include <stdexcept>

namespace parity {
namespace {

template <typename Enum>
Enum pick(const std::string& value, std::initializer_list<std::pair<const char*, Enum>> names, const char* what) {
    for (const auto& [name, e] : names) if (value == name) return e;
    throw std::runtime_error(std::string("unknown ") + what + ": " + value);
}

std::vector<uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

scene::BlendTerm term(const std::string& v) {
    return pick<scene::BlendTerm>(v, {{"Source", scene::BlendTerm::Source}, {"Destination", scene::BlendTerm::Destination}, {"Zero", scene::BlendTerm::Zero}}, "blend term");
}

scene::Address address(const nlohmann::json& j) {
    return {pick<scene::AddressMode>(j.at("mode"), {{"Repeat", scene::AddressMode::Repeat}, {"Clamp", scene::AddressMode::Clamp},
                {"RegionClamp", scene::AddressMode::RegionClamp}, {"RegionRepeat", scene::AddressMode::RegionRepeat}}, "address mode"),
            j.at("min"), j.at("max")};
}

DepthImage depthFromPair(const Image& pair) {
    DepthImage out{pair.width, pair.height, std::vector<uint32_t>(size_t(pair.width) * pair.height)};
    for (size_t i = 0; i < out.depth.size(); i++) {
        const uint8_t* p = &pair.rgba[i * 4];
        out.depth[i] = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    }
    return out;
}

}  // namespace

Image loadPngPair(const std::filesystem::path& prefix) {
    int w = 0, h = 0, n = 0, aw = 0, ah = 0;
    const std::string colourPath = prefix.string() + ".png", alphaPath = prefix.string() + "_alpha.png";
    stbi_uc* colour = stbi_load(colourPath.c_str(), &w, &h, &n, 3);
    if (!colour) throw std::runtime_error("cannot read " + colourPath);
    stbi_uc* alpha = stbi_load(alphaPath.c_str(), &aw, &ah, &n, 1);
    if (!alpha || aw != w || ah != h) { stbi_image_free(colour); throw std::runtime_error("cannot read " + alphaPath + " at the size of its colour file"); }
    Image out{uint32_t(w), uint32_t(h), std::vector<uint8_t>(size_t(w) * h * 4)};
    for (size_t i = 0; i < size_t(w) * h; i++) {
        out.rgba[i * 4 + 0] = colour[i * 3 + 0];
        out.rgba[i * 4 + 1] = colour[i * 3 + 1];
        out.rgba[i * 4 + 2] = colour[i * 3 + 2];
        out.rgba[i * 4 + 3] = alpha[i];
    }
    stbi_image_free(colour);
    stbi_image_free(alpha);
    return out;
}

Image Fixture::oracleColour(size_t pass) const { return loadPngPair(root / oracle.at(pass).colour); }
DepthImage Fixture::oracleDepth(size_t pass) const { return depthFromPair(loadPngPair(root / oracle.at(pass).depth)); }
DepthImage Fixture::startDepth() const { return depthFromPair(loadPngPair(root / depthStart)); }

Fixture loadFixture(const std::filesystem::path& directory) {
    std::ifstream in(directory / "frame.json");
    if (!in) throw std::runtime_error("no frame.json in " + directory.string());
    const nlohmann::json j = nlohmann::json::parse(in);

    Fixture fixture;
    fixture.root = directory;
    fixture.frame.field = j.at("field");
    fixture.depthStart = j.at("depthStart");

    for (const auto& t : j.at("targets")) {
        scene::Target target{t.at("id"), t.at("width"), t.at("height")};
        Image start{target.width, target.height, readFile(directory / t.at("start").get<std::string>())};
        if (start.rgba.size() != size_t(target.width) * target.height * 4) throw std::runtime_error("start buffer of " + target.id + " has the wrong size");
        fixture.targetStart.emplace(target.id, std::move(start));
        fixture.frame.targets.push_back(std::move(target));
    }
    for (const auto& t : j.at("textures")) {
        Image image{t.at("width"), t.at("height"), readFile(directory / t.at("file").get<std::string>())};
        if (image.rgba.size() != size_t(image.width) * image.height * 4) throw std::runtime_error("texture " + t.at("id").get<std::string>() + " has the wrong size");
        fixture.textures.emplace(t.at("id").get<std::string>(), std::move(image));
    }

    for (const auto& p : j.at("passes")) {
        scene::Pass pass{};
        pass.index = p.at("index");
        pass.name = p.at("name");
        pass.target = p.at("target");
        pass.primitive = pick<scene::Primitive>(p.at("primitive"), {{"Triangles", scene::Primitive::Triangles}, {"Sprites", scene::Primitive::Sprites}, {"Lines", scene::Primitive::Lines}}, "primitive");
        const auto& s = p.at("scissor");
        pass.scissor = {s[0], s[1], s[2], s[3]};
        if (!p.at("blend").is_null()) {
            const auto& b = p.at("blend");
            pass.blend = scene::Blend{term(b.at("a")), term(b.at("b")),
                pick<scene::BlendFactor>(b.at("c"), {{"SourceAlpha", scene::BlendFactor::SourceAlpha}, {"DestinationAlpha", scene::BlendFactor::DestinationAlpha}, {"Fixed", scene::BlendFactor::Fixed}}, "blend factor"),
                term(b.at("d")), b.at("fixed")};
        }
        pass.antialias = p.at("antialias");
        pass.depth = {pick<scene::DepthTest>(p.at("depth").at("test"), {{"Never", scene::DepthTest::Never}, {"Always", scene::DepthTest::Always},
                          {"GreaterEqual", scene::DepthTest::GreaterEqual}, {"Greater", scene::DepthTest::Greater}}, "depth test"),
                      p.at("depth").at("write")};
        if (!p.at("texture").is_null()) {
            const auto& t = p.at("texture");
            scene::Texture texture{};
            texture.sourceIsTarget = t.at("source").contains("target");
            texture.source = t.at("source").at(texture.sourceIsTarget ? "target" : "image");
            texture.width = t.at("width");
            texture.height = t.at("height");
            texture.coordinates = pick<scene::Coordinates>(t.at("coordinates"), {{"Texel", scene::Coordinates::Texel}, {"Projective", scene::Coordinates::Projective}}, "coordinates");
            texture.addressU = address(t.at("addressU"));
            texture.addressV = address(t.at("addressV"));
            texture.filter = pick<scene::Filter>(t.at("filter"), {{"Nearest", scene::Filter::Nearest}, {"Bilinear", scene::Filter::Bilinear}}, "filter");
            const auto& a = t.at("alpha");
            if (a.at("mode") == "Constant") texture.alpha = {true, a.at("value"), a.at("zeroWhenBlack")};
            pass.texture = std::move(texture);
        }
        if (!p.at("skip").is_null()) pass.skip = p.at("skip");
        for (const auto& v : p.at("vertices")) pass.vertices.push_back({v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9]});
        fixture.oracle.push_back({p.at("oracle").at("colour"), p.at("oracle").at("depth")});
        fixture.frame.passes.push_back(std::move(pass));
    }
    return fixture;
}

}  // namespace parity
```

- [ ] **Step 5: Build and run the test**

Run: `cmake --build build --config Debug --target FixtureTest; ctest --test-dir build -C Debug -R FixtureTest --output-on-failure`
Expected: `100% tests passed`. If the alpha check fails with other values, read `GSPng.cpp` and `GSTextureSW::Save` in the PCSX2 tree for how the alpha file is written and undo it in `loadPngPair`; if the depth check fails, the same for the depth file's byte order.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/scene/FrameDescription.hpp src/parity/Fixture.hpp src/parity/Fixture.cpp src/parity/StbImage.cpp tests/Check.hpp tests/FixtureTest.cpp
git commit -m "Feat(Renderer): Add the native frame description and fixture loader"
```

---

### Task 4: Image comparison

**Files:**
- Create: `src/parity/Compare.hpp`, `src/parity/Compare.cpp`, `tests/CompareTest.cpp`
- Modify: `CMakeLists.txt` (the `ClockRenderer` source list; a test)

**Interfaces:**
- Consumes: `parity::Image`, `parity::DepthImage` (Task 3).
- Produces:

```cpp
namespace parity {
struct Difference {
    uint64_t pixels{0}, differing{0};
    uint32_t largest{0};                       // largest channel difference
    std::array<uint64_t, 5> buckets{};         // pixels by largest channel difference: 0, 1, 2-3, 4-15, 16+
    Difference& operator+=(const Difference& other);
};
Difference compare(const Image& ours, const Image& oracle);
Difference compare(const DepthImage& ours, const DepthImage& oracle);
Image differenceImage(const Image& ours, const Image& oracle, uint32_t gain);   // |a-b| * gain, clamped, alpha 255
}
```

- [ ] **Step 1: Write the failing test**

```cpp
// tests/CompareTest.cpp
#include "Check.hpp"
#include "parity/Compare.hpp"

int main() {
    parity::Image a{2, 2, std::vector<uint8_t>(16, 10)}, b = a;
    b.rgba[0] = 11;                 // pixel 0: 1
    b.rgba[5] = 13;                 // pixel 1: 3
    b.rgba[10] = 30;                // pixel 2: 20
    const parity::Difference d = parity::compare(a, b);
    CHECK(d.pixels == 4 && d.differing == 3 && d.largest == 20);
    CHECK(d.buckets[0] == 1 && d.buckets[1] == 1 && d.buckets[2] == 1 && d.buckets[3] == 0 && d.buckets[4] == 1);

    parity::Difference sum = d;
    sum += d;
    CHECK(sum.pixels == 8 && sum.differing == 6 && sum.largest == 20 && sum.buckets[4] == 2);

    const parity::Image diff = parity::differenceImage(a, b, 8);
    CHECK(diff.rgba[0] == 8 && diff.rgba[5] == 24 && diff.rgba[10] == 160 && diff.rgba[3] == 255);

    parity::DepthImage za{2, 1, {5, 9}}, zb{2, 1, {5, 12}};
    const parity::Difference z = parity::compare(za, zb);
    CHECK(z.pixels == 2 && z.differing == 1 && z.largest == 3);

    bool threw = false;
    try { parity::compare(a, parity::Image{1, 1, std::vector<uint8_t>(4)}); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    return 0;
}
```

- [ ] **Step 2: Add to the build and see it fail**

In `CMakeLists.txt` add `src/parity/Compare.cpp` to `add_library(ClockRenderer ...)` and append:

```cmake
add_executable(CompareTest tests/CompareTest.cpp)
target_link_libraries(CompareTest PRIVATE ClockRenderer)
add_test(NAME CompareTest COMMAND CompareTest)
```

Run: `cmake -S . -B build; cmake --build build --config Debug --target CompareTest`
Expected: FAIL, `Cannot find source file: src/parity/Compare.cpp`

- [ ] **Step 3: Implement**

`src/parity/Compare.hpp`: the Interfaces block above under `#pragma once` with `"parity/Fixture.hpp"`, `<array>`.

```cpp
// src/parity/Compare.cpp
#include "parity/Compare.hpp"
#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace parity {
namespace {
void count(Difference& d, uint32_t delta) {
    d.pixels++;
    if (delta) d.differing++;
    d.largest = std::max(d.largest, delta);
    d.buckets[delta == 0 ? 0 : delta == 1 ? 1 : delta < 4 ? 2 : delta < 16 ? 3 : 4]++;
}
}  // namespace

Difference& Difference::operator+=(const Difference& other) {
    pixels += other.pixels;
    differing += other.differing;
    largest = std::max(largest, other.largest);
    for (size_t i = 0; i < buckets.size(); i++) buckets[i] += other.buckets[i];
    return *this;
}

Difference compare(const Image& ours, const Image& oracle) {
    if (ours.width != oracle.width || ours.height != oracle.height || ours.rgba.size() != oracle.rgba.size()) throw std::runtime_error("images of different sizes cannot be compared");
    Difference d;
    for (size_t i = 0; i < ours.rgba.size(); i += 4) {
        uint32_t delta = 0;
        for (size_t c = 0; c < 4; c++) delta = std::max(delta, uint32_t(std::abs(int(ours.rgba[i + c]) - int(oracle.rgba[i + c]))));
        count(d, delta);
    }
    return d;
}

Difference compare(const DepthImage& ours, const DepthImage& oracle) {
    if (ours.depth.size() != oracle.depth.size()) throw std::runtime_error("depth images of different sizes cannot be compared");
    Difference d;
    for (size_t i = 0; i < ours.depth.size(); i++) count(d, ours.depth[i] > oracle.depth[i] ? ours.depth[i] - oracle.depth[i] : oracle.depth[i] - ours.depth[i]);
    return d;
}

Image differenceImage(const Image& ours, const Image& oracle, uint32_t gain) {
    if (ours.rgba.size() != oracle.rgba.size()) throw std::runtime_error("images of different sizes cannot be compared");
    Image out{ours.width, ours.height, std::vector<uint8_t>(ours.rgba.size())};
    for (size_t i = 0; i < ours.rgba.size(); i += 4) {
        for (size_t c = 0; c < 3; c++) out.rgba[i + c] = uint8_t(std::min(255u, uint32_t(std::abs(int(ours.rgba[i + c]) - int(oracle.rgba[i + c]))) * gain));
        out.rgba[i + 3] = 255;
    }
    return out;
}

}  // namespace parity
```

- [ ] **Step 4: Run the test**

Run: `cmake --build build --config Debug --target CompareTest; ctest --test-dir build -C Debug -R CompareTest --output-on-failure`
Expected: `100% tests passed`

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/parity/Compare.hpp src/parity/Compare.cpp tests/CompareTest.cpp
git commit -m "Feat(Renderer): Add image comparison for the parity report"
```

---

### Task 5: Headless device

**Files:**
- Create: `src/core/GpuDevice.hpp`, `src/core/HeadlessContext.hpp`, `src/core/HeadlessContext.cpp`, `src/core/VmaUsage.cpp`, `tests/HeadlessTest.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
struct GpuDevice { VkDevice device; VkPhysicalDevice physicalDevice; VmaAllocator allocator; VkQueue queue; uint32_t queueFamily; };
class HeadlessContext {
public:
    explicit HeadlessContext(bool validation);
    ~HeadlessContext();
    HeadlessContext(const HeadlessContext&) = delete;
    HeadlessContext& operator=(const HeadlessContext&) = delete;
    GpuDevice gpu() const;
    uint32_t validationErrors() const;
};
```

- [ ] **Step 1: Write the failing test**

```cpp
// tests/HeadlessTest.cpp
#include "Check.hpp"
#include "core/HeadlessContext.hpp"

int main() {
    HeadlessContext context(true);
    const GpuDevice gpu = context.gpu();
    CHECK(gpu.device != VK_NULL_HANDLE && gpu.allocator != VK_NULL_HANDLE && gpu.queue != VK_NULL_HANDLE);

    VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT interlock{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &interlock};
    vkGetPhysicalDeviceFeatures2(gpu.physicalDevice, &features);
    CHECK(interlock.fragmentShaderPixelInterlock == VK_TRUE);
    CHECK(features.features.fragmentStoresAndAtomics == VK_TRUE);

    VkFormatProperties format{};
    vkGetPhysicalDeviceFormatProperties(gpu.physicalDevice, VK_FORMAT_R8G8B8A8_UINT, &format);
    CHECK(format.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
    vkGetPhysicalDeviceFormatProperties(gpu.physicalDevice, VK_FORMAT_R32_UINT, &format);
    CHECK(format.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
    CHECK(context.validationErrors() == 0);
    return 0;
}
```

- [ ] **Step 2: Add to the build and see it fail**

In `CMakeLists.txt`: add `src/core/HeadlessContext.cpp` and `src/core/VmaUsage.cpp` to `ClockRenderer`; change its link line to
`target_link_libraries(ClockRenderer PUBLIC nlohmann_json::nlohmann_json vk-bootstrap VulkanMemoryAllocator Vulkan::Vulkan)`; append:

```cmake
add_executable(HeadlessTest tests/HeadlessTest.cpp)
target_link_libraries(HeadlessTest PRIVATE ClockRenderer)
add_test(NAME HeadlessTest COMMAND HeadlessTest)
```

Run: `cmake -S . -B build; cmake --build build --config Debug --target HeadlessTest`
Expected: FAIL, `Cannot find source file: src/core/HeadlessContext.cpp`

- [ ] **Step 3: Implement**

```cpp
// src/core/GpuDevice.hpp
#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

struct GpuDevice {
    VkDevice device{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VmaAllocator allocator{VK_NULL_HANDLE};
    VkQueue queue{VK_NULL_HANDLE};
    uint32_t queueFamily{0};
};
```

```cpp
// src/core/HeadlessContext.hpp
#pragma once
#include "core/GpuDevice.hpp"
#include <VkBootstrap.h>
#include <atomic>

class HeadlessContext {
public:
    explicit HeadlessContext(bool validation);
    ~HeadlessContext();
    HeadlessContext(const HeadlessContext&) = delete;
    HeadlessContext& operator=(const HeadlessContext&) = delete;

    GpuDevice gpu() const { return {m_device.device, m_device.physical_device.physical_device, m_allocator, m_queue, m_queueFamily}; }
    uint32_t validationErrors() const { return m_errors.load(); }

private:
    vkb::Instance m_instance;
    vkb::Device m_device;
    VmaAllocator m_allocator{VK_NULL_HANDLE};
    VkQueue m_queue{VK_NULL_HANDLE};
    uint32_t m_queueFamily{0};
    std::atomic<uint32_t> m_errors{0};
};
```

```cpp
// src/core/VmaUsage.cpp
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
```

```cpp
// src/core/HeadlessContext.cpp
#include "core/HeadlessContext.hpp"
#include <cstdio>
#include <stdexcept>

namespace {
VKAPI_ATTR VkBool32 VKAPI_CALL onMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                         const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        static_cast<std::atomic<uint32_t>*>(user)->fetch_add(1);
        std::fprintf(stderr, "validation: %s\n", data->pMessage);
    }
    return VK_FALSE;
}
}  // namespace

HeadlessContext::HeadlessContext(bool validation) {
    vkb::InstanceBuilder instanceBuilder;
    instanceBuilder.set_app_name("CrystalClockParity").require_api_version(1, 4, 0).set_headless(true);
    if (validation) instanceBuilder.request_validation_layers(true).set_debug_callback(onMessage).set_debug_callback_user_data_pointer(&m_errors);
    auto instance = instanceBuilder.build();
    if (!instance) throw std::runtime_error("instance: " + instance.error().message());
    m_instance = instance.value();

    VkPhysicalDeviceFeatures features{};
    features.fragmentStoresAndAtomics = VK_TRUE;
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT interlock{};
    interlock.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT;
    interlock.fragmentShaderPixelInterlock = VK_TRUE;

    vkb::PhysicalDeviceSelector selector{m_instance};
    auto physical = selector.set_minimum_version(1, 4)
        .set_required_features(features)
        .set_required_features_13(features13)
        .add_required_extension(VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME)
        .add_required_extension_features(interlock)
        .select();
    if (!physical) throw std::runtime_error("physical device: " + physical.error().message());

    auto device = vkb::DeviceBuilder{physical.value()}.build();
    if (!device) throw std::runtime_error("device: " + device.error().message());
    m_device = device.value();

    auto queue = m_device.get_queue(vkb::QueueType::graphics);
    auto family = m_device.get_queue_index(vkb::QueueType::graphics);
    if (!queue || !family) throw std::runtime_error("no graphics queue");
    m_queue = queue.value();
    m_queueFamily = family.value();

    VmaAllocatorCreateInfo allocator{};
    allocator.physicalDevice = m_device.physical_device.physical_device;
    allocator.device = m_device.device;
    allocator.instance = m_instance.instance;
    allocator.vulkanApiVersion = VK_API_VERSION_1_4;
    if (vmaCreateAllocator(&allocator, &m_allocator) != VK_SUCCESS) throw std::runtime_error("allocator");
}

HeadlessContext::~HeadlessContext() {
    if (m_allocator) vmaDestroyAllocator(m_allocator);
    vkb::destroy_device(m_device);
    vkb::destroy_instance(m_instance);
}
```

- [ ] **Step 4: Run the test**

Run: `cmake --build build --config Debug --target HeadlessTest; ctest --test-dir build -C Debug -R HeadlessTest --output-on-failure`
Expected: `100% tests passed`

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/core/GpuDevice.hpp src/core/HeadlessContext.hpp src/core/HeadlessContext.cpp src/core/VmaUsage.cpp tests/HeadlessTest.cpp
git commit -m "Feat(Core): Add a headless device with ordered pixel interlock"
```

---

### Task 6: The parity renderer, against known answers

Coverage, ordering, blending and depth are tested here on synthetic passes whose result is known by arithmetic, before any captured frame.

**Files:**
- Create: `src/renderer/GsParityRenderer.hpp`, `src/renderer/GsParityRenderer.cpp`, `shaders/GsParity.vert`, `shaders/GsParity.frag`, `tests/RendererTest.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `GpuDevice` (Task 5), `scene::Pass` (Task 3).
- Produces:

```cpp
class GsParityRenderer {
public:
    GsParityRenderer(const GpuDevice& gpu, const std::filesystem::path& shaderDirectory);
    ~GsParityRenderer();
    void setTarget(const std::string& id, uint32_t width, uint32_t height, std::span<const uint8_t> rgba);
    void setDepth(uint32_t width, uint32_t height, std::span<const uint32_t> depth);
    void setTexture(const std::string& id, uint32_t width, uint32_t height, std::span<const uint8_t> rgba);
    void draw(const scene::Pass& pass);                    // throws on pass.skip, unknown target, texture == target
    std::vector<uint8_t> readTarget(const std::string& id);
    std::vector<uint32_t> readDepth();
};
```

- [ ] **Step 1: Write the failing test**

```cpp
// tests/RendererTest.cpp
#include "Check.hpp"
#include "core/HeadlessContext.hpp"
#include "renderer/GsParityRenderer.hpp"
#include <tuple>

namespace {
constexpr uint32_t W = 64, H = 32;

scene::Pass pass(scene::Primitive primitive, std::vector<scene::Vertex> vertices) {
    scene::Pass p{};
    p.index = 1; p.name = "test"; p.target = "t"; p.primitive = primitive;
    p.scissor = {0, 0, int32_t(W) - 1, int32_t(H) - 1};
    p.depth = {scene::DepthTest::Always, true};
    p.vertices = std::move(vertices);
    return p;
}
scene::Vertex at(float x, float y, uint32_t depth, float r, float g, float b, float a) { return {x, y, depth, r, g, b, a, 0, 0, 1}; }
const uint8_t* px(const std::vector<uint8_t>& image, uint32_t x, uint32_t y) { return &image[(y * W + x) * 4]; }
const scene::Blend Add{scene::BlendTerm::Source, scene::BlendTerm::Zero, scene::BlendFactor::SourceAlpha, scene::BlendTerm::Destination, 0};
}  // namespace

int main(int argc, char** argv) {
    CHECK(argc == 2);
    HeadlessContext context(true);
    GsParityRenderer renderer(context.gpu(), argv[1]);
    const std::vector<uint8_t> black(W * H * 4, 0);
    const std::vector<uint32_t> depth100(W * H, 100);

    // A sprite covers [x0, x1) x [y0, y1) at integer sample points; fractional corners round up.
    renderer.setTarget("t", W, H, black);
    renderer.setDepth(W, H, depth100);
    renderer.draw(pass(scene::Primitive::Sprites, {at(10, 20, 7, 0, 0, 0, 0), at(13, 22, 7, 10, 20, 30, 128), at(20.5f, 4, 7, 0, 0, 0, 0), at(22.5f, 5, 7, 1, 2, 3, 4)}));
    std::vector<uint8_t> image = renderer.readTarget("t");
    uint32_t lit = 0;
    for (uint32_t y = 0; y < H; y++) for (uint32_t x = 0; x < W; x++) {
        const bool first = x >= 10 && x < 13 && y >= 20 && y < 22, second = x >= 21 && x < 23 && y == 4;
        const uint8_t* p = px(image, x, y);
        if (first) CHECK(p[0] == 10 && p[1] == 20 && p[2] == 30 && p[3] == 128);
        else if (second) CHECK(p[0] == 1 && p[1] == 2 && p[2] == 3 && p[3] == 4);
        else CHECK(p[0] == 0 && p[3] == 0);
        lit += first || second;
    }
    CHECK(lit == 8);
    std::vector<uint32_t> depth = renderer.readDepth();
    CHECK(depth[20 * W + 10] == 7 && depth[0] == 100);

    // Sixty-four overlapping additive sprites in one pass: each must see the one before it.
    renderer.setTarget("t", W, H, black);
    scene::Pass stack = pass(scene::Primitive::Sprites, {});
    for (int i = 0; i < 64; i++) { stack.vertices.push_back(at(0, 0, 0, 0, 0, 0, 0)); stack.vertices.push_back(at(8, 8, 0, 1, 2, 3, 128)); }
    stack.blend = Add;
    renderer.draw(stack);
    image = renderer.readTarget("t");
    for (uint32_t y = 0; y < 8; y++) for (uint32_t x = 0; x < 8; x++) CHECK(px(image, x, y)[0] == 64 && px(image, x, y)[1] == 128 && px(image, x, y)[2] == 192);
    CHECK(px(image, 8, 0)[0] == 0);

    // Two triangles sharing an edge cover every pixel of their square exactly once.
    renderer.setTarget("t", W, H, black);
    scene::Pass quad = pass(scene::Primitive::Triangles, {at(3.25f, 2.5f, 0, 1, 1, 1, 128), at(40.75f, 5.125f, 0, 1, 1, 1, 128), at(9.5f, 29.0625f, 0, 1, 1, 1, 128),
                                                           at(40.75f, 5.125f, 0, 1, 1, 1, 128), at(9.5f, 29.0625f, 0, 1, 1, 1, 128), at(50.0f, 27.5f, 0, 1, 1, 1, 128)});
    quad.blend = Add;
    renderer.draw(quad);
    image = renderer.readTarget("t");
    uint32_t covered = 0;
    for (uint32_t i = 0; i < W * H; i++) { CHECK(image[i * 4] <= 1); covered += image[i * 4]; }
    CHECK(covered > 500);

    // Depth tests against a buffer holding 100.
    const std::tuple<scene::DepthTest, uint32_t, bool> depthCases[] = {
        {scene::DepthTest::Greater, 100u, false}, {scene::DepthTest::Greater, 101u, true},
        {scene::DepthTest::GreaterEqual, 100u, true}, {scene::DepthTest::GreaterEqual, 99u, false}, {scene::DepthTest::Never, 500u, false}};
    for (const auto& [test, z, drawn] : depthCases) {
        renderer.setTarget("t", W, H, black);
        renderer.setDepth(W, H, depth100);
        scene::Pass p = pass(scene::Primitive::Sprites, {at(0, 0, z, 0, 0, 0, 0), at(4, 4, z, 9, 9, 9, 9)});
        p.depth = {test, true};
        renderer.draw(p);
        CHECK((renderer.readTarget("t")[0] == 9) == drawn);
        CHECK(renderer.readDepth()[0] == (drawn ? z : 100u));
    }

    // Subtraction clamps at zero; a scissor cuts.
    renderer.setTarget("t", W, H, std::vector<uint8_t>(W * H * 4, 5));
    scene::Pass sub = pass(scene::Primitive::Sprites, {at(0, 0, 0, 0, 0, 0, 0), at(8, 8, 0, 9, 3, 0, 128)});
    sub.blend = scene::Blend{scene::BlendTerm::Zero, scene::BlendTerm::Source, scene::BlendFactor::SourceAlpha, scene::BlendTerm::Destination, 0};
    sub.scissor = {2, 2, 5, 5};
    renderer.draw(sub);
    image = renderer.readTarget("t");
    CHECK(px(image, 2, 2)[0] == 0 && px(image, 2, 2)[1] == 2 && px(image, 2, 2)[2] == 5);
    CHECK(px(image, 1, 2)[0] == 5 && px(image, 6, 5)[0] == 5);

    // A texture that is the pass's own target is refused.
    scene::Pass self = pass(scene::Primitive::Sprites, {at(0, 0, 0, 0, 0, 0, 0), at(4, 4, 0, 128, 128, 128, 128)});
    self.texture = scene::Texture{"t", true, 64, 32, scene::Coordinates::Texel, {}, {}, scene::Filter::Nearest, {}};
    bool threw = false;
    try { renderer.draw(self); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    // Nearest and bilinear on a 2x2 texture, texel coordinates: the centre of the four texels is their mean.
    const std::vector<uint8_t> four{0, 0, 0, 128, 64, 0, 0, 128, 0, 64, 0, 128, 64, 64, 0, 128};
    renderer.setTexture("four", 2, 2, four);
    for (const auto filter : {scene::Filter::Nearest, scene::Filter::Bilinear}) {
        renderer.setTarget("t", W, H, black);
        scene::Pass textured = pass(scene::Primitive::Sprites, {{0, 0, 0, 128, 128, 128, 128, 1.0f, 1.0f, 1}, {1, 1, 0, 128, 128, 128, 128, 1.0f, 1.0f, 1}});
        textured.texture = scene::Texture{"four", false, 2, 2, scene::Coordinates::Texel, {scene::AddressMode::Clamp, 0, 0}, {scene::AddressMode::Clamp, 0, 0}, filter, {}};
        renderer.draw(textured);
        const uint8_t* p = px(image = renderer.readTarget("t"), 0, 0);
        if (filter == scene::Filter::Nearest) CHECK(p[0] == 64 && p[1] == 64);
        else CHECK(p[0] == 32 && p[1] == 32);
    }

    CHECK(context.validationErrors() == 0);
    return 0;
}
```

- [ ] **Step 2: Add to the build and see it fail**

In `CMakeLists.txt`: add `src/renderer/GsParityRenderer.cpp` to `ClockRenderer`; add `add_dependencies(ClockRenderer Shaders)`; append:

```cmake
add_executable(RendererTest tests/RendererTest.cpp)
target_link_libraries(RendererTest PRIVATE ClockRenderer)
add_test(NAME RendererTest COMMAND RendererTest ${CMAKE_SOURCE_DIR}/bin/shaders)
```

Run: `cmake -S . -B build; cmake --build build --config Debug --target RendererTest`
Expected: FAIL, `Cannot find source file: src/renderer/GsParityRenderer.cpp`

- [ ] **Step 3: Write the shaders**

```glsl
// shaders/GsParity.vert
#version 460

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inDepth;
layout(location = 2) in vec4 inColour;
layout(location = 3) in vec3 inTexture;

layout(push_constant) uniform DrawState {
    ivec4 scissor; ivec4 blend; ivec4 misc; ivec4 tex; ivec4 texSize; ivec4 addressU; ivec4 addressV; ivec4 flags;
} state;

layout(location = 0) noperspective out vec2 outDepth;
layout(location = 1) noperspective out vec4 outColour;
layout(location = 2) noperspective out vec3 outTexture;

void main() {
    // The GS samples a pixel at its integer coordinate; Vulkan samples at the centre.
    gl_Position = vec4((inPosition + 0.5) / vec2(state.flags.yz) * 2.0 - 1.0, 0.0, 1.0);
    outDepth = inDepth;
    outColour = inColour;
    outTexture = inTexture;
}
```

```glsl
// shaders/GsParity.frag
#version 460
#extension GL_ARB_fragment_shader_interlock : require

layout(pixel_interlock_ordered) in;

layout(set = 0, binding = 0, rgba8ui) uniform uimage2D targetImage;
layout(set = 0, binding = 1, r32ui) uniform uimage2D depthImage;
layout(set = 0, binding = 2) uniform usampler2D textureImage;

// scissor: x0 y0 x1 y1 (inclusive)        blend: a b c d (terms 0 source 1 destination 2 zero; c 0 source alpha 1 destination alpha 2 fixed)
// misc: blend on, fixed, depth test (0 never 1 always 2 >= 3 >), depth write
// tex: on, coordinates (0 texel 1 projective), filter (0 nearest 1 bilinear), alpha (0 texel 1 constant)
// texSize: width, height, constant alpha, zero when black      address: mode (0 repeat 1 clamp 2 region clamp 3 region repeat), min, max
// flags: antialias, target width, target height
layout(push_constant) uniform DrawState {
    ivec4 scissor; ivec4 blend; ivec4 misc; ivec4 tex; ivec4 texSize; ivec4 addressU; ivec4 addressV; ivec4 flags;
} state;

layout(location = 0) noperspective in vec2 inDepth;
layout(location = 1) noperspective in vec4 inColour;
layout(location = 2) noperspective in vec3 inTexture;

int address(int value, ivec4 mode, int size) {
    if (mode.x == 0) return value & (size - 1);
    if (mode.x == 1) return clamp(value, 0, size - 1);
    if (mode.x == 2) return clamp(value, mode.y, mode.z);
    return (value & mode.y) | mode.z;
}

ivec4 texel(ivec2 at) {
    ivec2 p = ivec2(address(at.x, state.addressU, state.texSize.x), address(at.y, state.addressV, state.texSize.y));
    ivec4 c = ivec4(texelFetch(textureImage, clamp(p, ivec2(0), textureSize(textureImage, 0) - 1), 0));
    if (state.tex.w == 1) c.a = (state.texSize.w == 1 && c.rgb == ivec3(0)) ? 0 : state.texSize.z;
    return c;
}

// Bilinear weights are four bits: a + ((b - a) * f >> 4).
ivec4 mix4(ivec4 a, ivec4 b, int f) { return a + (((b - a) * f) >> 4); }

ivec4 sampleTexture() {
    vec2 texels = state.tex.y == 0 ? inTexture.xy : inTexture.xy / inTexture.z * vec2(state.texSize.xy);
    ivec2 uv = ivec2(floor(texels * 65536.0));
    if (state.tex.z == 0) return texel(uv >> 16);
    uv -= 0x8000;
    ivec2 f = (uv >> 12) & 0xf;
    ivec2 i = uv >> 16;
    ivec4 top = mix4(texel(i), texel(i + ivec2(1, 0)), f.x);
    ivec4 bottom = mix4(texel(i + ivec2(0, 1)), texel(i + ivec2(1, 1)), f.x);
    return mix4(top, bottom, f.y);
}

ivec3 term(int which, ivec3 source, ivec3 destination) { return which == 0 ? source : which == 1 ? destination : ivec3(0); }

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    if (pixel.x < state.scissor.x || pixel.y < state.scissor.y || pixel.x > state.scissor.z || pixel.y > state.scissor.w) discard;

    ivec4 colour = ivec4(inColour);
    if (state.tex.x == 1) colour = min((sampleTexture() * colour) >> 7, ivec4(255));
    if (state.flags.x == 1) colour.a = 0x80;
    uint depth = uint(inDepth.x * 4096.0 + inDepth.y);

    beginInvocationInterlockARB();
    uint stored = imageLoad(depthImage, pixel).r;
    bool visible = state.misc.z == 1 || (state.misc.z == 2 && depth >= stored) || (state.misc.z == 3 && depth > stored);
    if (visible) {
        ivec4 result = colour;
        if (state.misc.x == 1) {
            ivec4 destination = ivec4(imageLoad(targetImage, pixel));
            int factor = state.blend.z == 0 ? colour.a : state.blend.z == 1 ? destination.a : state.misc.y;
            ivec3 a = term(state.blend.x, colour.rgb, destination.rgb);
            ivec3 b = term(state.blend.y, colour.rgb, destination.rgb);
            ivec3 d = term(state.blend.w, colour.rgb, destination.rgb);
            result.rgb = clamp((((a - b) * factor) >> 7) + d, ivec3(0), ivec3(255));
        }
        imageStore(targetImage, pixel, uvec4(result));
        if (state.misc.w == 1) imageStore(depthImage, pixel, uvec4(depth, 0u, 0u, 0u));
    }
    endInvocationInterlockARB();
}
```

- [ ] **Step 4: Write the renderer**

```cpp
// src/renderer/GsParityRenderer.hpp
#pragma once
#include "core/GpuDevice.hpp"
#include "scene/FrameDescription.hpp"
#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <vector>

// Draws native passes under the GS's rules: targets are storage images, and the fragment shader
// tests depth, blends in integers and writes inside an ordered pixel interlock.
class GsParityRenderer {
public:
    GsParityRenderer(const GpuDevice& gpu, const std::filesystem::path& shaderDirectory);
    ~GsParityRenderer();
    GsParityRenderer(const GsParityRenderer&) = delete;
    GsParityRenderer& operator=(const GsParityRenderer&) = delete;

    void setTarget(const std::string& id, uint32_t width, uint32_t height, std::span<const uint8_t> rgba);
    void setDepth(uint32_t width, uint32_t height, std::span<const uint32_t> depth);
    void setTexture(const std::string& id, uint32_t width, uint32_t height, std::span<const uint8_t> rgba);
    void draw(const scene::Pass& pass);
    std::vector<uint8_t> readTarget(const std::string& id);
    std::vector<uint32_t> readDepth();

private:
    struct Image { VkImage image{VK_NULL_HANDLE}; VkImageView view{VK_NULL_HANDLE}; VmaAllocation allocation{VK_NULL_HANDLE}; uint32_t width{0}, height{0}; VkFormat format{VK_FORMAT_UNDEFINED}; };
    struct Buffer { VkBuffer buffer{VK_NULL_HANDLE}; VmaAllocation allocation{VK_NULL_HANDLE}; void* mapped{nullptr}; };

    Image& place(std::map<std::string, Image>& where, const std::string& id, uint32_t width, uint32_t height, VkFormat format);
    Image createImage(uint32_t width, uint32_t height, VkFormat format);
    void destroyImage(Image& image);
    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage);
    void upload(const Image& image, const void* data, size_t bytes);
    std::vector<uint8_t> download(const Image& image, size_t bytes);
    void submit(const std::function<void(VkCommandBuffer)>& record);
    VkPipeline createPipeline(VkPrimitiveTopology topology, VkShaderModule vertex, VkShaderModule fragment);

    GpuDevice m_gpu;
    VkCommandPool m_pool{VK_NULL_HANDLE};
    VkCommandBuffer m_commands{VK_NULL_HANDLE};
    VkSampler m_sampler{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_setLayout{VK_NULL_HANDLE};
    VkPipelineLayout m_layout{VK_NULL_HANDLE};
    VkDescriptorPool m_descriptors{VK_NULL_HANDLE};
    VkDescriptorSet m_set{VK_NULL_HANDLE};
    VkPipeline m_triangles{VK_NULL_HANDLE}, m_lines{VK_NULL_HANDLE};
    std::map<std::string, Image> m_targets, m_textures;
    Image m_depth, m_blank;
};
```

```cpp
// src/renderer/GsParityRenderer.cpp
#include "renderer/GsParityRenderer.hpp"
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace {

struct DrawState { int32_t scissor[4], blend[4], misc[4], tex[4], texSize[4], addressU[4], addressV[4], flags[4]; };
static_assert(sizeof(DrawState) == 128);

struct GpuVertex { float x, y, depthHigh, depthLow, r, g, b, a, s, t, q, pad; };
static_assert(sizeof(GpuVertex) == 48);

void check(VkResult result, const char* what) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(what) + " failed");
}

VkShaderModule loadShader(VkDevice device, const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("no shader at " + path.string());
    std::vector<uint32_t> words(size_t(in.tellg()) / 4);
    in.seekg(0);
    in.read(reinterpret_cast<char*>(words.data()), std::streamsize(words.size() * 4));
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = words.size() * 4;
    info.pCode = words.data();
    VkShaderModule module;
    check(vkCreateShaderModule(device, &info, nullptr, &module), "shader module");
    return module;
}

GpuVertex convert(const scene::Vertex& v) {
    return {v.x, v.y, float(v.depth >> 12), float(v.depth & 0xfff), v.r, v.g, v.b, v.a, v.s, v.t, v.q, 0};
}

// A sprite is two corners; colour, depth and Q are the second vertex's.
void expandSprite(const scene::Vertex& a, const scene::Vertex& b, bool projective, const scene::Texture* texture, std::vector<GpuVertex>& out) {
    float s0 = a.s, t0 = a.t, s1 = b.s, t1 = b.t;
    if (projective && texture) {
        s0 = a.s / b.q * float(texture->width);
        t0 = a.t / b.q * float(texture->height);
        s1 = b.s / b.q * float(texture->width);
        t1 = b.t / b.q * float(texture->height);
    }
    auto corner = [&](float x, float y, float s, float t) {
        scene::Vertex v = b;
        v.x = x; v.y = y; v.s = s; v.t = t; v.q = 1;
        return convert(v);
    };
    const GpuVertex topLeft = corner(a.x, a.y, s0, t0), topRight = corner(b.x, a.y, s1, t0), bottomLeft = corner(a.x, b.y, s0, t1), bottomRight = corner(b.x, b.y, s1, t1);
    out.insert(out.end(), {topLeft, topRight, bottomLeft, topRight, bottomRight, bottomLeft});
}

}  // namespace

GsParityRenderer::GsParityRenderer(const GpuDevice& gpu, const std::filesystem::path& shaderDirectory) : m_gpu(gpu) {
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = gpu.queueFamily;
    check(vkCreateCommandPool(gpu.device, &pool, nullptr, &m_pool), "command pool");
    VkCommandBufferAllocateInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commands.commandPool = m_pool;
    commands.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commands.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(gpu.device, &commands, &m_commands), "command buffer");

    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    check(vkCreateSampler(gpu.device, &sampler, nullptr, &m_sampler), "sampler");

    const VkDescriptorSetLayoutBinding bindings[3] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo setLayout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setLayout.bindingCount = 3;
    setLayout.pBindings = bindings;
    check(vkCreateDescriptorSetLayout(gpu.device, &setLayout, nullptr, &m_setLayout), "set layout");

    const VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DrawState)};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &m_setLayout;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &range;
    check(vkCreatePipelineLayout(gpu.device, &layout, nullptr, &m_layout), "pipeline layout");

    const VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}};
    VkDescriptorPoolCreateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptors.maxSets = 1;
    descriptors.poolSizeCount = 2;
    descriptors.pPoolSizes = sizes;
    check(vkCreateDescriptorPool(gpu.device, &descriptors, nullptr, &m_descriptors), "descriptor pool");
    VkDescriptorSetAllocateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    set.descriptorPool = m_descriptors;
    set.descriptorSetCount = 1;
    set.pSetLayouts = &m_setLayout;
    check(vkAllocateDescriptorSets(gpu.device, &set, &m_set), "descriptor set");

    const VkShaderModule vertex = loadShader(gpu.device, shaderDirectory / "GsParity.vert.spv");
    const VkShaderModule fragment = loadShader(gpu.device, shaderDirectory / "GsParity.frag.spv");
    m_triangles = createPipeline(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, vertex, fragment);
    m_lines = createPipeline(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, vertex, fragment);
    vkDestroyShaderModule(gpu.device, vertex, nullptr);
    vkDestroyShaderModule(gpu.device, fragment, nullptr);

    m_blank = createImage(1, 1, VK_FORMAT_R8G8B8A8_UINT);
    const uint8_t zero[4] = {0, 0, 0, 0};
    upload(m_blank, zero, 4);
}

GsParityRenderer::~GsParityRenderer() {
    vkDeviceWaitIdle(m_gpu.device);
    for (auto& [id, image] : m_targets) destroyImage(image);
    for (auto& [id, image] : m_textures) destroyImage(image);
    destroyImage(m_depth);
    destroyImage(m_blank);
    vkDestroyPipeline(m_gpu.device, m_triangles, nullptr);
    vkDestroyPipeline(m_gpu.device, m_lines, nullptr);
    vkDestroyDescriptorPool(m_gpu.device, m_descriptors, nullptr);
    vkDestroyPipelineLayout(m_gpu.device, m_layout, nullptr);
    vkDestroyDescriptorSetLayout(m_gpu.device, m_setLayout, nullptr);
    vkDestroySampler(m_gpu.device, m_sampler, nullptr);
    vkDestroyCommandPool(m_gpu.device, m_pool, nullptr);
}

VkPipeline GsParityRenderer::createPipeline(VkPrimitiveTopology topology, VkShaderModule vertex, VkShaderModule fragment) {
    const VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr},
    };
    const VkVertexInputBindingDescription binding{0, sizeof(GpuVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attributes[4] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuVertex, x)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuVertex, depthHigh)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuVertex, r)},
        {3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuVertex, s)},
    };
    VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    input.vertexBindingDescriptionCount = 1;
    input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = 4;
    input.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = topology;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    const VkDynamicState dynamics[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamics;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = m_layout;
    VkPipeline pipeline;
    check(vkCreateGraphicsPipelines(m_gpu.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline), "pipeline");
    return pipeline;
}

void GsParityRenderer::submit(const std::function<void(VkCommandBuffer)>& record) {
    check(vkResetCommandBuffer(m_commands, 0), "reset");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(m_commands, &begin), "begin");
    record(m_commands);
    check(vkEndCommandBuffer(m_commands), "end");
    VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    info.commandBufferCount = 1;
    info.pCommandBuffers = &m_commands;
    check(vkQueueSubmit(m_gpu.queue, 1, &info, VK_NULL_HANDLE), "submit");
    check(vkQueueWaitIdle(m_gpu.queue), "wait");
}

GsParityRenderer::Image GsParityRenderer::createImage(uint32_t width, uint32_t height, VkFormat format) {
    Image image;
    image.width = width;
    image.height = height;
    image.format = format;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO;
    check(vmaCreateImage(m_gpu.allocator, &info, &allocation, &image.image, &image.allocation, nullptr), "image");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = image.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    check(vkCreateImageView(m_gpu.device, &view, nullptr, &image.view), "image view");

    // Every image stays in the general layout: it is stored to, fetched from and copied.
    submit([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.image = image.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dependency);
    });
    return image;
}

void GsParityRenderer::destroyImage(Image& image) {
    if (image.view) vkDestroyImageView(m_gpu.device, image.view, nullptr);
    if (image.image) vmaDestroyImage(m_gpu.allocator, image.image, image.allocation);
    image = {};
}

GsParityRenderer::Buffer GsParityRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage) {
    Buffer buffer;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO;
    allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo made{};
    check(vmaCreateBuffer(m_gpu.allocator, &info, &allocation, &buffer.buffer, &buffer.allocation, &made), "buffer");
    buffer.mapped = made.pMappedData;
    return buffer;
}

void GsParityRenderer::upload(const Image& image, const void* data, size_t bytes) {
    Buffer staging = createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    std::memcpy(staging.mapped, data, bytes);
    vmaFlushAllocation(m_gpu.allocator, staging.allocation, 0, VK_WHOLE_SIZE);
    submit([&](VkCommandBuffer cmd) {
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {image.width, image.height, 1};
        vkCmdCopyBufferToImage(cmd, staging.buffer, image.image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    });
    vmaDestroyBuffer(m_gpu.allocator, staging.buffer, staging.allocation);
}

std::vector<uint8_t> GsParityRenderer::download(const Image& image, size_t bytes) {
    Buffer staging = createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    submit([&](VkCommandBuffer cmd) {
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {image.width, image.height, 1};
        vkCmdCopyImageToBuffer(cmd, image.image, VK_IMAGE_LAYOUT_GENERAL, staging.buffer, 1, &region);
    });
    vmaInvalidateAllocation(m_gpu.allocator, staging.allocation, 0, VK_WHOLE_SIZE);
    std::vector<uint8_t> out(bytes);
    std::memcpy(out.data(), staging.mapped, bytes);
    vmaDestroyBuffer(m_gpu.allocator, staging.buffer, staging.allocation);
    return out;
}

GsParityRenderer::Image& GsParityRenderer::place(std::map<std::string, Image>& where, const std::string& id, uint32_t width, uint32_t height, VkFormat format) {
    Image& image = where[id];
    if (image.width != width || image.height != height) {
        destroyImage(image);
        image = createImage(width, height, format);
    }
    return image;
}

void GsParityRenderer::setTarget(const std::string& id, uint32_t width, uint32_t height, std::span<const uint8_t> rgba) {
    if (rgba.size() != size_t(width) * height * 4) throw std::runtime_error("target " + id + ": wrong number of bytes");
    upload(place(m_targets, id, width, height, VK_FORMAT_R8G8B8A8_UINT), rgba.data(), rgba.size());
}

void GsParityRenderer::setTexture(const std::string& id, uint32_t width, uint32_t height, std::span<const uint8_t> rgba) {
    if (rgba.size() != size_t(width) * height * 4) throw std::runtime_error("texture " + id + ": wrong number of bytes");
    upload(place(m_textures, id, width, height, VK_FORMAT_R8G8B8A8_UINT), rgba.data(), rgba.size());
}

void GsParityRenderer::setDepth(uint32_t width, uint32_t height, std::span<const uint32_t> depth) {
    if (depth.size() != size_t(width) * height) throw std::runtime_error("depth: wrong number of values");
    if (m_depth.width != width || m_depth.height != height) {
        destroyImage(m_depth);
        m_depth = createImage(width, height, VK_FORMAT_R32_UINT);
    }
    upload(m_depth, depth.data(), depth.size() * 4);
}

std::vector<uint8_t> GsParityRenderer::readTarget(const std::string& id) {
    const Image& image = m_targets.at(id);
    return download(image, size_t(image.width) * image.height * 4);
}

std::vector<uint32_t> GsParityRenderer::readDepth() {
    const std::vector<uint8_t> bytes = download(m_depth, size_t(m_depth.width) * m_depth.height * 4);
    std::vector<uint32_t> out(bytes.size() / 4);
    std::memcpy(out.data(), bytes.data(), bytes.size());
    return out;
}

void GsParityRenderer::draw(const scene::Pass& pass) {
    if (!pass.skip.empty()) throw std::logic_error(pass.name + " cannot be drawn: " + pass.skip);
    const auto target = m_targets.find(pass.target);
    if (target == m_targets.end()) throw std::runtime_error(pass.name + ": no target " + pass.target);
    if (!m_depth.image) throw std::runtime_error(pass.name + ": no depth buffer set");

    const Image* texture = &m_blank;
    if (pass.texture) {
        if (pass.texture->sourceIsTarget && pass.texture->source == pass.target) throw std::runtime_error(pass.name + " reads the target it draws into");
        const auto& where = pass.texture->sourceIsTarget ? m_targets : m_textures;
        const auto found = where.find(pass.texture->source);
        if (found == where.end()) throw std::runtime_error(pass.name + ": no texture " + pass.texture->source);
        texture = &found->second;
    }

    const bool sprites = pass.primitive == scene::Primitive::Sprites;
    const bool projective = pass.texture && pass.texture->coordinates == scene::Coordinates::Projective;
    std::vector<GpuVertex> vertices;
    if (sprites) {
        if (pass.vertices.size() % 2) throw std::runtime_error(pass.name + ": odd number of sprite corners");
        for (size_t i = 0; i < pass.vertices.size(); i += 2) expandSprite(pass.vertices[i], pass.vertices[i + 1], projective, pass.texture ? &*pass.texture : nullptr, vertices);
    } else {
        for (const scene::Vertex& v : pass.vertices) vertices.push_back(convert(v));
    }
    if (vertices.empty()) return;

    DrawState state{};
    state.scissor[0] = pass.scissor.x0; state.scissor[1] = pass.scissor.y0; state.scissor[2] = pass.scissor.x1; state.scissor[3] = pass.scissor.y1;
    if (pass.blend) {
        state.blend[0] = int(pass.blend->a); state.blend[1] = int(pass.blend->b); state.blend[2] = int(pass.blend->c); state.blend[3] = int(pass.blend->d);
        state.misc[0] = 1;
        state.misc[1] = pass.blend->fixed;
    }
    state.misc[2] = int(pass.depth.test);
    state.misc[3] = pass.depth.write ? 1 : 0;
    if (pass.texture) {
        const scene::Texture& t = *pass.texture;
        state.tex[0] = 1;
        state.tex[1] = (projective && !sprites) ? 1 : 0;
        state.tex[2] = int(t.filter);
        state.tex[3] = t.alpha.constant ? 1 : 0;
        state.texSize[0] = int(t.width); state.texSize[1] = int(t.height); state.texSize[2] = t.alpha.value; state.texSize[3] = t.alpha.zeroWhenBlack ? 1 : 0;
        state.addressU[0] = int(t.addressU.mode); state.addressU[1] = t.addressU.min; state.addressU[2] = t.addressU.max;
        state.addressV[0] = int(t.addressV.mode); state.addressV[1] = t.addressV.min; state.addressV[2] = t.addressV.max;
    }
    state.flags[0] = pass.antialias ? 1 : 0;
    state.flags[1] = int(target->second.width);
    state.flags[2] = int(target->second.height);

    const VkDescriptorImageInfo images[3] = {
        {VK_NULL_HANDLE, target->second.view, VK_IMAGE_LAYOUT_GENERAL},
        {VK_NULL_HANDLE, m_depth.view, VK_IMAGE_LAYOUT_GENERAL},
        {m_sampler, texture->view, VK_IMAGE_LAYOUT_GENERAL},
    };
    VkWriteDescriptorSet writes[3]{};
    for (uint32_t i = 0; i < 3; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = m_set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = i < 2 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &images[i];
    }
    vkUpdateDescriptorSets(m_gpu.device, 3, writes, 0, nullptr);

    Buffer buffer = createBuffer(vertices.size() * sizeof(GpuVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    std::memcpy(buffer.mapped, vertices.data(), vertices.size() * sizeof(GpuVertex));
    vmaFlushAllocation(m_gpu.allocator, buffer.allocation, 0, VK_WHOLE_SIZE);

    const VkExtent2D extent{target->second.width, target->second.height};
    submit([&](VkCommandBuffer cmd) {
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea = {{0, 0}, extent};
        rendering.layerCount = 1;
        vkCmdBeginRendering(cmd, &rendering);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pass.primitive == scene::Primitive::Lines ? m_lines : m_triangles);
        const VkViewport viewport{0, 0, float(extent.width), float(extent.height), 0, 1};
        const VkRect2D scissor{{0, 0}, extent};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DrawState), &state);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 0, 1, &m_set, 0, nullptr);
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &buffer.buffer, &offset);
        vkCmdDraw(cmd, uint32_t(vertices.size()), 1, 0, 0);
        vkCmdEndRendering(cmd);
    });
    vmaDestroyBuffer(m_gpu.allocator, buffer.buffer, buffer.allocation);
}
```

- [ ] **Step 5: Build and run the test**

Run: `cmake -S . -B build; cmake --build build --config Debug --target RendererTest; ctest --test-dir build -C Debug -R RendererTest --output-on-failure`
Expected: `100% tests passed`, no `validation:` line.

Each check that fails names a rule to settle here, before any captured frame:
- the sprite or the watertight triangles: coverage (the half-pixel shift in `GsParity.vert`; `GSRasterizer::DrawSprite`, `DrawTriangle` for the rule);
- the stack of 64: ordering. If the count is below 64 with interlock on, record the driver's behaviour in the spec and build the fallback: one `submit` per primitive for passes that blend (split in `draw`), then rerun;
- a validation error about a draw with no attachments: give the pipeline and `VkRenderingInfo` the depth image's size as `renderArea` only (it is already so) and read the message for the exact rule it names.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/renderer/GsParityRenderer.hpp src/renderer/GsParityRenderer.cpp shaders/GsParity.vert shaders/GsParity.frag tests/RendererTest.cpp
git commit -m "Feat(Renderer): Add the parity renderer with shader blending"
```

---

### Task 7: Parity tool and the picture of the difference

**Files:**
- Create: `tools/ParityTool/main.cpp`, `tools/parity/side_by_side.mjs`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `parity::loadFixture`, `parity::compare`, `parity::differenceImage`, `GsParityRenderer`, `HeadlessContext`.
- Produces: `bin/ParityTool.exe <fixture dir> <out dir> <shader dir>` writing `<out>/report.json`:

```
{ capture, frame,
  passes: [{ index, name, group, skipped, colour: { pixels, differing, largest, buckets[5] }, depth: {...} }],
  groups: [{ group, passes, colour, depth }],
  chained: [{ target, colour }], chainedDepth: {...},
  images: [{ name, width, height, ours, oracle, difference }] }      // raw RGBA files beside the report
```

Exit code 0 always when it ran (the gate comes in Task 10); 1 on an error.
`side_by_side.mjs <out dir>` writes `<out>/<name>.png` for each entry of `images`: ours, oracle, difference side by side.

- [ ] **Step 1: Write the tool**

```cpp
// tools/ParityTool/main.cpp
#include "core/HeadlessContext.hpp"
#include "parity/Compare.hpp"
#include "parity/Fixture.hpp"
#include "renderer/GsParityRenderer.hpp"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <fstream>
#include <map>

namespace {

std::string groupOf(const scene::Pass& pass) {
    static const char* primitives[] = {"triangles", "sprites", "lines"};
    static const char* tests[] = {"never", "always", "gequal", "greater"};
    static const char* terms[] = {"Cs", "Cd", "0"};
    static const char* factors[] = {"As", "Ad", "FIX"};
    static const char* modes[] = {"repeat", "clamp", "region-clamp", "region-repeat"};
    std::string group = primitives[int(pass.primitive)];
    if (pass.texture) {
        group += std::string(" tex(") + (pass.texture->sourceIsTarget ? "target" : "image") + "," + (pass.texture->coordinates == scene::Coordinates::Texel ? "texel" : "projective") + "," +
                 modes[int(pass.texture->addressU.mode)] + "," + (pass.texture->filter == scene::Filter::Bilinear ? "bilinear" : "nearest") + ")";
    } else {
        group += " flat";
    }
    if (pass.blend) group += std::string(" (") + terms[int(pass.blend->a)] + "-" + terms[int(pass.blend->b)] + ")*" + factors[int(pass.blend->c)] + "+" + terms[int(pass.blend->d)];
    else group += " opaque";
    if (pass.antialias) group += " aa";
    group += std::string(" z-") + tests[int(pass.depth.test)];
    return group;
}

nlohmann::json toJson(const parity::Difference& d) {
    return {{"pixels", d.pixels}, {"differing", d.differing}, {"largest", d.largest}, {"buckets", d.buckets}};
}

void writeRaw(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) { std::fprintf(stderr, "usage: ParityTool <fixture dir> <out dir> <shader dir>\n"); return 1; }
    try {
        const parity::Fixture fixture = parity::loadFixture(argv[1]);
        const std::filesystem::path out = argv[2];
        std::filesystem::create_directories(out);
        HeadlessContext context(true);
        GsParityRenderer renderer(context.gpu(), argv[3]);
        for (const auto& [id, image] : fixture.textures) renderer.setTexture(id, image.width, image.height, image.rgba);

        nlohmann::json report{{"passes", nlohmann::json::array()}, {"images", nlohmann::json::array()}};
        auto keep = [&](const std::string& name, const parity::Image& ours, const parity::Image& oracle) {
            writeRaw(out / (name + ".ours.rgba"), ours.rgba);
            writeRaw(out / (name + ".oracle.rgba"), oracle.rgba);
            writeRaw(out / (name + ".difference.rgba"), parity::differenceImage(ours, oracle, 16).rgba);
            report["images"].push_back({{"name", name}, {"width", ours.width}, {"height", ours.height},
                {"ours", name + ".ours.rgba"}, {"oracle", name + ".oracle.rgba"}, {"difference", name + ".difference.rgba"}});
        };

        // Isolated: each pass starts from the oracle's buffers before it, so a difference belongs to that pass alone.
        std::map<std::string, parity::Image> state = fixture.targetStart;
        parity::DepthImage depth = fixture.startDepth();
        struct Group { uint32_t passes{0}; parity::Difference colour, depth; };
        std::map<std::string, Group> groups;
        size_t worst = 0;
        uint64_t worstDiffering = 0;
        for (size_t i = 0; i < fixture.frame.passes.size(); i++) {
            const scene::Pass& pass = fixture.frame.passes[i];
            const parity::Image oracle = fixture.oracleColour(i);
            const parity::DepthImage oracleDepth = fixture.oracleDepth(i);
            nlohmann::json entry{{"index", pass.index}, {"name", pass.name}, {"group", groupOf(pass)}, {"skipped", pass.skip}};
            if (pass.skip.empty()) {
                for (const auto& [id, image] : state) renderer.setTarget(id, image.width, image.height, image.rgba);
                renderer.setDepth(depth.width, depth.height, depth.depth);
                renderer.draw(pass);
                const parity::Image ours{oracle.width, oracle.height, renderer.readTarget(pass.target)};
                const parity::DepthImage oursDepth{oracleDepth.width, oracleDepth.height, renderer.readDepth()};
                const parity::Difference colour = parity::compare(ours, oracle), z = parity::compare(oursDepth, oracleDepth);
                entry["colour"] = toJson(colour);
                entry["depth"] = toJson(z);
                Group& group = groups[groupOf(pass)];
                group.passes++;
                group.colour += colour;
                group.depth += z;
                if (colour.differing > worstDiffering) { worstDiffering = colour.differing; worst = i; }
            }
            report["passes"].push_back(std::move(entry));
            state[pass.target] = oracle;
            depth = oracleDepth;
        }
        report["groups"] = nlohmann::json::array();
        for (const auto& [name, group] : groups) report["groups"].push_back({{"group", name}, {"passes", group.passes}, {"colour", toJson(group.colour)}, {"depth", toJson(group.depth)}});

        if (worstDiffering) {
            const scene::Pass& pass = fixture.frame.passes[worst];
            std::map<std::string, parity::Image> before = fixture.targetStart;
            parity::DepthImage depthBefore = fixture.startDepth();
            for (size_t i = 0; i < worst; i++) { before[fixture.frame.passes[i].target] = fixture.oracleColour(i); depthBefore = fixture.oracleDepth(i); }
            for (const auto& [id, image] : before) renderer.setTarget(id, image.width, image.height, image.rgba);
            renderer.setDepth(depthBefore.width, depthBefore.height, depthBefore.depth);
            renderer.draw(pass);
            const parity::Image oracle = fixture.oracleColour(worst);
            keep("worst-" + pass.name, parity::Image{oracle.width, oracle.height, renderer.readTarget(pass.target)}, oracle);
        }

        // Chained: the whole frame from the start buffers, as the application will draw it.
        for (const auto& [id, image] : fixture.targetStart) renderer.setTarget(id, image.width, image.height, image.rgba);
        const parity::DepthImage startDepth = fixture.startDepth();
        renderer.setDepth(startDepth.width, startDepth.height, startDepth.depth);
        uint32_t skipped = 0;
        for (size_t i = 0; i < fixture.frame.passes.size(); i++) {
            const scene::Pass& pass = fixture.frame.passes[i];
            if (pass.skip.empty()) { renderer.draw(pass); continue; }
            const parity::Image oracle = fixture.oracleColour(i);
            const parity::DepthImage oracleDepth = fixture.oracleDepth(i);
            renderer.setTarget(pass.target, oracle.width, oracle.height, oracle.rgba);
            renderer.setDepth(oracleDepth.width, oracleDepth.height, oracleDepth.depth);
            skipped++;
        }
        report["chained"] = nlohmann::json::array();
        for (const auto& [id, oracle] : state) {
            const parity::Image ours{oracle.width, oracle.height, renderer.readTarget(id)};
            report["chained"].push_back({{"target", id}, {"colour", toJson(parity::compare(ours, oracle))}});
            keep("chained-" + id, ours, oracle);
        }
        report["chainedDepth"] = toJson(parity::compare(parity::DepthImage{depth.width, depth.height, renderer.readDepth()}, depth));
        report["skipped"] = skipped;
        report["validationErrors"] = context.validationErrors();

        std::ofstream(out / "report.json") << report.dump(1);
        std::printf("%-78s %6s %9s %9s %5s %9s\n", "group", "passes", "pixels", "differ", "max", "depth");
        for (const auto& [name, group] : groups) {
            std::printf("%-78s %6u %9llu %9llu %5u %9llu\n", name.c_str(), group.passes, (unsigned long long)group.colour.pixels,
                        (unsigned long long)group.colour.differing, group.colour.largest, (unsigned long long)group.depth.differing);
        }
        for (const auto& chained : report["chained"]) {
            std::printf("chained %s: %llu of %llu pixels differ, largest %u\n", chained["target"].get<std::string>().c_str(),
                        chained["colour"]["differing"].get<unsigned long long>(), chained["colour"]["pixels"].get<unsigned long long>(), chained["colour"]["largest"].get<unsigned>());
        }
        std::printf("skipped passes taken from the oracle: %u; validation errors: %u\n", skipped, context.validationErrors());
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "ParityTool: %s\n", error.what());
        return 1;
    }
}
```

Append to `CMakeLists.txt`:

```cmake
add_executable(ParityTool tools/ParityTool/main.cpp)
target_link_libraries(ParityTool PRIVATE ClockRenderer)
```

```js
// tools/parity/side_by_side.mjs
// The parity report's raw images as PNG files: ours, the oracle's and their difference, side by side.
//   node tools/parity/side_by_side.mjs <report dir>
import fs from 'node:fs';
import path from 'node:path';
import { png } from '../../References/scripts/extract_buffers.mjs';

const dir = process.argv[2];
if (!dir) { console.error('usage: node tools/parity/side_by_side.mjs <report dir>'); process.exit(2); }
const report = JSON.parse(fs.readFileSync(path.join(dir, 'report.json'), 'utf8'));
for (const image of report.images) {
  const { width, height } = image;
  const panels = [image.ours, image.oracle, image.difference].map((file) => fs.readFileSync(path.join(dir, file)));
  const out = Buffer.alloc(width * 3 * height * 4);
  for (let y = 0; y < height; y++) for (let p = 0; p < 3; p++) {
    panels[p].copy(out, (y * width * 3 + p * width) * 4, y * width * 4, (y + 1) * width * 4);
  }
  for (let i = 3; i < out.length; i += 4) out[i] = 255;
  fs.writeFileSync(path.join(dir, `${image.name}.png`), png(width * 3, height, out));
  console.log(`${image.name}.png`);
}
```

- [ ] **Step 2: Build and run on the fixture**

Run: `cmake -S . -B build; cmake --build build --config Debug --target ParityTool; bin/ParityTool.exe References/fixtures/hddosd-110U-whole3-clock/f0 References/fixtures/hddosd-110U-whole3-clock/f0/report bin/shaders; node tools/parity/side_by_side.mjs References/fixtures/hddosd-110U-whole3-clock/f0/report`
Expected: a table of about 17 groups with their differing pixel counts, three `chained fb....` lines, `skipped passes taken from the oracle: 26; validation errors: 0`, and four PNG names. The numbers are the first measurement; they are not expected to be zero yet.

- [ ] **Step 3: Record the first measurement and commit**

Save the table printed in Step 2 as `docs/superpowers/plans/2026-10-03-parity-baseline.md` under the heading `# Parity baseline, hddosd-110U-whole3-clock frame 0` (the table in a code block, one line saying which commit produced it).

```bash
git add CMakeLists.txt tools/ParityTool/main.cpp tools/parity/side_by_side.mjs docs/superpowers/plans/2026-10-03-parity-baseline.md
git commit -m "Feat(Renderer): Add the parity tool and its first measurement"
```

---

### Task 8: Bring each group to its rule

One group of the report at a time, in this order, each as its own cycle. The failing test is the group's line in the report; it passes when its `differ` is 0, or when what is left is shown to be interpolation (below).

**Files:**
- Modify: `shaders/GsParity.frag`, `shaders/GsParity.vert`, `src/renderer/GsParityRenderer.cpp`
- Modify: `tests/RendererTest.cpp` (one known-answer case per rule found)
- Modify: `docs/superpowers/plans/2026-10-03-parity-baseline.md` (one row per group: before, after, cause)

Order and where each rule is in the software renderer (`D:/CodingProjects/Watson/References/pcsx2/pcsx2/GS/Renderers/SW/`):

| # | Group | Rule to read | File, function |
|---|---|---|---|
| 1 | `sprites flat opaque` | sprite corners and coverage | `GSRasterizer.cpp` `DrawSprite` |
| 2 | `sprites tex(...,texel,...,bilinear) opaque` (the copies) | texel coordinates of a sprite, the half-texel shift, four-bit weights, constant alpha of a 24-bit texture | `GSRendererSW.cpp` `ConvertVertexBuffer` (`fst`); `GSDrawScanline.cpp` `CDrawScanline` (`sel.ltf`, `lerp16_4`), `CSetupPrim` |
| 3 | `sprites ... (Cs-0)*As+Cd` and `(Cs-Cd)*As+Cd` | modulate, blend, the alpha written | `CDrawScanline` (`modulate16`, the block after `sel.abe`) |
| 4 | `triangles tex(image,projective,repeat,bilinear) opaque z-gequal` (the background) | S/Q per pixel, colour interpolation, depth interpolation and its test | `ConvertVertexBuffer` (`q_div`), `GSRasterizer.cpp` `DrawTriangle`, `CSetupPrim`, `CDrawScanline` (`sel.zb`, `ztst`) |
| 5 | `triangles tex(image,...) (0-Cs)*As+Cd` and `(Cs-0)*As+Cd` (rod faces) | same as 3 and 4 together | as above |
| 6 | `triangles tex(image,texel,clamp,bilinear)` (orb sprites as strips) | clamp addressing | `CDrawScanline` (wrap of `uv`) |
| 7 | groups with `aa` | left for Task 9 | — |

- [ ] **Step 1: Take the first group whose `differ` is not 0**

Run: `bin/ParityTool.exe References/fixtures/hddosd-110U-whole3-clock/f0 References/fixtures/hddosd-110U-whole3-clock/f0/report bin/shaders`
Expected: the table; note the group's `differ` and `max`, and the first pass of that group in `report.json` (`passes[].group`).

- [ ] **Step 2: Find the rule**

Read the function the table names for that group. State the rule in one sentence in the baseline document, with the file and function it was read in. Decide which of two kinds the difference is:
- **arithmetic**: our shader computes a different formula (wrong shift, rounding, operand, order). It is fixed until `differ` is 0 on pixels whose inputs are constant across the primitive (flat colour, sprites).
- **interpolation**: the formula is the same and the inputs differ because the GPU interpolates colour, depth or coordinates in floating point. It is shown by the pixels differing only where an input varies, with `max` small on untextured gradients.

- [ ] **Step 3: Write the known-answer case**

Add to `tests/RendererTest.cpp` a pass with constant inputs that exercises the rule, with the result computed by hand from the rule (as the cases already there). Run `ctest --test-dir build -C Debug -R RendererTest --output-on-failure`.
Expected: FAIL at the new `CHECK`.

- [ ] **Step 4: Change the shader or the expansion to follow the rule**

Edit `shaders/GsParity.frag` (or `.vert`, or `expandSprite`). Rebuild: `cmake --build build --config Debug --target RendererTest ParityTool`.

- [ ] **Step 5: Run both**

Run: `ctest --test-dir build -C Debug --output-on-failure; bin/ParityTool.exe References/fixtures/hddosd-110U-whole3-clock/f0 References/fixtures/hddosd-110U-whole3-clock/f0/report bin/shaders`
Expected: all tests pass; the group's `differ` is 0, or lower with the rest explained as interpolation. No other group's `differ` went up.

- [ ] **Step 6: Record and commit**

Add the group's row to the baseline document (before, after, the rule, the kind). Commit:

```bash
git add shaders/GsParity.frag shaders/GsParity.vert src/renderer/GsParityRenderer.cpp tests/RendererTest.cpp docs/superpowers/plans/2026-10-03-parity-baseline.md
git commit -m "Fix(Shaders): Follow the GS rule for <the group, in words>"
```

- [ ] **Step 7: Repeat Steps 1 to 6 for the next group in the table**, until every group without `aa` has `differ` 0 or a recorded interpolation remainder.

If an interpolation remainder is large (more than 1% of a group's pixels, or `max` above 16 on a textured group), compute that input in the fragment shader from the primitive's three vertices instead of taking the varying: pass the three vertices' values `flat` and the barycentric weights `noperspective`, and evaluate in the order the software renderer does (`CSetupPrim`: the value at the scanline start plus the step times the pixel count). Record the cost in the baseline document.

---

### Task 9: Edge antialiasing, measured

**Files:**
- Modify: `docs/superpowers/plans/2026-10-03-parity-baseline.md`
- Modify (only if the decision below says so): `shaders/GsParity.frag`, `src/renderer/GsParityRenderer.cpp`, `tests/RendererTest.cpp`

The groups with `aa` are the orb trails (14 passes, line strips, depth `Greater`) and the rods' refracted faces (36 passes, triangles reading a work buffer). The software renderer draws their interior as usual with alpha 0x80, then their edges with a coverage value as alpha, blended (`GSRasterizer.cpp` `DrawEdgeTriangle`, `DrawEdgeLine`, `DrawEdge`; `GSDrawScanline.cpp` `CDrawEdge` and the `sel.aa1` block of `CDrawScanline`).

- [ ] **Step 1: Measure with edges ignored**

Run the tool as in Task 8 Step 1.
Expected: for each `aa` group, `differ`, `max` and the buckets. Write them in the baseline document, with, for the triangle groups, how many of the differing pixels lie on a primitive's boundary (count them from `worst-*.png` of one such pass, made by `side_by_side.mjs`).

- [ ] **Step 2: Read the rule and write it down**

In the baseline document, state from the three functions above: which pixels an edge pass touches, how coverage is computed from the edge's fractional position, how it enters the blend when the pass has no blend of its own, and what a line's interior is.

- [ ] **Step 3: Decide, with the numbers**

- If the differing pixels are only edge pixels and the side-by-side picture is indistinguishable at 1:1: record the group's remainder as the cause `edge antialiasing` and stop here.
- Otherwise implement it: a second draw of the pass's edges as lines (`m_lines`), with a new `flags.w = 1` that makes the fragment shader take coverage as the alpha and blend with `(Cs − Cd) × coverage + Cd`, coverage computed in the vertex stage from the edge's direction as the rule of Step 2 says. Write its known-answer case first (one horizontal edge at a fractional y: coverage is the fraction), then the shader, as in Task 8 Steps 3 to 6.

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/plans/2026-10-03-parity-baseline.md
git commit -m "Docs(Project): Record the measured cost of edge antialiasing"
```

(With `shaders/GsParity.frag`, `src/renderer/GsParityRenderer.cpp`, `tests/RendererTest.cpp` and the message `Feat(Shaders): Draw antialiased edges by the GS rule` if Step 3 implemented it.)

---

### Task 10: The whole frame, the budgets, the verdict

**Files:**
- Modify: `docs/superpowers/specs/2026-10-03-native-vulkan-renderer-design.md` (sections "Tolerance", "Renderer", "Risks")
- Modify: `tools/ParityTool/main.cpp` (the gate), `CMakeLists.txt` (the test)
- Create: `tools/ParityTool/budgets.json`

- [ ] **Step 1: Run the chained frame on three captures**

Make fixtures for two more captures and run the tool on the three:

```bash
node tools/parity/make_fixture.mjs D:/CodingProjects/Watson/Runtime/captures/hddosd-110U-whole3-config.gs References/fixtures/hddosd-110U-whole3-config/f0
node tools/parity/make_fixture.mjs D:/CodingProjects/Watson/Runtime/captures/hddosd-110U-whole2-to-clock.gs References/fixtures/hddosd-110U-whole2-to-clock/f0
```

then for each of the three fixtures: `bin/ParityTool.exe <fixture> <fixture>/report bin/shaders; node tools/parity/side_by_side.mjs <fixture>/report`
Expected: three reports. A fixture that fails to build because of a state the maker does not know is fixed in `describeState` (a new `skip` reason, or support) with its test updated.

- [ ] **Step 2: Write the budgets**

`tools/ParityTool/budgets.json`, from the largest values of the three reports, one entry per group, plus the chained frame:

```json
{
  "groups": { "<group name as printed>": { "differingShare": 0.0, "largest": 0 } },
  "chained": { "differingShare": 0.0, "largest": 0 },
  "depth": { "differingShare": 0.0, "largest": 0 }
}
```

A group whose measured `differ` is 0 gets 0 and 0. A group with a remainder gets its measured share rounded up to two significant digits and its measured `largest`.

- [ ] **Step 3: Make the tool a gate, test first**

Append to `CMakeLists.txt`:

```cmake
add_test(NAME ParityClock COMMAND ParityTool ${CLOCK_FIXTURE} ${CLOCK_FIXTURE}/report ${CMAKE_SOURCE_DIR}/bin/shaders ${CMAKE_SOURCE_DIR}/tools/ParityTool/budgets.json)
```

Run: `cmake -S . -B build; ctest --test-dir build -C Debug -R ParityClock --output-on-failure`
Expected: FAIL, `usage: ParityTool <fixture dir> <out dir> <shader dir>`

In `tools/ParityTool/main.cpp`: accept the optional fourth argument (`argc == 4 || argc == 5`); after the report is written, when a budgets file is given, load it and return 1, printing one line per breach, when: a group is not in the file; a group's `differing / pixels` or `largest` exceeds its budget; a chained target or the chained depth exceeds its budget; `validationErrors` is not 0.

```cpp
        if (argc == 5) {
            std::ifstream in(argv[4]);
            if (!in) throw std::runtime_error(std::string("no budgets at ") + argv[4]);
            const nlohmann::json budgets = nlohmann::json::parse(in);
            uint32_t breaches = 0;
            auto within = [&](const std::string& what, const parity::Difference& d, const nlohmann::json& budget) {
                const double share = d.pixels ? double(d.differing) / double(d.pixels) : 0.0;
                if (share > budget.at("differingShare").get<double>() || d.largest > budget.at("largest").get<uint32_t>()) {
                    std::printf("over budget: %s differs on %.6f of its pixels, largest %u\n", what.c_str(), share, d.largest);
                    breaches++;
                }
            };
            for (const auto& [name, group] : groups) {
                if (!budgets.at("groups").contains(name)) { std::printf("no budget for group: %s\n", name.c_str()); breaches++; continue; }
                within(name, group.colour, budgets.at("groups").at(name));
                within(name + " (depth)", group.depth, budgets.at("depth"));
            }
            for (const auto& [id, oracle] : state) within("chained " + id, parity::compare(parity::Image{oracle.width, oracle.height, renderer.readTarget(id)}, oracle), budgets.at("chained"));
            if (context.validationErrors()) { std::printf("validation errors: %u\n", context.validationErrors()); breaches++; }
            if (breaches) return 1;
        }
```

Run: `cmake --build build --config Debug --target ParityTool; ctest --test-dir build -C Debug --output-on-failure`
Expected: `100% tests passed` (FixtureTest, CompareTest, HeadlessTest, RendererTest, ParityClock).

- [ ] **Step 4: Write the results into the spec**

In `docs/superpowers/specs/2026-10-03-native-vulkan-renderer-design.md`:
- "Tolerance": replace the paragraph's last two sentences with a table, one row per cause (interpolation of colour; texel boundary; depth interpolation; edge antialiasing), giving the measured share of pixels and largest difference, and the capture each came from; say the gate is `tools/ParityTool/budgets.json`.
- "Renderer" table: correct each row's "Expected parity" to what was measured; add that `AA1` is on the rods' refracted faces as well as the trails.
- "Ordering inside a draw": state which method is used and that the 64-sprite test proves it.
- "Testing" and "Slices": the oracle is `pcsx2-gsrunner -dump rt,z,a,i` per draw; the decoder is `tools/parity/make_fixture.mjs`.
- "Risks": remove the ones answered; keep what stays open.

- [ ] **Step 5: Commit and hand the pictures over**

```bash
git add tools/ParityTool/main.cpp tools/ParityTool/budgets.json CMakeLists.txt tools/parity/make_fixture.mjs tools/parity/make_fixture.test.mjs docs/superpowers/specs/2026-10-03-native-vulkan-renderer-design.md docs/superpowers/plans/2026-10-03-parity-baseline.md
git commit -m "Feat(Renderer): Gate the clock frame on its measured budgets"
```

Report to Jean, with the three `chained-fb0000.png` paths (ours, oracle, difference side by side) and the budgets table: the verdict on "convincing" is his, by eye, and it decides whether slices 2 and 3 are planned on this renderer.
