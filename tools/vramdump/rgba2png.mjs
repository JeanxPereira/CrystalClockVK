// Wrap a raw RGBA8 buffer into a PNG (so the decoded VRAM can be viewed).
// Usage: node rgba2png.mjs <in.rgba> <W> <H> <out.png>
import { readFileSync, writeFileSync } from "node:fs";
import { deflateSync } from "node:zlib";

const [, , rawPath, W, H, outPath] = process.argv;
const w = +W, h = +H;
const raw = readFileSync(rawPath);
const stride = w * 4;

const filtered = Buffer.alloc((stride + 1) * h);
for (let y = 0; y < h; y++) {
  filtered[y * (stride + 1)] = 0; // filter type 0 (none)
  raw.copy(filtered, y * (stride + 1) + 1, y * stride, y * stride + stride);
}
const idat = deflateSync(filtered);

const crcTable = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();
const crc32 = (b) => {
  let c = 0xffffffff;
  for (let i = 0; i < b.length; i++) c = crcTable[(c ^ b[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
};
const chunk = (type, data) => {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length);
  const cd = Buffer.concat([Buffer.from(type, "latin1"), data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(cd));
  return Buffer.concat([len, cd, crc]);
};

const ihdr = Buffer.alloc(13);
ihdr.writeUInt32BE(w, 0);
ihdr.writeUInt32BE(h, 4);
ihdr[8] = 8;  // bit depth
ihdr[9] = 6;  // color type RGBA
const sig = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);
const png = Buffer.concat([sig, chunk("IHDR", ihdr), chunk("IDAT", idat), chunk("IEND", Buffer.alloc(0))]);
writeFileSync(outPath, png);
console.log(`wrote ${outPath} ${png.length} bytes ${w}x${h}`);
