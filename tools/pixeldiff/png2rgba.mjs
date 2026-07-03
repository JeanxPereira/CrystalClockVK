// Decode a PNG (8-bit RGB/RGBA truecolor) to raw RGBA8 — for using GSRunner's
// per-draw render-target dumps as native-resolution pixel-diff references.
// Usage: node png2rgba.mjs <in.png> <out.rgba>
import { readFileSync, writeFileSync } from "node:fs";
import { inflateSync } from "node:zlib";

const [, , inPath, outPath] = process.argv;
const b = readFileSync(inPath);
const w = b.readUInt32BE(16), h = b.readUInt32BE(20);
const bitDepth = b[24], colorType = b[25];
const ch = colorType === 6 ? 4 : colorType === 2 ? 3 : 0;
if (!ch || bitDepth !== 8) throw new Error(`unsupported png: ct=${colorType} bd=${bitDepth}`);

let p = 8;
const idat = [];
while (p < b.length) {
  const len = b.readUInt32BE(p);
  const type = b.toString("ascii", p + 4, p + 8);
  if (type === "IDAT") idat.push(b.subarray(p + 8, p + 8 + len));
  p += 12 + len;
}
const raw = inflateSync(Buffer.concat(idat));
const stride = w * ch;
const out = Buffer.alloc(w * h * 4);
const prev = Buffer.alloc(stride);
const cur = Buffer.alloc(stride);
for (let y = 0; y < h; y++) {
  const f = raw[y * (stride + 1)];
  const line = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
  for (let i = 0; i < stride; i++) {
    const a = i >= ch ? cur[i - ch] : 0, up = prev[i], ul = i >= ch ? prev[i - ch] : 0;
    let v = line[i];
    if (f === 1) v += a;
    else if (f === 2) v += up;
    else if (f === 3) v += (a + up) >> 1;
    else if (f === 4) {
      const pp = a + up - ul, pa = Math.abs(pp - a), pb = Math.abs(pp - up), pc = Math.abs(pp - ul);
      v += pa <= pb && pa <= pc ? a : pb <= pc ? up : ul;
    }
    cur[i] = v & 0xff;
  }
  for (let x = 0; x < w; x++) {
    out[(y * w + x) * 4] = cur[x * ch];
    out[(y * w + x) * 4 + 1] = cur[x * ch + 1];
    out[(y * w + x) * 4 + 2] = cur[x * ch + 2];
    out[(y * w + x) * 4 + 3] = ch === 4 ? cur[x * ch + 3] : 255;
  }
  cur.copy(prev);
}
writeFileSync(outPath, out);
console.log(`decoded ${w}x${h} (ct${colorType}) -> ${outPath}`);
