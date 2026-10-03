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
  assert.equal(done.dropped, 284);
  assert.equal(done.left, 1);
  assert.equal(frame.passes.length, 188);
  assert.deepEqual(frame.targets.map((t) => t.id).sort(), ['fb0000', 'fb1a40', 'fb2300']);
  for (const target of frame.targets) {
    assert.equal(target.width, 640);
    assert.equal(target.height, 224);
    assert.equal(fs.statSync(path.join(out, target.start)).size, 640 * 224 * 4);
  }

  const skipped = frame.passes.filter((p) => p.skip);
  assert.equal(skipped.length, 26);
  assert.ok(!frame.passes.some((p) => p.skip === 'gouraud line'), 'no clock line is smooth');
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
