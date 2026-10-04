// The scene fixture: for every frame of a whole-frame capture, the clock's named state at the
// frame function's entry and every value the JS model (References/model/clock_*.mjs) computes from
// it, run as References/scripts/verify_frame.mjs --carry runs it (state carried from the first
// frame, only its EXTERNAL inputs taken from each snapshot). The native ports are tested against it
// bit for bit. Refuses to write when the model does not reproduce the capture.
//
// CLOCK_BUILD=hdd node tools/scene/export_fixture.mjs <trace.jsonl> [out.json]
//   default out: D:/CodingProjects/CrystalClockVK/References/fixtures/<capture>/scene.json
//
// Schema. Floats are their 32-bit pattern as "0x" and 8 hex digits; every number is an integer;
// a matrix is 4 rows of 4 (rows as the model holds them); a colour is [r, g, b, a].
// { capture, build: "hdd", frames: [ {
//   index,
//   input: the state at the frame's entry, one key per piece of clock_memory.mjs (decoders: PIECES
//          in instrument.mjs): time {ms, seconds, minutes, hours}, eased {secondHand, hourHand,
//          progress, fraction}, state {currentRod, secondsAngle, rodAngle, rods[12] {appearance,
//          progress, base, reflection}, accent, fourth, progressTarget, accentFrame, currentTarget},
//          rings[7] {head, count, full, entries[50] {x, y, z, colour}}, template (a rod record),
//          ramps {length, counter, changed, state}, rectangle records {colour, x0, y0, u0, v0, x1,
//          y1, u1, v1, z, blend, textured}, ...
//   expect: {
//     drawOrder: ["rod 5", "orb 3", ...]          the draw list, deepest first
//     camera: { screen, view, cameraOffset }      cameraOffset: after the frame's decay
//     rods[12], in draw order: { number, record (the rod record handed to transform), local,
//       matrix (local through the view), centre {cx, cy, cz}, pieces [{piece "A"|"B", record,
//       faces}] (a split rod), faces[record.faces]: { index, flag, normal, edge, vertices[4] {eye,
//       screen, q, s, t, x, y, z, f}, refracted[], textured[], reflected[] } }
//       each emitter call: { phase "rod"|"extra pass 0"|"extra pass 1", piece "whole"|"A"|"B",
//       refracted: extra, centre [cx, cy]; textured: colour, ds, dt; reflected: colour;
//       prim, vertices[4] } with the GS values sent: x, y (12.4), z (24 bits), f, u, v, r, g, b, a,
//       q, s, t (each as its register holds it)
//     orbs[7], by orb number k: { k, drawn (place among the orbs drawn), centre {cx, cy, cz},
//       position [x, y, z], colour, ring (after the frame), trail[2] {header, prim, vertices},
//       sprites[2] {glow, disc}: [0] to the frame, [1] to the refraction buffer }
//     head: { background[] (strips), blur[], copies[], tint, vignette[], fade, blurAfter[] (the
//       trips after the rods), bars[], column }: each a draw { label, prim, vertices }
//     after: the state the frame leaves, decoded as `input`
//     text: { text[], hint[], pages[] } the strings of the date and time, the button hint and the menu pages
//     stages: { cubes, menuStep, menus, endOfFrame }, each { before, after } (STAGE_PIECES of instrument.mjs;
//       menus also notes), null for a stage the model did not call
//     cubes[]: the packets cubes() sent that hold vertices { label, prim, fbp (FRAME_1's page), vertices }
//     listFade: { first, second, secondIndex } at the list function's probe, when the frame draws the list
//   } , between: null for frame 0, else { before, after, notes } around the clock thread's step (between())
//   input also: the menus' pieces (pad, configPage, configEntries[9], mainMenu, cubeList, ...) and listFade
//   } } ] }
import fs from 'node:fs';
import path from 'node:path';

process.env.CLOCK_BUILD ??= 'hdd';
if (process.env.CLOCK_BUILD !== 'hdd') throw new Error('the scene fixture is HDD OSD 1.10U only (CLOCK_BUILD=hdd)');

const { install, REFERENCES } = await import('./instrument.mjs');

/**
 * What the alpha rules of the date, time and button hint read (verify_text2.mjs dateAlpha and panelsOf, HDD OSD
 * 1.10U), from the text probes at func_00230008 and func_002269E0 that precede the frame's date and hint strings:
 * ramps as { length, counter, changed, state }, words as integers. The menus' code writes them; the clock screen
 * only reads them. Written into each frame's input as `textRamps`.
 */
async function addTextRamps(traceFile, frames) {
  const { readTraceFor } = await import(`${REFERENCES}lib/trace.mjs`);
  const { PROBES } = await import(`${REFERENCES}scripts/verify_text2.mjs`);
  const trace = readTraceFor(traceFile, PROBES);
  const probes = trace.probes.filter((probe) => !probe.preroll);
  const before = (pc, at) => probes.filter((probe) => probe.pc === pc && probe.at <= at).at(-1);
  const reader = (probe) => {
    const word = (address) => {
      for (const m of probe.mem) if (m.bytes && address >= m.address && address + 4 <= m.address + m.bytes.length) return m.bytes.readInt32LE(address - m.address);
      throw new Error(`0x${(address >>> 0).toString(16)} was not probed at 0x${probe.pc.toString(16)}`);
    };
    return { word, ramp: (address) => ({ length: word(address), counter: word(address + 4), changed: word(address + 8), state: word(address + 12) }) };
  };
  for (const frame of frames) {
    const text = frame.expect.text;
    if (!text?.text.length || !text.hint.length) continue;
    const date = reader(before(0x00230008, text.text[0].at)), panels = reader(before(0x002269e0, text.hint[0].at));
    const index = panels.word(0x002b2ff8);
    frame.input.textRamps = {
      config: date.ramp(0x002b2e04), dialogClosing: date.ramp(0x002b46b8), firstRun: date.ramp(0x002b46d0), lead: date.word(0x003702e0),
      body: date.word(0x003702cc), tail: date.word(0x003702d0), weight: date.word(0x00370ab8),
      mainMenu: panels.ramp(0x002b2e78), version: panels.ramp(0x002b3000), menu: panels.ramp(0x002b5780), dialog: panels.ramp(panels.word(0x003701c0) + 0x1c),
      panel7: panels.word(0x002b2e00), panel8On: panels.word(0x00370140), panel8: panels.word(0x0037013c), adjustRow: panels.word(panels.word(0x002b2fec) + index * 16 + 8),
      panelConfig: panels.ramp(0x002b2e04), panelLead: panels.word(0x003702e0), panelBody: panels.word(0x003702cc), panelTail: panels.word(0x003702d0), panelWeight: panels.word(0x00370ab8),
    };
  }
}

/**
 * The list entries' crossfade (D_0037029C first, D_003702A0 second, D_003702A4 second's index; writers func_002316B8
 * and func_00230FD8, CrystalOSD/asm), read at the list function's probe (0x00231388, verify_text2 PROBES, gp words
 * 0x00370130 + 0x1C0). expect.listFade: the frame's own words (after func_00230FD8). input.listFade: the frame
 * before's; frame 0 starts at rest: func_00230FD8 saturates first to 128 and second to 0 within 16 frames, so a
 * capture that starts at rest starts at (128, 0, index).
 */
async function addListFade(traceFile, frames) {
  const { readTraceFor } = await import(`${REFERENCES}lib/trace.mjs`);
  const { PROBES } = await import(`${REFERENCES}scripts/verify_text2.mjs`);
  const probes = readTraceFor(traceFile, PROBES).probes.filter((p) => !p.preroll);
  const word = (probe, address) => { for (const m of probe.mem) if (m.bytes && address >= m.address && address + 4 <= m.address + m.bytes.length) return m.bytes.readInt32LE(address - m.address); return null; };
  let last = null;
  frames.forEach((frame) => {
    const pages = frame.expect.text?.pages ?? [];
    const list = pages.length ? probes.filter((p) => p.pc === 0x00231388 && p.at <= pages.at(-1).at).at(-1) : null;
    const fade = list ? { first: word(list, 0x0037029c), second: word(list, 0x003702a0), secondIndex: word(list, 0x003702a4) } : null;
    if (frame.index === 0) {
      if (fade && (fade.first !== 128 || fade.second !== 0)) throw new Error(`${traceFile}: the list fade moves at frame 0`);
      frame.input.listFade = fade ?? { first: 128, second: 0, secondIndex: 0 };
    } else if (last) frame.input.listFade = last;
    if (fade) { frame.expect.listFade = fade; last = fade; }
  });
}

/** The frames of `traceFile` as the fixture's JSON text. */
export async function exportScene(traceFile) {
  const recording = await install();
  const { verify } = await import(`${REFERENCES}scripts/verify_frame.mjs`);
  const result = verify(traceFile, 'carry');
  const unequal = Object.entries(result.kinds).filter(([, { packets }]) => packets[0] !== packets[1]).map(([key]) => key);
  const drift = Object.keys(result.state);
  if (!result.whole || result.frames === 0 || unequal.length || drift.length || result.matrices[0] !== result.matrices[1] || result.sync[0] !== result.sync[1])
    throw new Error(`the model does not reproduce ${traceFile}: ${[...unequal, ...drift, ...result.problems].join('; ')}`);
  if (recording.frames.length !== result.frames) throw new Error(`${recording.frames.length} frames recorded, ${result.frames} compared`);
  await addTextRamps(traceFile, recording.frames);
  if (recording.frames.some((frame) => frame.expect.text)) await addListFade(traceFile, recording.frames);
  const capture = path.basename(traceFile).replace(/\.trace\.jsonl$/, '');
  return { capture, frames: recording.frames.length, text: `${JSON.stringify({ capture, build: 'hdd', frames: recording.frames })}\n` };
}

if (process.argv[1] && process.argv[1].endsWith('export_fixture.mjs')) {
  const trace = process.argv[2];
  if (!trace) throw new Error('usage: node tools/scene/export_fixture.mjs <trace.jsonl> [out.json]');
  const { capture, frames, text } = await exportScene(trace);
  const out = process.argv[3] ?? `D:/CodingProjects/CrystalClockVK/References/fixtures/${capture}/scene.json`;
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, text);
  console.log(`${out}: ${frames} frames, ${text.length} bytes`);
}
