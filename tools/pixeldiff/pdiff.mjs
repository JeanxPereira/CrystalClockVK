// Numeric pixel-diff between our render and a ground-truth GS frame (PCSX2 SW
// renderer replaying the same .gs). Reports per-channel mean/max abs error and
// writes an error heatmap PNG. The project's actual fidelity gate (no eyeballing).
//
// Usage: node pdiff.mjs <ours.png|.rgba WxH> <reference.png|.rgba WxH> [heatmap.png]
import { readFileSync, writeFileSync } from "node:fs";
import { inflateSync, deflateSync } from "node:zlib";

// --- minimal PNG decode (8-bit RGB/RGBA, filters 0-4) ---
function decodePng(buf) {
  if (buf.readUInt32BE(0) !== 0x89504e47) throw new Error("not a PNG");
  let off = 8, w = 0, h = 0, colorType = 0, bitDepth = 0;
  const idat = [];
  while (off < buf.length) {
    const len = buf.readUInt32BE(off);
    const type = buf.toString("latin1", off + 4, off + 8);
    const data = buf.subarray(off + 8, off + 8 + len);
    if (type === "IHDR") {
      w = data.readUInt32BE(0); h = data.readUInt32BE(4);
      bitDepth = data[8]; colorType = data[9];
    } else if (type === "IDAT") idat.push(data);
    else if (type === "IEND") break;
    off += 12 + len;
  }
  if (bitDepth !== 8) throw new Error("only 8-bit PNG supported");
  const ch = colorType === 6 ? 4 : colorType === 2 ? 3 : 1;
  const raw = inflateSync(Buffer.concat(idat));
  const stride = w * ch;
  const out = Buffer.alloc(w * h * 4);
  const prev = Buffer.alloc(stride);
  const cur = Buffer.alloc(stride);
  let p = 0;
  for (let y = 0; y < h; y++) {
    const filter = raw[p++];
    raw.copy(cur, 0, p, p + stride); p += stride;
    for (let i = 0; i < stride; i++) {
      const a = i >= ch ? cur[i - ch] : 0;
      const b = prev[i];
      const c = i >= ch ? prev[i - ch] : 0;
      let v = cur[i];
      if (filter === 1) v += a;
      else if (filter === 2) v += b;
      else if (filter === 3) v += (a + b) >> 1;
      else if (filter === 4) {
        const pp = a + b - c, pa = Math.abs(pp - a), pb = Math.abs(pp - b), pc = Math.abs(pp - c);
        v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
      }
      cur[i] = v & 0xff;
    }
    for (let x = 0; x < w; x++) {
      const s = x * ch, d = (y * w + x) * 4;
      out[d] = cur[s]; out[d + 1] = cur[ch >= 2 ? s + 1 : s];
      out[d + 2] = cur[ch >= 3 ? s + 2 : s]; out[d + 3] = ch === 4 ? cur[s + 3] : 255;
    }
    cur.copy(prev);
  }
  return { w, h, data: out };
}

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

function encodePng(w, h, rgba) {
  const stride = w * 4, filtered = Buffer.alloc((stride + 1) * h);
  for (let y = 0; y < h; y++) rgba.copy(filtered, y * (stride + 1) + 1, y * stride, y * stride + stride);
  const idat = deflateSync(filtered);
  const tab = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
  const crc = (b) => { let c = 0xffffffff; for (let i = 0; i < b.length; i++) c = tab[(c ^ b[i]) & 0xff] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
  const chunk = (type, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const cd = Buffer.concat([Buffer.from(type), d]); const cc = Buffer.alloc(4); cc.writeUInt32BE(crc(cd)); return Buffer.concat([l, cd, cc]); };
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 6;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk("IHDR", ihdr), chunk("IDAT", idat), chunk("IEND", Buffer.alloc(0))]);
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
