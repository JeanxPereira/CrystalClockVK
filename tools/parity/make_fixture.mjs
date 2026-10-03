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
export function describeState(state, targetOfBlock, primitive) {
  const prim = fields(state, 'PRIM'), frame = fields(state, 'FRAME'), zbuf = fields(state, 'ZBUF');
  const test = fields(state, 'TEST'), alpha = fields(state, 'ALPHA'), scissor = fields(state, 'SCISSOR');
  const reasons = [];
  if (zbuf.PSM !== 0) reasons.push(`depth format 0x${hex(0x30 | zbuf.PSM, 2)}`);
  if (frame.PSM !== 0) reasons.push(`frame format 0x${hex(frame.PSM, 2)}`);
  if (frame.FBMSK !== 0) reasons.push('frame mask');
  if (test.ATE) reasons.push('alpha test');
  if (test.DATE) reasons.push('destination alpha test');
  if (prim.FGE) reasons.push('fog');
  if (fields(state, 'FBA').FBA) reasons.push('alpha correction');
  if (fields(state, 'PABE').PABE) reasons.push('per-pixel alpha blending');
  if (fields(state, 'DTHE').DTHE) reasons.push('dithering');
  if (!fields(state, 'COLCLAMP').CLAMP) reasons.push('colour wrap');

  // AA1 on a line or triangle forces the blend with ALPHA as it stands, even with ABE 0 (GSDrawScanline.cpp: abe || aa1).
  // With ABE 1, AA1 replaces the alpha only where it is 0x80 (GSDrawScanline.cpp); the renderer replaces it always.
  if (prim.ABE && prim.AA1 && primitive !== 'Sprites') reasons.push('antialiasing with alpha blending');
  if (primitive === 'Lines' && !prim.AA1) reasons.push('line without antialiasing');
  let blend = null;
  if (prim.ABE || (prim.AA1 && primitive !== 'Sprites')) {
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

function oracleContext(text) {
  const out = {};
  let section = null;
  for (const line of text.split(/\r?\n/)) {
    const head = /^([A-Z0-9_]+):\s*$/.exec(line);
    if (head) { section = out[head[1]] = {}; continue; }
    const item = /^ {4}([A-Z0-9]+):\s*(-?[\w.]+)/.exec(line);
    if (item && section) section[item[1]] = Number(item[2]);
  }
  return out;
}

const COMPARED = {
  FRAME: ['FBP', 'FBW', 'PSM', 'FBMSK'], TEST: ['ZTE', 'ZTST', 'ATE', 'DATE'], ALPHA: ['A', 'B', 'C', 'D', 'FIX'],
  SCISSOR: ['SCAX0', 'SCAX1', 'SCAY0', 'SCAY1'], ZBUF: ['PSM', 'ZMSK'], PRIM: ['IIP', 'TME', 'ABE', 'AA1', 'FST', 'FGE'],
};
const COMPARED_TEXTURE = { TEX0: ['TBP0', 'TBW', 'PSM', 'TW', 'TH', 'TFX', 'TCC'], CLAMP: ['WMS', 'WMT', 'MINU', 'MAXU', 'MINV', 'MAXV'], TEX1: ['MMAG', 'MMIN'], TEXA: ['AEM', 'TA0'] };

/** The first disagreement between the oracle's printed context and a stream state, or null. */
function disagreement(context, state) {
  const tables = { ...COMPARED, ...(context.PRIM?.TME ? COMPARED_TEXTURE : {}) };
  for (const [name, keys] of Object.entries(tables)) {
    const decoded = fields(state, name);
    for (const key of keys) {
      const wanted = context[name]?.[key];
      const got = name === 'FRAME' && key === 'FBP' ? decoded.FBP * 32 : name === 'ZBUF' && key === 'PSM' ? 0x30 | decoded.PSM : decoded[key];
      if (wanted === undefined) return `${name}.${key} is not in the oracle context`;
      if (wanted !== Number(got)) return `${name}.${key} is ${wanted} in the oracle and ${got} in the dump`;
    }
  }
  return null;
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
    const context = oracleContext(fs.readFileSync(path.join(oracleDir, `${draw}_context.txt`), 'utf8'));
    const found = [];
    for (const primitive of wanted.primitives) {
      let why = null;
      while (cursor < stream.length) {
        if (same(primitive, stream[cursor].vertices)) {
          why = disagreement(context, stream[cursor].state);
          if (!why) break;
        }
        cursor++; dropped++;
      }
      if (cursor === stream.length) throw new Error(`draw ${draw}: primitive ${found.length} of the oracle is not in the dump's stream with its state${why ? ` (nearest geometry: ${why})` : ''}`);
      found.push(stream[cursor++]);
    }
    const key = JSON.stringify(found[0].state);
    if (!found.every((p) => JSON.stringify(p.state) === key)) throw new Error(`draw ${draw}: its primitives do not share one state`);
    matched.push({ draw, primitive: wanted.primitive, found, state: found[0].state });
  }

  const left = stream.length - cursor;

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
    for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
      const at = (word32(block, pages, x, y) % 0x100000) * 4;
      vram.copy(out, (y * width + x) * 4, at, at + 4);
    }
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
    const state = describeState(m.state, targetOfBlock, m.primitive);
    field ||= state.field;
    const flat = !state.smooth || m.primitive === 'Sprites';
    if (state.smooth && m.primitive === 'Lines') state.skip ||= 'gouraud line';
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
  return { passes: passes.length, skipped: passes.filter((p) => p.skip).length, dropped, left };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const [dump, outDir, frame] = process.argv.slice(2);
  if (!dump || !outDir) { console.error('usage: node tools/parity/make_fixture.mjs <dump.gs> <out dir> [frame]'); process.exit(2); }
  const done = await makeFixture(dump, outDir, Number(frame ?? 0));
  console.log(`fixture: ${done.passes} passes (${done.skipped} skipped), ${done.dropped} primitives the oracle culled, ${done.left} left after the last draw, in ${outDir}`);
}
