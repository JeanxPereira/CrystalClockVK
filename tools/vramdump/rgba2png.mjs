// Wrap a raw RGBA8 buffer into a PNG (so the decoded VRAM can be viewed).
// Usage: node rgba2png.mjs <in.rgba> <W> <H> <out.png>
import { readFileSync, writeFileSync } from "node:fs";
import { encodePng } from "../shared/png.mjs";

const [, , rawPath, W, H, outPath] = process.argv;
const w = +W, h = +H;
const raw = readFileSync(rawPath);
const png = encodePng(w, h, raw);
writeFileSync(outPath, png);
console.log(`wrote ${outPath} ${png.length} bytes ${w}x${h}`);
