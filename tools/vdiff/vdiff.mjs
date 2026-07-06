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

// Vertex-level equality used by both exact diff() and subset matching:
// xy within +/-1 on the raw 12.4 grid, colors/uv exact (mirrors diff()'s
// per-vertex checks, minus the error-message bookkeeping).
function vertsEqual(ov, cv) {
  if (ov.length !== cv.length) return false;
  for (let k = 0; k < ov.length; k++) {
    const oxy = [to124(ov[k].x), to124(ov[k].y)];
    const cxy = [to124(cv[k].x), to124(cv[k].y)];
    if (Math.abs(oxy[0] - cxy[0]) > 1 || Math.abs(oxy[1] - cxy[1]) > 1) return false;
    for (const ch of ["r", "g", "b", "a", "u", "v"]) {
      if ((ov[k][ch] ?? 0) !== (cv[k][ch] ?? 0)) return false;
    }
  }
  return true;
}

// Subset mode: every candidate draw must match SOME oracle draw of the same
// PRIM.type within tolerance (0x232618 emits only a fragment of the full
// clock stream, so an exact 1:1 draw-count match is not expected here).
// Returns { matched, total, mismatches: [first few unmatched candidate idx] }.
function subsetMatch(oracle, cand) {
  const byType = new Map();
  for (const o of oracle) {
    const t = o.PRIM?.type;
    if (!byType.has(t)) byType.set(t, []);
    byType.get(t).push(o);
  }
  let matched = 0;
  const mismatches = [];
  for (let i = 0; i < cand.length; i++) {
    const c = cand[i];
    const pool = byType.get(c.PRIM?.type) ?? [];
    const cv = c.verts ?? [];
    const found = pool.some((o) => vertsEqual(o.verts ?? [], cv));
    if (found) matched++;
    else if (mismatches.length < 10) mismatches.push(i);
  }
  return { matched, total: cand.length, mismatches };
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

// Coordinate-cloud mode: unlike --subset (whole-draw vertex-set equality,
// which assumes matching granularity/color), this checks each CANDIDATE
// vertex individually against the full oracle vertex cloud (every vertex
// from every oracle draw, flattened) and asks only "is there ANY oracle
// vertex within `tol` px?" -- the right check when candidate draws use a
// different granularity/color than the oracle (Phase 2 rod staging: 4-vert
// chunks vs the oracle's 60-vert TRI_STRIPs), so geometry can be validated
// even though draw-level or vertex-count-level comparison would not match.
function cloudMatch(oracle, cand, tol) {
  const cloud = [];
  for (const o of oracle) for (const v of o.verts ?? []) cloud.push(v);
  const results = [];
  let matched = 0;
  for (const c of cand) {
    for (const v of c.verts ?? []) {
      let best = null, bestDist = Infinity;
      for (const o of cloud) {
        const d = Math.hypot(v.x - o.x, v.y - o.y);
        if (d < bestDist) { bestDist = d; best = o; }
      }
      const hit = bestDist <= tol;
      if (hit) matched++;
      results.push({ x: v.x, y: v.y, nearest: best ? { x: best.x, y: best.y } : null, dist: bestDist, hit });
    }
  }
  return { matched, total: results.length, results };
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

  // --subset self-test: oracle has two draws (a SPRITE and the TRI_STRIP
  // from `a`); candidate is a subset containing only a perturbed-but-within-
  // tolerance copy of the SPRITE draw, plus one draw that matches nothing.
  const oracle2 = [
    { PRIM: { type: 6 }, verts: [{ x: 10, y: 20, r: 1, g: 2, b: 3, a: 4, u: 5, v: 6 }] },
    a[0],
  ];
  const candGood = [{ PRIM: { type: 6 }, verts: [{ x: 10.0625, y: 20, r: 1, g: 2, b: 3, a: 4, u: 5, v: 6 }] }];
  const r1 = subsetMatch(oracle2, candGood);
  if (r1.matched !== 1 || r1.total !== 1) { console.error("subset: expected 1/1 matched"); process.exit(1); }
  const candBad = [...candGood, { PRIM: { type: 6 }, verts: [{ x: 999, y: 999, r: 0, g: 0, b: 0, a: 0, u: 0, v: 0 }] }];
  const r2 = subsetMatch(oracle2, candBad);
  if (r2.matched !== 1 || r2.total !== 2 || r2.mismatches.length !== 1) {
    console.error("subset: expected 1/2 matched with 1 mismatch"); process.exit(1);
  }
  const r3 = subsetMatch(oracle2, []);
  if (r3.matched !== 0 || r3.total !== 0) { console.error("subset: expected 0/0 on empty candidate"); process.exit(1); }

  // --cloud self-test: oracle cloud has one vertex at (10,20); a candidate
  // vertex within tol must hit, one far outside must miss.
  const oracleCloud = [{ PRIM: { type: 4 }, verts: [{ x: 10, y: 20 }] }];
  const candCloud = [{ PRIM: { type: 4 }, verts: [{ x: 11, y: 20 }, { x: 500, y: 500 }] }];
  const rc = cloudMatch(oracleCloud, candCloud, 2);
  if (rc.matched !== 1 || rc.total !== 2) { console.error("cloud: expected 1/2 matched"); process.exit(1); }

  console.log("vdiff self-test OK");
  process.exit(0);
}

if (process.argv[2] === "--subset") {
  const oracle = loadDraws(process.argv[3]);
  const cand = loadDraws(process.argv[4]);
  const { matched, total, mismatches } = subsetMatch(oracle, cand);
  console.log(`vdiff --subset: ${matched}/${total} candidate draws matched`);
  if (mismatches.length) {
    console.log("first unmatched candidate draws:");
    for (const i of mismatches) console.log(`  cand[${i}]: PRIM.type=${cand[i].PRIM?.type} nverts=${(cand[i].verts ?? []).length}`);
  }
  process.exit(matched === total ? 0 : 1);
}

if (process.argv[2] === "--cloud") {
  const oracle = loadDraws(process.argv[3]);
  const cand = loadDraws(process.argv[4]);
  const tol = process.argv[5] ? Number(process.argv[5]) : 2;
  const { matched, total, results } = cloudMatch(oracle, cand, tol);
  console.log(`vdiff --cloud: ${matched}/${total} candidate vertices within ${tol}px of an oracle vertex`);
  for (const r of results) {
    console.log(`  (${r.x.toFixed(3)},${r.y.toFixed(3)}) -> nearest (${r.nearest?.x},${r.nearest?.y}) dist=${r.dist.toFixed(3)} ${r.hit ? "MATCH" : "miss"}`);
  }
  process.exit(matched === total ? 0 : 1);
}

const errs = diff(loadDraws(process.argv[2]), loadDraws(process.argv[3]));
if (errs.length) { errs.forEach((e) => console.error(e)); process.exit(1); }
console.log("vdiff: MATCH");
