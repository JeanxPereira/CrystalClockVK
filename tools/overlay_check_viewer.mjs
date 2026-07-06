import { readFileSync, writeFileSync } from "node:fs";
import { decodePng, encodePng } from "./shared/png.mjs";

// Phase 2 -- Visor dial render: same overlay method as overlay_check.mjs
// (which validates the MENU capture against re/ram/clock/Screenshot.png),
// pointed at the Visor capture instead. yScale assumes the same 640x224
// native framebuffer height as the menu (both are OSDSYS UI in the same
// video mode) scaled to the 640x480 screenshot -- NOT independently
// confirmed for this capture (no GS FRAME/DISPLAY register dump exists for
// clock_viewer; see phase2-visor-report.md for the honest caveat).
const shot = decodePng(readFileSync("re/ram/clock_viewer/Screenshot.png"));
const rods = JSON.parse(readFileSync("re/oracle/cand_viewer_staging.json"));
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
writeFileSync("re/oracle/viewer_rods_on_screenshot.png", encodePng(W, H, buf));
console.log("W="+W+" H="+H+" markers (x, y*"+yScale.toFixed(2)+"):");
for (const rod of rods) console.log("  "+rod.verts.map(v=>`(${v.x.toFixed(0)},${(v.y*yScale).toFixed(0)})`).join(" "));
