// Numeric pixel-diff between our render and a ground-truth GS frame (PCSX2 SW
// renderer replaying the same .gs). Reports per-channel mean/max abs error and
// writes an error heatmap PNG. The project's actual fidelity gate (no eyeballing).
//
// Usage: node pdiff.mjs <ours.png|.rgba WxH> <reference.png|.rgba WxH> [heatmap.png]
import { readFileSync, writeFileSync } from "node:fs";
import { encodePng, decodePng } from "../shared/png.mjs";

function loadImage(spec) {
  // "<file.rgba> WxH" for raw, or "<file.png>"
  const parts = spec.split(/\s+/);
  const path = parts[0];
  if (path.endsWith(".png")) return decodePng(readFileSync(path));
  const [w, h] = (parts[1] || "0x0").split("x").map(Number);
  return { w, h, data: readFileSync(path) };
}

// nearest-neighbor sample (handles differing dims)
function sample(img, x, y) {
  const sx = Math.min(img.w - 1, x | 0), sy = Math.min(img.h - 1, y | 0);
  const i = (sy * img.w + sx) * 4;
  return [img.data[i], img.data[i + 1], img.data[i + 2]];
}

const a = loadImage(process.argv[2]);
const b = loadImage(process.argv[3]);
const heatPath = process.argv[4];
const W = Math.min(a.w, b.w), H = Math.min(a.h, b.h);
console.log(`A ${a.w}x${a.h}  B ${b.w}x${b.h}  -> compare ${W}x${H}`);

let sum = [0, 0, 0], max = [0, 0, 0], over = 0;
const THRESH = 16;
const heat = heatPath ? Buffer.alloc(W * H * 4) : null;
for (let y = 0; y < H; y++)
  for (let x = 0; x < W; x++) {
    const pa = sample(a, x * a.w / W, y * a.h / H);
    const pb = sample(b, x * b.w / W, y * b.h / H);
    let e = 0;
    for (let c = 0; c < 3; c++) { const d = Math.abs(pa[c] - pb[c]); sum[c] += d; if (d > max[c]) max[c] = d; e = Math.max(e, d); }
    if (e > THRESH) over++;
    if (heat) { const i = (y * W + x) * 4; const v = Math.min(255, e * 3); heat[i] = v; heat[i + 1] = 255 - v; heat[i + 2] = 0; heat[i + 3] = 255; }
  }
const n = W * H;
console.log(`mean abs err  R ${(sum[0] / n).toFixed(2)}  G ${(sum[1] / n).toFixed(2)}  B ${(sum[2] / n).toFixed(2)}`);
console.log(`max  abs err  R ${max[0]}  G ${max[1]}  B ${max[2]}`);
console.log(`pixels > ${THRESH}: ${(100 * over / n).toFixed(1)}%`);
if (heat) { writeFileSync(heatPath, encodePng(W, H, heat)); console.log(`heatmap -> ${heatPath}`); }
