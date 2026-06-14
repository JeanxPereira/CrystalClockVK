// Convert a raw RGBA8 dump (e.g. clock_dump's --dump-rgba output) to PNG for viewing.
// Usage: node rgba2png.mjs <in.rgba> <WxH> <out.png>
import { readFileSync, writeFileSync } from "node:fs";
import { deflateSync } from "node:zlib";

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

const [, , inPath, wh, outPath] = process.argv;
const [w, h] = (wh || "0x0").split("x").map(Number);
const rgba = readFileSync(inPath);
writeFileSync(outPath, encodePng(w, h, rgba));
console.log(`wrote ${w}x${h} PNG -> ${outPath}`);
