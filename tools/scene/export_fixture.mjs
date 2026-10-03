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
//   } } ] }
import fs from 'node:fs';
import path from 'node:path';

process.env.CLOCK_BUILD ??= 'hdd';
if (process.env.CLOCK_BUILD !== 'hdd') throw new Error('the scene fixture is HDD OSD 1.10U only (CLOCK_BUILD=hdd)');

const { install } = await import('./instrument.mjs');

/** The frames of `traceFile` as the fixture's JSON text. */
export async function exportScene(traceFile) {
  const recording = await install();
  const { verify } = await import('../../References/scripts/verify_frame.mjs');
  const result = verify(traceFile, 'carry');
  const unequal = Object.entries(result.kinds).filter(([, { packets }]) => packets[0] !== packets[1]).map(([key]) => key);
  const drift = Object.keys(result.state);
  if (!result.whole || result.frames === 0 || unequal.length || drift.length || result.matrices[0] !== result.matrices[1] || result.sync[0] !== result.sync[1])
    throw new Error(`the model does not reproduce ${traceFile}: ${[...unequal, ...drift, ...result.problems].join('; ')}`);
  if (recording.frames.length !== result.frames) throw new Error(`${recording.frames.length} frames recorded, ${result.frames} compared`);
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
