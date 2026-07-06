// Convert a raw RGBA8 dump (e.g. clock_dump's --dump-rgba output) to PNG for viewing.
// Usage: node rgba2png.mjs <in.rgba> <WxH> <out.png>
import { readFileSync, writeFileSync } from "node:fs";
import { encodePng } from "../shared/png.mjs";

const [, , inPath, wh, outPath] = process.argv;
const [w, h] = (wh || "0x0").split("x").map(Number);
const rgba = readFileSync(inPath);
writeFileSync(outPath, encodePng(w, h, rgba));
console.log(`wrote ${w}x${h} PNG -> ${outPath}`);
