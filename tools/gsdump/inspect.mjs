// Quick format-validation pass over a PCSX2 .gs dump (decompressed).
// Grounded in pcsx2-ref: GSDump.cpp::AddHeader (writer) + GSLzma.cpp (reader).
// Validates the header, walks the packet stream, and walks GIFtags inside
// PATH transfers to tally primitives + A+D register writes. Precedes the C++ tool.
//
// Usage: node inspect.mjs <path-to.gs|.gs.zst>
import { readFileSync, writeFileSync } from "node:fs";
import { zstdDecompressSync } from "node:zlib";

const path = process.argv[2];
if (!path) {
  console.error("usage: node inspect.mjs <dump.gs|dump.gs.zst>");
  process.exit(1);
}

let buf = readFileSync(path);
if (path.endsWith(".zst")) {
  buf = zstdDecompressSync(buf);
  console.log(`decompressed .zst -> ${buf.length} bytes`);
}

let off = 0;
const u8 = () => buf.readUInt8(off++);
const u32 = () => {
  const v = buf.readUInt32LE(off);
  off += 4;
  return v;
};

// --- Header (new format: GSLzma.cpp:74-129) ---
const crc = u32();
const headerSize = u32(); // = sizeof(GSDumpHeader)+serial+screenshot
if (crc !== 0xffffffff)
  console.warn(`WARN: leading crc=0x${crc.toString(16)} (old-format dump?)`);

const stateBlob = buf.subarray(off, off + headerSize);
off += headerSize;

// GSDumpHeader: 9x u32 (GSDump.h:30)
const h = {};
let ho = 0;
const hn = (k) => {
  h[k] = stateBlob.readUInt32LE(ho);
  ho += 4;
};
[
  "state_version",
  "state_size",
  "serial_offset",
  "serial_size",
  "crc",
  "screenshot_width",
  "screenshot_height",
  "screenshot_offset",
  "screenshot_size",
].forEach(hn);
const serial = stateBlob
  .subarray(h.serial_offset, h.serial_offset + h.serial_size)
  .toString("latin1");

// real freeze state, then the 8192-byte GSPrivRegSet
off += h.state_size;
const REGSET_BYTES = 0x2000;
off += REGSET_BYTES;

console.log("=== GSDumpHeader ===");
console.log(`state_version     ${h.state_version}`);
console.log(`state_size        ${h.state_size} (freeze)`);
console.log(`real crc          0x${h.crc.toString(16)}`);
console.log(`serial            "${serial}"`);
console.log(`screenshot        ${h.screenshot_width}x${h.screenshot_height}`);
console.log(`header(=oldstate)  ${headerSize}  priv-regs ${REGSET_BYTES}`);
console.log(`packet stream @    ${off}`);

// --- GIFtag walk (GSRegs.h:507 GIFTag, :1133/1140 nloop/nreg) ---
const FLG = ["PACKED", "REGLIST", "IMAGE", "IMAGE2"];
const tally = {
  transfers: 0,
  vsync: 0,
  readfifo: 0,
  regs: 0,
  giftags: 0,
  kicks: 0, // vertices emitted (XYZ drawing kicks)
  nloopSum: 0,
  flg: { PACKED: 0, REGLIST: 0, IMAGE: 0, IMAGE2: 0 },
  primTypes: {}, // PRIM value histogram
  adRegs: {}, // A+D register-address histogram
};
const pathName = ["PATH1", "PATH2", "PATH3"];
const transfersByPath = {};

// --- GS register bitfield decoders (canonical GS layout, cross-checked vs GSRegs.h) ---
const lo32 = (v) => Number(v & 0xffffffffn);
const hi32 = (v) => Number((v >> 32n) & 0xffffffffn);
const DEC = {
  ALPHA: (v) => {
    const l = lo32(v);
    return { A: l & 3, B: (l >> 2) & 3, C: (l >> 4) & 3, D: (l >> 6) & 3, FIX: hi32(v) & 0xff };
  },
  TEST: (v) => {
    const l = lo32(v);
    return {
      ATE: l & 1, ATST: (l >> 1) & 7, AREF: (l >> 4) & 0xff, AFAIL: (l >> 12) & 3,
      DATE: (l >> 14) & 1, DATM: (l >> 15) & 1, ZTE: (l >> 16) & 1, ZTST: (l >> 17) & 3,
    };
  },
  TEX0: (v) => {
    const l = lo32(v), h = hi32(v);
    return {
      TBP0: l & 0x3fff, TBW: (l >> 14) & 0x3f, PSM: (l >> 20) & 0x3f,
      TW: (l >> 26) & 0xf, TH: ((l >>> 30) & 3) | ((h & 3) << 2),
      TCC: (h >> 2) & 1, TFX: (h >> 3) & 3, CBP: (h >> 5) & 0x3fff,
      CPSM: (h >> 19) & 0xf, CSM: (h >> 23) & 1, CSA: (h >> 24) & 0x1f, CLD: (h >> 29) & 7,
    };
  },
  TEX1: (v) => {
    const l = lo32(v);
    return { LCM: l & 1, MXL: (l >> 2) & 7, MMAG: (l >> 5) & 1, MMIN: (l >> 6) & 7, MTBA: (l >> 9) & 1 };
  },
  FRAME: (v) => {
    const l = lo32(v);
    return { FBP: l & 0x1ff, FBW: (l >> 16) & 0x3f, PSM: (l >> 24) & 0x3f, FBMSK: hi32(v) >>> 0 };
  },
  ZBUF: (v) => {
    const l = lo32(v);
    return { ZBP: l & 0x1ff, PSM: (l >> 24) & 0x3f, ZMSK: hi32(v) & 1 };
  },
  SCISSOR: (v) => {
    const l = lo32(v), h = hi32(v);
    return { SCAX0: l & 0x7ff, SCAX1: (l >> 16) & 0x7ff, SCAY0: h & 0x7ff, SCAY1: (h >> 16) & 0x7ff };
  },
  XYOFFSET: (v) => ({ OFX: lo32(v), OFY: hi32(v), OFX_px: lo32(v) / 16, OFY_px: hi32(v) / 16 }),
  bit0: (v) => lo32(v) & 1,
};
const PSM = {
  0x00: "PSMCT32", 0x01: "PSMCT24", 0x02: "PSMCT16", 0x0a: "PSMCT16S",
  0x13: "PSMT8", 0x14: "PSMT4", 0x1b: "PSMT8H", 0x24: "PSMT4HL",
  0x2c: "PSMT4HH", 0x30: "PSMZ32", 0x31: "PSMZ24", 0x32: "PSMZ16", 0x3a: "PSMZ16S",
};

// Running GS register file (addr -> bigint value). Set via A+D (PACKED) and
// direct register descriptors (REGLIST). Snapshotted into a draw at first kick.
const gsState = new Map();
const prims = []; // draw groups: { idx, PRIM, regs..., nverts, verts[] }
const dv = (addr, decoder) =>
  gsState.has(addr) ? decoder(gsState.get(addr)) : null;

const f32 = new DataView(new ArrayBuffer(4));
const i2f = (u) => (f32.setUint32(0, u >>> 0, true), f32.getFloat32(0, true));

function makeState(primField) {
  const ctxt = (primField >> 9) & 1; // 0 = context 1
  const c = (a1, a2) => (ctxt ? a2 : a1);
  return {
    PRIM: {
      type: primField & 7,
      IIP: (primField >> 3) & 1, TME: (primField >> 4) & 1, FGE: (primField >> 5) & 1,
      ABE: (primField >> 6) & 1, AA1: (primField >> 7) & 1, FST: (primField >> 8) & 1,
      CTXT: ctxt, FIX: (primField >> 10) & 1,
    },
    ALPHA: dv(c(0x42, 0x43), DEC.ALPHA),
    TEST: dv(c(0x47, 0x48), DEC.TEST),
    TEX0: dv(c(0x06, 0x07), DEC.TEX0),
    TEX1: dv(c(0x14, 0x15), DEC.TEX1),
    FRAME: dv(c(0x4c, 0x4d), DEC.FRAME),
    ZBUF: dv(c(0x4e, 0x4f), DEC.ZBUF),
    SCISSOR: dv(c(0x40, 0x41), DEC.SCISSOR),
    XYOFFSET: dv(c(0x18, 0x19), DEC.XYOFFSET),
    DTHE: dv(0x45, DEC.bit0),
    COLCLAMP: dv(0x46, DEC.bit0),
    PABE: dv(0x49, DEC.bit0),
    FBA: dv(c(0x4a, 0x4b), DEC.bit0),
  };
}

// --- GS vertex-kick state machine ---
// PRIM may be (re)loaded via the GIFtag PRE bit, an A+D PRIM write, or a REGLIST
// PRIM descriptor. Each (re)load starts a new draw group. Vertices accumulate on
// every XYZ kick into the current group, snapshotting the resolved register state
// at the group's first kick.
let curPrimField = 0;
let pendingGroup = false;
let curDraw = null;
let curColor = { r: 0, g: 0, b: 0, a: 0 };
let curTexUV = null;
let curTexST = null;

function setPrim(primField) {
  curPrimField = primField;
  pendingGroup = true;
  tally.primTypes[primField & 0x7ff] = (tally.primTypes[primField & 0x7ff] || 0) + 1;
}

function startGroupIfNeeded() {
  if (curDraw && !pendingGroup) return;
  curDraw = { idx: prims.length, ...makeState(curPrimField), nverts: 0, verts: [] };
  prims.push(curDraw);
  pendingGroup = false;
}

function emitVertex(xRaw, yRaw, z, f) {
  startGroupIfNeeded();
  const ofx = curDraw.XYOFFSET ? curDraw.XYOFFSET.OFX : 0;
  const ofy = curDraw.XYOFFSET ? curDraw.XYOFFSET.OFY : 0;
  const v = {
    x: (xRaw - ofx) / 16, y: (yRaw - ofy) / 16, z, // screen pixels
    r: curColor.r, g: curColor.g, b: curColor.b, a: curColor.a,
  };
  if (f !== undefined) v.f = f;
  if (curDraw.PRIM.FST) { if (curTexUV) { v.u = curTexUV.u; v.v = curTexUV.v; } }
  else if (curTexST) { v.s = curTexST.s; v.t = curTexST.t; }
  curDraw.verts.push(v);
  curDraw.nverts++;
  tally.kicks++;
}

function walkGif(p, end) {
  while (p + 16 <= end) {
    const a = buf.readUInt32LE(p);
    const b = buf.readUInt32LE(p + 4);
    const regsLo = buf.readUInt32LE(p + 8);
    const regsHi = buf.readUInt32LE(p + 12);
    p += 16;

    const nloop = a & 0x7fff;
    const eop = (a >>> 15) & 1;
    const pre = (b >>> 14) & 1;
    const prim = (b >>> 15) & 0x7ff;
    const flg = (b >>> 26) & 3;
    let nreg = (b >>> 28) & 0xf;
    if (nreg === 0) nreg = 16;

    tally.giftags++;
    tally.nloopSum += nloop;
    tally.flg[FLG[flg]]++;
    if (pre) setPrim(prim); // tag PRE reloads PRIM -> new draw group

    if (nloop === 0) continue;

    const regs = [];
    for (let i = 0; i < nreg; i++) {
      const src = i < 8 ? regsLo : regsHi;
      regs.push((src >>> ((i % 8) * 4)) & 0xf);
    }

    if (flg === 0) {
      // PACKED: nloop*nreg units of 16 bytes (GIFPacked* layout).
      for (let l = 0; l < nloop; l++) {
        for (let r = 0; r < nreg; r++) {
          handlePacked(regs[r], p);
          p += 16;
        }
      }
    } else if (flg === 1) {
      // REGLIST: nloop*nreg 64-bit regs (GIFReg* layout), padded to qword.
      let q = p;
      for (let l = 0; l < nloop; l++) {
        for (let r = 0; r < nreg; r++) {
          handleReglist(regs[r], buf.readBigUInt64LE(q));
          q += 8;
        }
      }
      p += ((nloop * nreg + 1) >> 1) * 16;
    } else {
      // IMAGE / IMAGE2: nloop qwords of raw data
      p += nloop * 16;
    }
    if (eop && p >= end) break;
  }
  if (p !== end) console.warn(`  WARN: gif walk Δ${end - p} bytes`);
}

// PACKED descriptor unit at byte offset `o` (GIFPacked* 128-bit layouts).
function handlePacked(desc, o) {
  const w0 = buf.readUInt32LE(o), w1 = buf.readUInt32LE(o + 4);
  const w2 = buf.readUInt32LE(o + 8), w3 = buf.readUInt32LE(o + 12);
  switch (desc) {
    case 0x00: setPrim(w0 & 0x7ff); break;
    case 0x01: curColor = { r: w0 & 0xff, g: w1 & 0xff, b: w2 & 0xff, a: w3 & 0xff }; break;
    case 0x02: curTexST = { s: i2f(w0), t: i2f(w1) }; break;
    case 0x03: curTexUV = { u: (w0 & 0x3fff) / 16, v: (w1 & 0x3fff) / 16 }; break;
    case 0x04: emitVertex(w0 & 0xffff, w1 & 0xffff, (w2 >>> 4) & 0xffffff, (w3 >>> 4) & 0xff); break;
    case 0x05: emitVertex(w0 & 0xffff, w1 & 0xffff, w2 >>> 0); break;
    case 0x0e: { // A+D: value = low qword, addr at byte 8
      const addr = buf.readUInt8(o + 8);
      tally.adRegs[addr] = (tally.adRegs[addr] || 0) + 1;
      const val = buf.readBigUInt64LE(o);
      gsState.set(addr, val);
      if (addr === 0x00) setPrim(Number(val & 0x7ffn));
      break;
    }
    default: break; // FOG/XYZ3 etc unused by the clock
  }
}

// REGLIST descriptor: one 64-bit value (GIFReg* layout).
function handleReglist(desc, val) {
  const lo = Number(val & 0xffffffffn), hi = Number((val >> 32n) & 0xffffffffn);
  switch (desc) {
    case 0x00: setPrim(lo & 0x7ff); break;
    case 0x01: curColor = { r: lo & 0xff, g: (lo >>> 8) & 0xff, b: (lo >>> 16) & 0xff, a: (lo >>> 24) & 0xff }; break;
    case 0x02: curTexST = { s: i2f(lo), t: i2f(hi) }; break;
    case 0x03: curTexUV = { u: (lo & 0x3fff) / 16, v: ((lo >>> 16) & 0x3fff) / 16 }; break;
    case 0x04: emitVertex(lo & 0xffff, (lo >>> 16) & 0xffff, hi & 0xffffff, (hi >>> 24) & 0xff); break;
    case 0x05: emitVertex(lo & 0xffff, (lo >>> 16) & 0xffff, hi >>> 0); break;
    case 0x0f: break; // NOP
    default: gsState.set(desc, val); break; // setup register (TEX0=6, CLAMP=8, ...)
  }
}

// --- packet loop (GSLzma.cpp:182) ---
while (off < buf.length) {
  const id = u8();
  if (id === 0) {
    const pathIdx = u8();
    const len = u32();
    const start = off;
    off += len;
    tally.transfers++;
    const pn = pathName[pathIdx] ?? `path${pathIdx}`;
    transfersByPath[pn] = (transfersByPath[pn] || 0) + 1;
    walkGif(start, start + len);
  } else if (id === 1) {
    off += 1; // field byte
    tally.vsync++;
  } else if (id === 2) {
    off += 4; // size
    tally.readfifo++;
  } else if (id === 3) {
    off += 8192; // GSPrivRegSet
    tally.regs++;
  } else {
    console.warn(`unknown packet id=${id} @${off - 1} — stopping`);
    break;
  }
}

// GIF A+D register map — verbatim from pcsx2-ref GSRegs.h:74 enum GIF_A_D_REG.
const GS_REG = {
  0x00: "PRIM", 0x01: "RGBAQ", 0x02: "ST", 0x03: "UV",
  0x04: "XYZF2", 0x05: "XYZ2", 0x06: "TEX0_1", 0x07: "TEX0_2",
  0x08: "CLAMP_1", 0x09: "CLAMP_2", 0x0a: "FOG", 0x0c: "XYZF3",
  0x0d: "XYZ3", 0x0f: "NOP", 0x14: "TEX1_1", 0x15: "TEX1_2",
  0x16: "TEX2_1", 0x17: "TEX2_2", 0x18: "XYOFFSET_1", 0x19: "XYOFFSET_2",
  0x1a: "PRMODECONT", 0x1b: "PRMODE", 0x1c: "TEXCLUT", 0x22: "SCANMSK",
  0x34: "MIPTBP1_1", 0x35: "MIPTBP1_2", 0x36: "MIPTBP2_1", 0x37: "MIPTBP2_2",
  0x3b: "TEXA", 0x3d: "FOGCOL", 0x3f: "TEXFLUSH", 0x40: "SCISSOR_1",
  0x41: "SCISSOR_2", 0x42: "ALPHA_1", 0x43: "ALPHA_2", 0x44: "DIMX",
  0x45: "DTHE", 0x46: "COLCLAMP", 0x47: "TEST_1", 0x48: "TEST_2",
  0x49: "PABE", 0x4a: "FBA_1", 0x4b: "FBA_2", 0x4c: "FRAME_1",
  0x4d: "FRAME_2", 0x4e: "ZBUF_1", 0x4f: "ZBUF_2", 0x50: "BITBLTBUF",
  0x51: "TRXPOS", 0x52: "TRXREG", 0x53: "TRXDIR", 0x54: "HWREG",
  0x60: "SIGNAL", 0x61: "FINISH", 0x62: "LABEL", 0x7f: "NOP-pad",
};

console.log("\n=== packet stream ===");
console.log(`transfers ${tally.transfers}  vsync ${tally.vsync}  readfifo ${tally.readfifo}  regs ${tally.regs}`);
console.log("transfers by path:", transfersByPath);
const vertSum = prims.reduce((n, p) => n + p.nverts, 0);
console.log(`\ngiftags ${tally.giftags}  nloop(sum) ${tally.nloopSum}  draws ${prims.length}  kicks/verts ${tally.kicks}`);
console.log("FLG:", tally.flg);
console.log(
  "PRIM types (all loads):",
  Object.entries(tally.primTypes).reduce((m, [k, v]) => {
    const n = primName(+k & 7);
    m[n] = (m[n] || 0) + v;
    return m;
  }, {}),
);
console.log("\n=== A+D register writes ===");
const adSorted = Object.entries(tally.adRegs).sort((a, b) => b[1] - a[1]);
for (const [addr, n] of adSorted) {
  const a = +addr;
  const name = GS_REG[a] ?? "?";
  console.log(`  0x${a.toString(16).padStart(2, "0")} ${name.padEnd(11)} ${n}`);
}

// --- per-primitive decoded state + unique configs ---
function uniq(sel, fmt) {
  const m = new Map();
  for (const pr of prims) {
    const v = sel(pr);
    if (v == null) continue;
    const k = fmt(v);
    m.set(k, (m.get(k) || 0) + 1);
  }
  return [...m.entries()].sort((a, b) => b[1] - a[1]);
}
const ABCD = "Cs Cd 0".split(" "); // ALPHA A/B/D operand: 0=Cs,1=Cd,2=0
const Cval = "As Ad FIX".split(" "); // ALPHA C operand: 0=As,1=Ad,2=FIX
const ATST = ["NEVER", "ALWAYS", "LESS", "LEQUAL", "EQUAL", "GEQUAL", "GREATER", "NOTEQUAL"];
const AFAIL = ["KEEP", "FB_ONLY", "ZB_ONLY", "RGB_ONLY"];
const ZTST = ["NEVER", "ALWAYS", "GEQUAL", "GREATER"];

console.log(`\n=== ${prims.length} draws / ${vertSum} verts decoded ===`);
console.log(
  "draw sizes (nverts -> count):",
  prims.reduce((m, p) => ((m[p.nverts] = (m[p.nverts] || 0) + 1), m), {}),
);
console.log(
  "\nPRIM flags:",
  uniq(
    (p) => p.PRIM,
    (v) =>
      `${primName(v.type)} IIP${v.IIP} TME${v.TME} FGE${v.FGE} ABE${v.ABE} AA1${v.AA1} FST${v.FST} CTXT${v.CTXT}`,
  ),
);
console.log(
  "\nALPHA (A-B)*C/128+D:",
  uniq(
    (p) => p.ALPHA,
    (v) => `(${ABCD[v.A]}-${ABCD[v.B]})*${Cval[v.C]}/128+${ABCD[v.D]}  FIX=${v.FIX}`,
  ),
);
console.log(
  "\nTEST:",
  uniq(
    (p) => p.TEST,
    (v) =>
      `ATE${v.ATE} ${ATST[v.ATST]} AREF${v.AREF} AFAIL=${AFAIL[v.AFAIL]} DATE${v.DATE} ZTE${v.ZTE} ZTST=${ZTST[v.ZTST]}`,
  ),
);
console.log(
  "\nTEX0:",
  uniq(
    (p) => p.TEX0,
    (v) =>
      `TBP0=${v.TBP0} TBW=${v.TBW} PSM=${PSM[v.PSM] ?? v.PSM} ${1 << v.TW}x${1 << v.TH} TCC${v.TCC} TFX${v.TFX} CPSM=${PSM[v.CPSM] ?? v.CPSM} CSA${v.CSA}`,
  ),
);
console.log(
  "\nFRAME:",
  uniq(
    (p) => p.FRAME,
    (v) => `FBP=${v.FBP}(*2048) FBW=${v.FBW}(*64px) PSM=${PSM[v.PSM] ?? v.PSM} FBMSK=0x${(v.FBMSK >>> 0).toString(16)}`,
  ),
);
console.log("\nscalar toggles across prims:");
for (const key of ["DTHE", "COLCLAMP", "PABE", "FBA"])
  console.log(`  ${key}:`, uniq((p) => p[key], (v) => String(v)));

const outPath = "tools/gsdump/clock_prims.json";
writeFileSync(outPath, JSON.stringify(prims, null, 1));
console.log(`\nwrote ${prims.length} decoded primitives -> ${outPath}`);

function primName(p) {
  return (
    [
      "POINT",
      "LINE",
      "LINE_STRIP",
      "TRI",
      "TRI_STRIP",
      "TRI_FAN",
      "SPRITE",
      "INVALID",
    ][p] + `(${p})`
  );
}
