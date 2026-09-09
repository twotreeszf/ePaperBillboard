#!/usr/bin/env node
/**
 * Convert existing PNG icons to TTI1 without re-slicing SVG.
 *
 * Usage:
 *   node tools/icons/png_to_i1.mjs <png...>
 */

import fs from "node:fs";
import path from "node:path";
import { PNG } from "pngjs";
import { maskToI1 } from "./write_i1.mjs";

function pngToMask(png) {
  const mask = new Uint8Array(png.width * png.height);
  for (let i = 0; i < mask.length; i++) {
    const o = i * 4;
    if (png.data[o + 3] < 128) {
      continue;
    }
    const lum = (png.data[o] * 77 + png.data[o + 1] * 150 + png.data[o + 2] * 29) >> 8;
    if (lum <= 128) {
      mask[i] = 1;
    }
  }
  return mask;
}

function convertOne(pngPath) {
  const raw = fs.readFileSync(pngPath);
  const png = PNG.sync.read(raw);
  const i1 = maskToI1(pngToMask(png), png.width, png.height);
  const outPath = pngPath.replace(/\.png$/i, ".i1");
  fs.writeFileSync(outPath, i1);
  console.log(
    `converted ${path.basename(pngPath)} -> ${path.basename(outPath)} ` +
      `(${png.width}x${png.height}, ${i1.length} bytes)`
  );
}

function main() {
  const files = process.argv.slice(2);
  if (files.length === 0) {
    console.error("usage: node png_to_i1.mjs <file.png...>");
    process.exit(1);
  }
  for (const file of files) {
    convertOne(file);
  }
}

main();
