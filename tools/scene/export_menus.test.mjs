import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REFERENCES = (process.env.CLOCK_REFERENCES ?? 'D:/CodingProjects/CrystalClockVK/References').replace(/\\/g, '/');
const CAPTURES = 'D:/CodingProjects/Watson/Runtime/captures';
const env = { ...process.env, CLOCK_BUILD: 'hdd', CLOCK_REFERENCES: REFERENCES };
const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'menus-'));
const run = (script, args) => execFileSync(process.execPath, [path.join(HERE, script), ...args], { env, stdio: 'pipe', maxBuffer: 1 << 28 });
const exportOf = (capture) => {
  const out = path.join(dir, `${capture}.json`);
  run('export_fixture.mjs', [`${CAPTURES}/${capture}.trace.jsonl`, out]);
  return JSON.parse(fs.readFileSync(out, 'utf8'));
};
const MENU_KEYS = ['pad', 'disc', 'screenCode', 'configPage', 'configRamp', 'configEntries', 'mainMenu', 'versionRamp', 'dialogRamp', 'firstRunRamp',
  'pagePointers', 'entryActive', 'menuLengths', 'listConstants', 'adjustFields', 'configGate', 'cubeList', 'cubeColours', 'cubeRecord', 'spin',
  'cubeConstants', 'centreFactors', 'cubeView', 'cubeScreen', 'layerClear', 'addRecord', 'halfRecord', 'chainRecord'];
const STAGES = ['cubes', 'menuStep', 'menus', 'endOfFrame'];
let enter = exportOf('hddosd-110U-whole3-enter');

test('every frame of a menu capture holds the menus\' pieces and its stages', () => {
  for (const frame of enter.frames) {
    for (const key of MENU_KEYS) assert.ok(key in frame.input, `frame ${frame.index}: input.${key}`);
    assert.equal(frame.input.configEntries.length, 9);
    for (const stage of STAGES) {
      assert.ok(frame.expect.stages[stage].before.configPage, `frame ${frame.index} ${stage}.before`);
      assert.ok(frame.expect.stages[stage].after.configPage, `frame ${frame.index} ${stage}.after`);
    }
    assert.equal(frame.between === null, frame.index === 0);
  }
});

test('the opening of System Configuration, the cubes and the page strings are recorded', () => {
  const opened = enter.frames.find((f) => f.expect.stages.menus.before.configPage.ramp.state === 0 && f.expect.stages.menus.after.configPage.ramp.state === 1);
  assert.ok(opened, 'no frame starts the page ramp');
  assert.ok(opened.input.pad.pressed & 0x20, 'cross is pressed in the frame that opens the page');
  assert.ok(enter.frames.some((f) => f.expect.cubes.length > 0 && f.expect.cubes[0].label.startsWith('cube')));
  assert.ok(enter.frames.some((f) => f.expect.text.pages.length > 0));
  assert.ok(enter.frames.some((f) => f.expect.listFade));
  enter = null;
});

test('whole3-clock keeps every old key byte for byte', () => {
  const fresh = exportOf('hddosd-110U-whole3-clock');
  const installed = JSON.parse(fs.readFileSync(`${REFERENCES}/fixtures/hddosd-110U-whole3-clock/scene.json`, 'utf8'));
  const pick = (from, keys) => Object.fromEntries(Object.keys(keys).filter((key) => key in from).map((key) => [key, from[key]]));
  assert.equal(fresh.frames.length, installed.frames.length);
  fresh.frames.forEach((frame, i) => {
    const old = installed.frames[i];
    const kept = { ...pick(frame, old), input: pick(frame.input, old.input), expect: { ...pick(frame.expect, old.expect), after: pick(frame.expect.after, old.expect.after) } };
    if (old.expect.text) kept.expect.text = pick(frame.expect.text, old.expect.text);
    assert.ok(JSON.stringify(kept) === JSON.stringify(old), `frame ${i}: an old key changed`);
  });
});

test('the scripted scenarios run through the model', () => {
  const out = path.join(dir, 'synthetic');
  run('menus_synthetic.mjs', [out]);
  for (const name of ['menu-cursor', 'enter-mash', 'config-wrap', 'square', 'entries', 'back', 'browser', 'long']) {
    const scene = JSON.parse(fs.readFileSync(path.join(out, name, 'scene.json'), 'utf8'));
    assert.equal(scene.stagesOnly, true);
    assert.ok(scene.frames.length > 30, name);
    assert.ok(scene.frames.every((f) => STAGES.every((s) => f.expect.stages[s])), name);
  }
  const square = JSON.parse(fs.readFileSync(path.join(out, 'square', 'scene.json'), 'utf8'));
  const states = square.frames.map((f) => f.expect.stages.menuStep.after.menuRamp.state);
  assert.ok(states.includes(2) && states.lastIndexOf(3) > states.indexOf(2), 'square hides the menu, then shows it');
  const long = JSON.parse(fs.readFileSync(path.join(out, 'long', 'scene.json'), 'utf8'));
  const positions = long.frames.map((f) => f.expect.stages.cubes.after.cubeList.position);
  assert.ok(positions.some((p, i) => i > 0 && p > positions[i - 1] + 90000), 'the list position wraps');
  assert.ok(long.frames.some((f, i) => i > 0 && f.input.spin < long.frames[i - 1].input.spin), 'the spin wraps');
  const browser = JSON.parse(fs.readFileSync(path.join(out, 'browser', 'scene.json'), 'utf8'));
  assert.ok(browser.frames.some((f) => f.between?.after.mainMenu.selected === 0 && f.input.pad.pressed & 0x20), 'cross is pressed on Browser');
  assert.ok(browser.frames.every((f) => f.expect.stages.menus.after.configPage.ramp.state === 0), 'Browser never opens System Configuration');
});
