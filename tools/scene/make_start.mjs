// The app's start state: the clock as one real capture holds it at a frame's entry. The model has no default
// state, so the app starts from a captured moment and real time drives it from there.
// Takes frames[0].input of a scene.json (tools/scene/export_fixture.mjs) and keeps only the pieces
// src/scene/SceneInputs.cpp reads (clockInputs, frameInputs), unchanged.
//
// node tools/scene/make_start.mjs <scene.json> [out.json]
//   default in:  D:/CodingProjects/CrystalClockVK/References/fixtures/hddosd-110U-whole3-clock/scene.json
//   default out: resources/clock/start.json
import fs from 'node:fs';
import path from 'node:path';

const KEYS = [
  'time', 'eased', 'state', 'appearance', 'colours', 'cycleCounters', 'cycleTables', 'logicConstants', 'scene',
  'scaleTarget', 'scaleFactor', 'timeFilled', 'mode', 'level', 'overlayLevel', 'vignetteRamp', 'vignetteLength',
  'menuRamp', 'tail', 'counter', 'proportions', 'position', 'direction', 'up', 'rotation', 'cameraOffset', 'zmax',
  'cameraFactor', 'greyRamp', 'greys', 'ringRecord', 'tint', 'blurRecord', 'copyRecord', 'fadeRecord', 'bars',
  'column', 'template', 'rings', 'spriteFade', 'clearColour', 'display', 'tubeConstants', 'orbConstants',
  'orbColour', 'screen', 'index', 'item0', 'font', 'textRamps', 'configItems', 'mechaconParam', 'videoMode',
  'wide', 'orbRandom', 'orbColours',
];
// The clock entered from the opening has no text and carries the orbs' entry motion.
const OPTIONAL = new Set(['font', 'textRamps', 'mechaconParam', 'videoMode', 'wide', 'orbRandom', 'orbColours']);

const input = process.argv[2] ?? 'D:/CodingProjects/CrystalClockVK/References/fixtures/hddosd-110U-whole3-clock/scene.json';
const output = process.argv[3] ?? 'resources/clock/start.json';
const scene = JSON.parse(fs.readFileSync(input, 'utf8'));
const first = scene.frames[0];
const start = { capture: scene.capture, build: scene.build, frame: first.index };
for (const key of KEYS) {
  if (!(key in first.input)) {
    if (OPTIONAL.has(key)) continue;
    throw new Error(`frames[0].input has no ${key}`);
  }
  start[key] = first.input[key];
}
const text = `{\n${Object.entries(start).map(([k, v]) => `${JSON.stringify(k)}: ${JSON.stringify(v)}`).join(',\n')}\n}\n`;
fs.mkdirSync(path.dirname(output), { recursive: true });
fs.writeFileSync(output, text);
console.log(`${output}: ${text.length} bytes from ${scene.capture} frame ${first.index}`);
