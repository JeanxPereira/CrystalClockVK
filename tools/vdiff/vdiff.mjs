// tools/vdiff/vdiff.mjs — per-pass vertex diff between two gsdump --json files.
//
// Real gsdump --json schema (tools/gsdump/main.cpp writeJson, verified against
// re/oracle/clock_sw_prims.json):
//   - top-level is a BARE array of draws (no "draws" wrapper key)
//   - per-draw primitive type lives at draw.PRIM.type (0=POINT..6=SPRITE)
//   - per-draw vertex array is draw.verts
//   - per-vertex x/y are ALREADY-CONVERTED floats: raw 12.4 fixed / 16.0
//     (see GsDumpParser.cpp: v.x = (xRaw - ofx) / 16.0f). So "±1 on the raw
//     12.4 int" means a float tolerance of 1/16 = 0.0625 pixels once the two
//     sides are re-quantized to the same raw-int grid (round(x*16)).
//   - color is NOT a packed rgba; it is four separate components
//     draw.verts[k].{r,g,b,a} (0-255ish, a is GS alpha 0-128), compared exact.
//   - uv/st are draw.verts[k].{u,v,s,t,q}; u/v compared exact per the brief's
//     "colors/UVs exact" requirement.
import { readFileSync } from "node:fs";

function loadDraws(path) {
  const j = JSON.parse(readFileSync(path, "utf8"));
  return Array.isArray(j) ? j : (j.draws ?? j); // tolerate a bare array or a {draws:[...]} wrapper
}

function to124(x) {
  // Re-quantize an already-divided-by-16 float back to the raw 12.4 int grid.
  return Math.round(x * 16);
}

function diff(oracle, cand) {
  const errs = [];
  if (oracle.length !== cand.length)
    errs.push(`draw count ${cand.length} != oracle ${oracle.length}`);
  const n = Math.min(oracle.length, cand.length);
  for (let i = 0; i < n && errs.length < 10; i++) {
    const o = oracle[i], c = cand[i];
    const oPrim = o.PRIM?.type, cPrim = c.PRIM?.type;
    if (oPrim !== cPrim) { errs.push(`draw ${i}: prim ${cPrim} != ${oPrim}`); continue; }
    const ov = o.verts ?? [], cv = c.verts ?? [];
    if (ov.length !== cv.length) { errs.push(`draw ${i}: ${cv.length} verts != ${ov.length}`); continue; }
    for (let k = 0; k < ov.length && errs.length < 10; k++) {
      const oxy = [to124(ov[k].x), to124(ov[k].y)];
      const cxy = [to124(cv[k].x), to124(cv[k].y)];
      if (Math.abs(oxy[0] - cxy[0]) > 1 || Math.abs(oxy[1] - cxy[1]) > 1)
        errs.push(`draw ${i} v${k}: xy (${cv[k].x},${cv[k].y}) != (${ov[k].x},${ov[k].y})`);
      for (const ch of ["r", "g", "b", "a"]) {
        if ((ov[k][ch] ?? 0) !== (cv[k][ch] ?? 0))
          errs.push(`draw ${i} v${k}: ${ch} ${cv[k][ch]} != ${ov[k][ch]}`);
      }
      for (const ch of ["u", "v"]) {
        if ((ov[k][ch] ?? 0) !== (cv[k][ch] ?? 0))
          errs.push(`draw ${i} v${k}: ${ch} ${cv[k][ch]} != ${ov[k][ch]}`);
      }
    }
  }
  return errs;
}

if (process.argv[2] === "--self-test") {
  const a = [{ PRIM: { type: 4 }, verts: [{ x: 100, y: 200, r: 0, g: 255, b: 0, a: 128, u: 0, v: 0 }] }];
  const b = JSON.parse(JSON.stringify(a));
  if (diff(a, b).length !== 0) { console.error("identity failed"); process.exit(1); }
  b[0].verts[0].x += 5;
  if (diff(a, b).length === 0) { console.error("perturbation not caught"); process.exit(1); }
  // sub-1/16-pixel jitter must NOT be flagged (still within ±1 raw-12.4 unit)
  const c = JSON.parse(JSON.stringify(a));
  c[0].verts[0].x += 0.0625;
  if (diff(a, c).length !== 0) { console.error("false positive on ±1 raw-unit tolerance"); process.exit(1); }
  console.log("vdiff self-test OK");
  process.exit(0);
}

const errs = diff(loadDraws(process.argv[2]), loadDraws(process.argv[3]));
if (errs.length) { errs.forEach((e) => console.error(e)); process.exit(1); }
console.log("vdiff: MATCH");
