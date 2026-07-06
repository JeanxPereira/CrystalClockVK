import { readFileSync, writeFileSync } from "node:fs";
import { decodePng, encodePng } from "./shared/png.mjs";
const shot = decodePng(readFileSync("re/ram/clock/Screenshot.png"));
const rods = JSON.parse(readFileSync("re/oracle/cand_rods_staging.json"));
const W = shot.w, H = shot.h;
const yScale = H / 224;
const buf = Buffer.from(shot.data);
function put(x, y, r, g, b) {
  x = Math.round(x); y = Math.round(y);
  if (x < 0 || x >= W || y < 0 || y >= H) return;
  const i = (y * W + x) * 4; buf[i] = r; buf[i+1] = g; buf[i+2] = b; buf[i+3] = 255;
}
function cross(x, y, r, g, b) { for (let d=-5; d<=5; d++){ put(x+d,y,r,g,b); put(x,y+d,r,g,b);} }
for (const rod of rods) for (const v of rod.verts) cross(v.x, v.y*yScale, 255, 0, 255);
writeFileSync("re/oracle/rods_on_screenshot.png", encodePng(W, H, buf));
console.log("markers (x, y*"+yScale.toFixed(2)+"):");
for (const rod of rods) console.log("  "+rod.verts.map(v=>`(${v.x.toFixed(0)},${(v.y*yScale).toFixed(0)})`).join(" "));
