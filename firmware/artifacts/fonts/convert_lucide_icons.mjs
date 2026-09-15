// Convert Lucide SVG icons to the 24x24 1-bit C arrays in src/icons.h.
//
// Same output as convert_lucide_icons.py, but rasterised with resvg (wasm) so
// it needs neither rsvg-convert nor ImageMagick — only `npm install` here.
//
//   cd artifacts/fonts && npm install && node convert_lucide_icons.mjs > ../../src/icons.h
//   node convert_lucide_icons.mjs --debug     # ASCII art to stderr instead
//   node convert_lucide_icons.mjs --all       # re-rasterise icons already in icons.h too
//
// By default icons that already exist in src/icons.h are copied through
// byte-for-byte and only new names are rasterised. resvg and rsvg/ImageMagick
// anti-alias differently (a few dozen pixels per icon after thresholding), and
// the existing icons were tuned by eye on the panel, so a regenerate should not
// quietly restyle them. --all opts into that.
//
// Bit set = stroke (drawn in black on the panel's white, or white on a black
// button; the renderer decides). MSB first, 3 bytes per row.

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import { createRequire } from "node:module";
import { Resvg, initWasm } from "@resvg/resvg-wasm";

const require = createRequire(import.meta.url);
const here = dirname(fileURLToPath(import.meta.url));

// (svg file, C name) — order is the order in icons.h
const ICONS = [
  ["lucide-battery.svg", "BATTERY_EMPTY"],
  ["lucide-battery-low.svg", "BATTERY_LOW"],
  ["lucide-battery-medium.svg", "BATTERY_MED"],
  ["lucide-battery-full.svg", "BATTERY_FULL"],
  ["lucide-signal-low.svg", "SIGNAL_1"],
  ["lucide-signal-medium.svg", "SIGNAL_2"],
  ["lucide-signal-high.svg", "SIGNAL_3"],
  ["lucide-signal.svg", "SIGNAL_FULL"],
  ["lucide-mail.svg", "MAIL"],
  ["lucide-send.svg", "SEND"],
  ["lucide-trash-2.svg", "TRASH"],
  ["lucide-images.svg", "IMAGES"],
  ["lucide-arrow-right.svg", "ARROW_RIGHT"],
  ["lucide-camera.svg", "CAMERA"],
];

const SIZE = 24;
const BYTES_PER_ROW = 3;
const THRESHOLD = 128;

await initWasm(readFileSync(require.resolve("@resvg/resvg-wasm/index_bg.wasm")));

function rasterise(file) {
  const svg = readFileSync(join(here, file), "utf8").replaceAll("currentColor", "#000000");
  const png = new Resvg(svg, {
    fitTo: { mode: "width", value: SIZE },
    background: "#ffffff",
  }).render();
  const { width, height } = png;
  const rgba = png.pixels; // straight RGBA, white already composited
  const bitmap = [];
  for (let y = 0; y < SIZE; y++) {
    const row = [];
    for (let x = 0; x < SIZE; x++) {
      let v = 255;
      if (x < width && y < height) {
        const i = (y * width + x) * 4;
        v = (rgba[i] + rgba[i + 1] + rgba[i + 2]) / 3;
      }
      row.push(v < THRESHOLD ? 1 : 0);
    }
    bitmap.push(row);
  }
  return bitmap;
}

function toCArray(bitmap, name) {
  const lines = [`static const uint8_t ${name}[${SIZE} * ${BYTES_PER_ROW}] = {`];
  for (const row of bitmap) {
    const bytes = [];
    for (let b = 0; b < BYTES_PER_ROW; b++) {
      let v = 0;
      for (let bit = 0; bit < 8; bit++) {
        const px = b * 8 + bit;
        if (px < SIZE && row[px]) v |= 1 << (7 - bit);
      }
      bytes.push("0x" + v.toString(16).toUpperCase().padStart(2, "0"));
    }
    lines.push(`    ${bytes.join(", ")},`);
  }
  lines.push("};");
  return lines.join("\n");
}

const debug = process.argv.includes("--debug");
const all = process.argv.includes("--all");

// Existing arrays from the committed header, keyed by C name.
const existing = new Map();
try {
  const current = readFileSync(join(here, "../../src/icons.h"), "utf8");
  for (const m of current.matchAll(/static const uint8_t (\w+)\[24 \* 3\] = \{[\s\S]*?\};/g)) {
    existing.set(m[1], m[0]);
  }
} catch {
  // No header yet: everything gets rasterised.
}

const out = [];
if (!debug) {
  out.push("#pragma once", "",
    "// Auto-generated from Lucide SVG icons",
    "// Run from artifacts/fonts: npm install && node convert_lucide_icons.mjs > ../../src/icons.h",
    "", "#include <stdint.h>", "", "namespace Icons {", "", `constexpr int SIZE = ${SIZE};`, "",
    "// Empty signal (no bars)",
    toCArray(Array.from({ length: SIZE }, () => new Array(SIZE).fill(0)), "SIGNAL_0"), "");
}
for (const [file, name] of ICONS) {
  if (!debug && !all && existing.has(name)) {
    out.push(`// ${file}`, existing.get(name), "");
    continue;
  }
  const bitmap = rasterise(file);
  if (debug) {
    process.stderr.write(`${name} (${file})\n` + bitmap.map(r => r.map(p => (p ? "#" : ".")).join("")).join("\n") + "\n\n");
  } else {
    out.push(`// ${file}`, toCArray(bitmap, name), "");
  }
}
if (!debug) {
  out.push("}  // namespace Icons", "");
  process.stdout.write(out.join("\n"));
}
