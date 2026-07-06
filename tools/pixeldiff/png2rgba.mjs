// Decode a PNG (8-bit RGB/RGBA truecolor) to raw RGBA8 — for using GSRunner's
// per-draw render-target dumps as native-resolution pixel-diff references.
// Usage: node png2rgba.mjs <in.png> <out.rgba>
import { readFileSync, writeFileSync } from "node:fs";
import { decodePng } from "../shared/png.mjs";

const [, , inPath, outPath] = process.argv;
const { w, h, data } = decodePng(readFileSync(inPath));
writeFileSync(outPath, data);
console.log(`decoded ${w}x${h} -> ${outPath}`);
