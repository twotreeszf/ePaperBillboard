#!/usr/bin/env node
/**
 * Slice Pixelarticons (https://pixelarticons.com/) into high-contrast PNGs.
 *
 * Usage:
 *   cd tools/icons && npm install && node slice_pixelart_icons.mjs
 */

import { createRequire } from "node:module";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { Resvg } from "@resvg/resvg-js";
import { PNG } from "pngjs";

const require = createRequire(import.meta.url);
const __dirname = path.dirname(fileURLToPath(import.meta.url));
const MANIFEST_PATH = path.join(__dirname, "icons.json");
const PIXELART_ICONS_DIR = path.join(
  path.dirname(require.resolve("pixelarticons/package.json")),
  "svg"
);

function loadManifest() {
  return JSON.parse(fs.readFileSync(MANIFEST_PATH, "utf8"));
}

function prepareSvg(raw) {
  return raw
    .replace(/fill="currentColor"/g, 'fill="#000000"')
    .replace(/<svg\b/, '<svg xmlns="http://www.w3.org/2000/svg"');
}

function pixelLum(rgba, i) {
  const o = i * 4;
  if (rgba[o + 3] < 128) {
    return 255;
  }
  return (rgba[o] * 77 + rgba[o + 1] * 150 + rgba[o + 2] * 29) >> 8;
}

function thresholdMask(rgba, width, height, threshold) {
  const mask = new Uint8Array(width * height);
  for (let i = 0; i < width * height; i++) {
    if (pixelLum(rgba, i) <= threshold) {
      mask[i] = 1;
    }
  }
  return { mask, width, height };
}

function rotateMask90Cw(mask, width, height) {
  const out = new Uint8Array(width * height);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const nx = height - 1 - y;
      const ny = x;
      out[ny * height + nx] = mask[y * width + x];
    }
  }
  return { mask: out, width: height, height: width };
}

function maskToPng(mask, width, height) {
  const png = new PNG({
    width,
    height,
    colorType: 6,
    inputColorType: 6,
    inputHasAlpha: true,
  });
  for (let i = 0; i < width * height; i++) {
    const o = i * 4;
    const ink = mask[i] ? 0 : 255;
    png.data[o] = ink;
    png.data[o + 1] = ink;
    png.data[o + 2] = ink;
    png.data[o + 3] = 255;
  }
  return PNG.sync.write(png, { colorType: 6, inputHasAlpha: true });
}

function sliceIcon(entry, svgDir, outputDir) {
  const width = entry.width ?? entry.size;
  const height = entry.height ?? entry.size;
  const packagedSvg = path.join(PIXELART_ICONS_DIR, `${entry.id}.svg`);
  const localSvg = path.join(svgDir, `${entry.id}.svg`);
  const srcSvg = fs.existsSync(localSvg) ? localSvg : packagedSvg;
  if (!fs.existsSync(srcSvg)) {
    throw new Error(`Pixelarticons SVG not found: ${entry.id}.svg`);
  }
  const raw = fs.readFileSync(srcSvg, "utf8");
  if (srcSvg !== localSvg) {
    fs.writeFileSync(localSvg, raw);
  }

  const wrapped = prepareSvg(raw);
  const rendered = new Resvg(wrapped, {
    fitTo: { mode: "width", value: width },
    background: "white",
  }).render();

  if (rendered.width !== width || rendered.height !== height) {
    throw new Error(
      `${entry.id}: unexpected raster size ${rendered.width}x${rendered.height}, expected ${width}x${height}`
    );
  }

  let sliced = thresholdMask(rendered.pixels, rendered.width, rendered.height, 96);
  const turns = ((entry.rotate ?? 0) / 90) % 4;
  for (let i = 0; i < turns; i++) {
    sliced = rotateMask90Cw(sliced.mask, sliced.width, sliced.height);
  }
  const png = maskToPng(sliced.mask, sliced.width, sliced.height);
  const outPath = path.join(outputDir, entry.file);
  fs.writeFileSync(outPath, png);
  console.log(
    `sliced ${entry.id} -> ${path.relative(path.join(__dirname, "../.."), outPath)} ` +
      `(${sliced.width}x${sliced.height}, ${png.length} bytes)`
  );
}

function main() {
  const manifest = loadManifest();
  const entries = manifest.pixelart && manifest.pixelart.icons;
  if (!entries || entries.length === 0) {
    console.log("No pixelart icons in icons.json");
    return;
  }

  const svgDir = path.join(__dirname, manifest.svgDir);
  const outputDir = path.resolve(__dirname, manifest.outputDir);
  fs.mkdirSync(svgDir, { recursive: true });
  fs.mkdirSync(outputDir, { recursive: true });

  console.log(`Pixelarticons source: ${manifest.pixelart.source}`);
  console.log(`pixelarticons svg: ${PIXELART_ICONS_DIR}`);
  for (const entry of entries) {
    sliceIcon(entry, svgDir, outputDir);
  }
}

main();
