// Extract the embedded PCSX2 screenshot from a (decompressed) .gs dump to raw
// RGBA8 — the ground-truth display frame for the numeric pixel-diff gate.
// Header layout mirrors tools/gsdump/inspect.mjs (GSLzma.cpp / GSDump.h).
//
// Usage: node extract_ref.mjs <dump.gs> <out.rgba>
import { readFileSync, writeFileSync } from "node:fs";

const [, , inPath, outPath] = process.argv;
if (!inPath || !outPath) {
  console.error("usage: node extract_ref.mjs <dump.gs> <out.rgba>");
  process.exit(1);
}

const buf = readFileSync(inPath);
const headerSize = buf.readUInt32LE(4);           // after leading crc (u32)
const stateBlob = buf.subarray(8, 8 + headerSize);
const w = stateBlob.readUInt32LE(20);
const h = stateBlob.readUInt32LE(24);
const ssOffset = stateBlob.readUInt32LE(28);
const ssSize = stateBlob.readUInt32LE(32);
const bpp = ssSize / (w * h);
console.log(`screenshot ${w}x${h}  size=${ssSize}  bpp=${bpp}`);
if (bpp !== 4) throw new Error(`unexpected screenshot bpp=${bpp} (want 4 = RGBA8)`);

const ss = stateBlob.subarray(ssOffset, ssOffset + ssSize);
writeFileSync(outPath, ss);
console.log(`wrote ${ss.length} bytes -> ${outPath}  (${w}x${h} RGBA8)`);
