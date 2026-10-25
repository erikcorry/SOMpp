#!/usr/bin/env node
// Draw bar charts of the fructus cycle counts of the cut-down DeltaBlue and
// Richards, one bar per commit, as SVG and as an 800x800 PNG.
//
//     tools/cycle-graphs.mjs [outdir]
//
// The counts before the table in ideas/deltablue-profile-fructus.md are mined
// from the commit messages, below. The rows of the table are added after them,
// or replace the mined count of the same commit. The PNG needs resvg.

import { readFileSync, writeFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const outdir = process.argv[2] ?? '.';

// A bar has a commit, a label (null for a jump no commit explains), and either
// its cycles or a pct, the change from the bar before. A bar with neither gets
// its count from the pct of the bar after it.
const MINED = {
  DeltaBlue: [
    { commit: '9670ac48', label: 'Starting point', cycles: 7.36e6 },   // c69ba1c3
    { commit: 'c69ba1c3', label: 'Box big ints, 8K heap', cycles: 8.88e6 },
    { commit: '7ab4c86c', label: 'Heap back to 16K', cycles: 7.433e6 }, // f0b6c1cf
    { commit: 'f0b6c1cf', label: 'Share int slow path', cycles: 7.385e6 },
    { commit: 'd625e4e2', label: 'Faster + - *', cycles: 7.384e6 },
    { commit: '371fccf9', label: 'Retry failed sends', cycles: 7395206 }, // 04331856
    { commit: '04331856', label: 'Retry increments', cycles: 7394716 },
    { commit: 'a73b63d6', label: 'New GC, bigger heap', cycles: 5.09e6 }, // 2487d5cb
    // Its message says 5.09M to 5.03M, but measured again it is 2% slower:
    // its 224 more bytes of writable data take that much heap, so DeltaBlue
    // collects more. f66bcb18 changes nothing.
    { commit: '2487d5cb', label: 'SEND_EQEQ' },
    { commit: '10f97a4d', label: 'Small int identity', pct: -0.06, cycles: 5.12e6 }, // 13aeca1e
    { commit: '13aeca1e', label: 'STATIC_SEND', cycles: 4.90e6 },
    { commit: '15e27e5a', label: 'Class IDs', pct: -0.24 },
    // Its -10% doesn't chain onto the counts before it.
    { commit: '9ab5c8a4', label: 'ID range dispatch' },
    { commit: 'ad04a24d', label: 'Unsigned classId', pct: -0.16 },
  ],
  Richards: [
    { commit: '7ab4c86c', label: 'Starting point', cycles: 193.3e6 },   // f0b6c1cf
    { commit: 'f0b6c1cf', label: 'Share int slow path', cycles: 192.7e6 },
    { commit: 'd625e4e2', label: 'Faster + - *', cycles: 192.64e6 },
    { commit: 'before 2487d5cb', label: 'New GC, bigger heap', cycles: 144.9e6 },
    { commit: '2487d5cb', label: 'SEND_EQEQ', cycles: 138.0e6 },
    { commit: '10f97a4d', label: 'Small int identity', pct: -0.12 },
    { commit: '13aeca1e', label: 'STATIC_SEND', cycles: 135.6e6 },
    { commit: '15e27e5a', label: 'Class IDs', pct: +0.09 },
    { commit: '9ab5c8a4', label: 'ID range dispatch' },
    { commit: 'ad04a24d', label: 'Unsigned classId', pct: -0.13 },
  ],
};

// Shorter labels for the notes of the table, which may not fit in a bar.
const SHORT = {
  '26793a87': 'New fructus gcc',
  '0442f246': 'Stack-relative vars',
  '77938552': 'SP in a register',
  '9180e1de': 'Remember the frame',
  'e62e7f7d': 'Barrier-free args',
  'cde6bc72': 'New fructus CPU',
  '6bfa2245': 'Frame stack',
  '67f69752': 'No barrier into current frame',
  'f7672260': 'Stack and heap frames',
  '8016517d': 'Pass sp into Invoke',
  'b4cfc11a': 'Self in the interpreter',
  // The rows after them are named by the fructus commit their note starts with.
  'c51921e': 'Faster absolute jump/call',
  'd170fdf': 'Dynamic branch predictor',
  'af28add': 'Indirect jump target predictor',
  'ec0304c': 'Return address predictor',
  '5f0f2af': 'Early branch target',
  '9f7d80e': 'Stores keep dispatching',
  '5b70889': 'Newer gcc',
  '68fad41': 'Two-cycle loads',
  'f840fa3': 'No indirect jump predictor',
};

// Rows the charts leave out, to keep the bars few; the table keeps them.  A
// left-out row's change shows in the next bar, whose count includes it, or,
// if FOLD, in the bar before, which takes the row's count.
const SKIP = new Set(['9e49ec9', '6b4c7f0', 'ebd3f71', '76d6d4f', '3893847', '6c667ac']);
const FOLD = new Set(['ae14399', '0aa5f42']);

function readTable() {
  const text = readFileSync(join(root, 'ideas/deltablue-profile-fructus.md'), 'utf8');
  const rows = [];
  for (const line of text.split('\n')) {
    const m = line.match(/^\|\s*([0-9a-f]{7,40})\s*\|(.*)\|\s*$/);
    if (!m) continue;
    const cells = m[2].split('|').map((c) => c.trim());
    const num = (c) => (c ? Number(c.replace(/,/g, '')) : null);
    rows.push({
      commit: m[1],
      DeltaBlue: num(cells[0]),
      Richards: num(cells[2]),
      note: cells[4],
    });
  }
  return rows;
}

function bars(benchmark, table) {
  const list = MINED[benchmark].map((b) => ({ ...b }));
  const mined = [...list];
  for (const row of table) {
    const cycles = row[benchmark];
    if (cycles == null) continue;
    const bar = mined.find((b) => b.commit === row.commit);
    if (bar) {
      bar.cycles = cycles;
    } else {
      const fructus = row.note.match(/^fructus ([0-9a-f]{7})/)?.[1];
      if (SKIP.has(fructus)) continue;
      if (FOLD.has(fructus)) {
        list[list.length - 1].cycles = cycles;
        continue;
      }
      list.push({ commit: row.commit, label: SHORT[row.commit] ?? SHORT[fructus] ?? row.note,
                  cycles });
    }
  }
  for (let changed = true; changed; ) {
    changed = false;
    list.forEach((b, i) => {
      const prev = list[i - 1];
      if (b.cycles == null && b.pct != null && prev?.cycles != null) {
        b.cycles = prev.cycles * (1 + b.pct / 100);
        changed = true;
      } else if (b.cycles != null && b.pct != null && prev && prev.cycles == null &&
                 prev.pct == null) {
        prev.cycles = b.cycles / (1 + b.pct / 100);
        changed = true;
      }
    });
  }
  const missing = list.filter((b) => b.cycles == null);
  if (missing.length) {
    throw new Error(`${benchmark}: no count for ${missing.map((b) => b.commit).join(', ')}`);
  }
  return list;
}

const SIZE = 800;
const FONT = "'DejaVu Sans', 'Liberation Sans', sans-serif";
const COLOR = {
  surface: '#fcfcfb',
  text: '#0b0b0b',
  secondary: '#52514e',
  muted: '#8a8984',
  grid: '#e4e3df',
  bar: '#2a78d6',
  unexplained: '#c3c2bc',
  barText: '#ffffff',
};

const esc = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
// DejaVu Sans averages a bit under 0.6em per character.
const textWidth = (s, size) => s.length * size * 0.6;

function formatCycles(cycles, max) {
  return `${(cycles / 1e6).toFixed(max < 20e6 ? 3 : 1)}M`;
}

function svg(benchmark, list) {
  const first = list[0].cycles;
  const last = list[list.length - 1].cycles;
  const max = Math.max(...list.map((b) => b.cycles));
  const change = Math.round((last / first - 1) * 100);

  const left = 32, right = SIZE - 24, base = 730, top = 250;
  const valueRoom = 78;  // for the vertical counts above the tallest bar
  const slot = (right - left) / list.length;
  const gap = Math.max(2, slot * 0.18);
  const width = slot - gap;
  const labelSize = Math.min(15, width * 0.55);
  const valueSize = Math.min(14, width * 0.5);
  // The labels go in the bars, at 10px or more, and the counts above them;
  // if a label doesn't fit, they swap, the labels above, as high as below the
  // headline, and the counts inside.
  let scale = (base - top - valueRoom) / max;
  const swap = list.some((b) => b.label && textWidth(b.label, 10) > b.cycles * scale - 16);
  if (swap) {
    scale = Math.min(...list.map((b) =>
      (base - 200 - 6 - (b.label ? textWidth(b.label, labelSize) : 0)) / b.cycles));
  }

  const out = [];
  out.push(`<svg xmlns="http://www.w3.org/2000/svg" width="${SIZE}" height="${SIZE}" ` +
           `viewBox="0 0 ${SIZE} ${SIZE}" font-family="${FONT}">`);
  out.push(`<rect width="${SIZE}" height="${SIZE}" fill="${COLOR.surface}"/>`);
  out.push(`<text x="${left}" y="64" font-size="34" font-weight="bold" fill="${COLOR.text}">` +
           `${benchmark} on fructus</text>`);
  out.push(`<text x="${left}" y="98" font-size="18" fill="${COLOR.secondary}">` +
           `Cycles to run the cut-down benchmark, per commit. Lower is better.</text>`);
  out.push(`<text x="${left}" y="170" font-size="52" font-weight="bold" fill="${COLOR.text}">` +
           `${change > 0 ? '+' : '−'}${Math.abs(change)}%</text>`);
  out.push(`<text x="${left + 190}" y="152" font-size="18" fill="${COLOR.secondary}">` +
           `${formatCycles(first, max)} → ${formatCycles(last, max)} cycles</text>`);
  out.push(`<text x="${left + 190}" y="176" font-size="18" fill="${COLOR.secondary}">` +
           `on a 16-bit CPU with no floats</text>`);

  list.forEach((b, i) => {
    const x = left + i * slot + gap / 2;
    const h = b.cycles * scale;
    const y = base - h;
    const r = Math.min(4, width / 2);
    const fill = b.label ? COLOR.bar : COLOR.unexplained;
    out.push(`<path d="M${x},${base} V${y + r} Q${x},${y} ${x + r},${y} ` +
             `H${x + width - r} Q${x + width},${y} ${x + width},${y + r} V${base} Z" ` +
             `fill="${fill}"><title>${esc(b.commit)}</title></path>`);
    const cx = x + width / 2;
    if (swap) {
      out.push(`<text transform="translate(${cx + valueSize * 0.36},${base - 8}) rotate(-90)" ` +
               `font-size="${valueSize}" fill="${b.label ? COLOR.barText : COLOR.text}">` +
               `${formatCycles(b.cycles, max)}</text>`);
      if (b.label) {
        out.push(`<text transform="translate(${cx + labelSize * 0.36},${y - 6}) rotate(-90)" ` +
                 `font-size="${labelSize.toFixed(1)}" fill="${COLOR.text}">${esc(b.label)}</text>`);
      }
      return;
    }
    out.push(`<text transform="translate(${cx + valueSize * 0.36},${y - 6}) rotate(-90)" ` +
             `font-size="${valueSize}" fill="${COLOR.text}">${formatCycles(b.cycles, max)}</text>`);
    if (b.label) {
      let label = b.label;
      let size = labelSize;
      const room = h - 16;
      if (textWidth(label, size) > room) size = Math.max(10, room / (label.length * 0.6));
      while (label.length > 1 && textWidth(label, size) > room) label = label.slice(0, -2) + '…';
      out.push(`<text transform="translate(${cx + size * 0.36},${base - 8}) rotate(-90)" ` +
               `font-size="${size.toFixed(1)}" fill="${COLOR.barText}">${esc(label)}</text>`);
    }
  });

  out.push(`<line x1="${left}" x2="${right}" y1="${base}" y2="${base}" ` +
           `stroke="${COLOR.muted}" stroke-width="1"/>`);
  if (list.some((b) => !b.label)) {
    out.push(`<rect x="${left}" y="${base + 26}" width="14" height="14" rx="2" ` +
             `fill="${COLOR.unexplained}"/>`);
    out.push(`<text x="${left + 22}" y="${base + 38}" font-size="15" fill="${COLOR.secondary}">` +
             `Grey: a change between measurements that no single commit explains</text>`);
  }
  out.push('</svg>');
  return out.join('\n');
}

const table = readTable();
for (const benchmark of Object.keys(MINED)) {
  const name = join(outdir, `${benchmark.toLowerCase()}-cycles`);
  writeFileSync(`${name}.svg`, svg(benchmark, bars(benchmark, table)));
  execFileSync('resvg', ['-w', String(SIZE), `${name}.svg`, `${name}.png`], { stdio: 'inherit' });
  console.log(`${name}.svg ${name}.png`);
}
