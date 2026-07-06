// Automated pixel-diff regression gate: replays the 5 reference .gs dumps
// through the app's --dump-rgba headless path and through GSRunner's
// bit-accurate SW renderer, then numerically diffs them (tools/pixeldiff).
// Exit 0 = all dumps within threshold; exit 1 = at least one regressed.
//
// Usage: node tools/gate/gate.mjs
import { execFileSync, spawnSync } from "node:child_process";
import { readFileSync, writeFileSync, mkdirSync, rmSync, existsSync, statSync, readdirSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const GATE_DIR = path.join(ROOT, "tools", "gate");
const CACHE_DIR = path.join(GATE_DIR, ".cache");
const APP_EXE = path.join(ROOT, "bin", "CrystalClockVK.exe");
const PDIFF_MJS = path.join(ROOT, "tools", "pixeldiff", "pdiff.mjs");
const PNG2RGBA_MJS = path.join(ROOT, "tools", "pixeldiff", "png2rgba.mjs");

const GSRUNNER_DIR = "C:\\CodingProjects\\Personal\\pcsx2-gsrunner";
const GSRUNNER_EXE = path.join(GSRUNNER_DIR, "pcsx2-gsrunner.exe");
const GSRUNNER_BIN = path.join(GSRUNNER_DIR, "bin");

const MAX_BUFFER = 64 * 1024 * 1024;

function toBackslash(p) {
  return p.replace(/\//g, "\\");
}

function pngSize(buf) {
  return [buf.readUInt32BE(16), buf.readUInt32BE(20)];
}

function runApp(gsPath, outRgbaPath) {
  // GsRenderer logs DISPFB (and everything else diagnostic) to stderr; stdout
  // carries only the scene-load summary. Capture both and search either.
  const result = spawnSync(APP_EXE, [gsPath, "--dump-rgba", outRgbaPath], {
    maxBuffer: MAX_BUFFER,
    encoding: "utf8",
  });
  if (result.error) throw result.error;
  const combined = `${result.stdout || ""}\n${result.stderr || ""}`;
  const m = combined.match(/DISPFB fbp=(\d+)/);
  if (!m) throw new Error(`could not find DISPFB fbp= in app output for ${gsPath}`);
  return parseInt(m[1], 10);
}

// Extract the native-resolution GSRunner reference frame for a dump (cached
// forever per dump filename+size — GSRunner's replay is deterministic and
// never changes for a given dump).
function extractRef(name, gsPath, fbp, cacheKey) {
  const refPath = path.join(CACHE_DIR, `${cacheKey}_ref.rgba`);
  if (existsSync(refPath)) return refPath;

  const tmpDir = path.join(CACHE_DIR, `tmp_${cacheKey}`);
  rmSync(tmpDir, { recursive: true, force: true });
  mkdirSync(tmpDir, { recursive: true });

  const logPath = path.join(tmpDir, "gsrunner.log");
  const gsPathBackslash = toBackslash(path.resolve(gsPath));
  const dumpDirBackslash = toBackslash(tmpDir);

  execFileSync(
    GSRUNNER_EXE,
    ["-renderer", "sw", "-dump", "rt", "-dumpdir", dumpDirBackslash, "-logfile", logPath, "--", gsPathBackslash],
    {
      env: { ...process.env, PATH: `${GSRUNNER_BIN};${process.env.PATH}` },
      maxBuffer: MAX_BUFFER,
    }
  );

  const addrHex = (fbp * 32).toString(16).padStart(5, "0");
  const suffix = `_rt1_${addrHex}_C_32.png`;
  const candidates = readdirSync(tmpDir)
    .filter((f) => f.endsWith(suffix))
    .sort();

  let best = null;
  for (const f of candidates) {
    const full = path.join(tmpDir, f);
    const buf = readFileSync(full);
    const [w, h] = pngSize(buf);
    if (w === 640 && h === 224) best = full;
  }
  if (!best) {
    throw new Error(`no 640x224 rt1_${addrHex} frame found for ${name} (fbp=${fbp}) in ${tmpDir}`);
  }

  execFileSync(process.execPath, [PNG2RGBA_MJS, best, refPath], { maxBuffer: MAX_BUFFER });
  rmSync(tmpDir, { recursive: true, force: true });
  return refPath;
}

function pdiffPct(oursPath, refPath) {
  const stdout = execFileSync(
    process.execPath,
    [PDIFF_MJS, `${oursPath} 640x224`, `${refPath} 640x224`],
    { maxBuffer: MAX_BUFFER, encoding: "utf8" }
  );
  const m = stdout.match(/pixels > \d+: ([\d.]+)%/);
  if (!m) throw new Error(`could not parse pdiff output:\n${stdout}`);
  return parseFloat(m[1]);
}

function main() {
  mkdirSync(CACHE_DIR, { recursive: true });
  const thresholds = JSON.parse(readFileSync(path.join(GATE_DIR, "thresholds.json"), "utf8"));
  const toleranceAbs = thresholds.toleranceAbs;

  const rows = [];
  const failures = [];

  for (const [name, cfg] of Object.entries(thresholds.dumps)) {
    if (!existsSync(cfg.gs)) {
      rows.push({ name, pct: NaN, maxPct: cfg.maxPct, pass: false, note: "dump not found" });
      failures.push(name);
      continue;
    }

    const size = statSync(cfg.gs).size;
    const cacheKey = `${name}_${size}`;
    const oursPath = path.join(CACHE_DIR, `${name}_ours.rgba`);

    const fbp = runApp(cfg.gs, oursPath);
    const refPath = extractRef(name, cfg.gs, fbp, cacheKey);
    const pct = pdiffPct(oursPath, refPath);

    const pass = pct <= cfg.maxPct + toleranceAbs;
    rows.push({ name, pct, maxPct: cfg.maxPct, pass, note: "" });
    if (!pass) failures.push(name);
  }

  const header = `${"dump".padEnd(15)} ${"pct".padStart(7)} ${"maxPct".padStart(8)} ${"tol".padStart(6)}  result`;
  console.log(header);
  console.log("-".repeat(header.length));
  for (const r of rows) {
    const pctStr = Number.isNaN(r.pct) ? "  n/a" : r.pct.toFixed(2).padStart(7);
    const status = r.pass ? "PASS" : `FAIL${r.note ? ` (${r.note})` : ""}`;
    console.log(
      `${r.name.padEnd(15)} ${pctStr} ${r.maxPct.toFixed(2).padStart(8)} ${toleranceAbs.toFixed(2).padStart(6)}  ${status}`
    );
  }

  if (failures.length > 0) {
    console.error(`\nGATE FAILED: ${failures.join(", ")}`);
    process.exit(1);
  }

  console.log("\nGATE PASSED: all dumps within threshold.");
}

main();
