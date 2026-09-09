#!/usr/bin/env node
/**
 * Slice curated Weather Icons SVGs into uncompressed TTI1 bitmaps.
 *
 * Usage:
 *   cd tools/icons && npm install && node slice_weather_icons.mjs
 */

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { Resvg } from "@resvg/resvg-js";
import { maskToI1 } from "./write_i1.mjs";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const MANIFEST_PATH = path.join(__dirname, "weather.json");

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

function cropMask(mask, width, height) {
  let x0 = width;
  let y0 = height;
  let x1 = -1;
  let y1 = -1;
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      if (!mask[y * width + x]) {
        continue;
      }
      if (x < x0) x0 = x;
      if (y < y0) y0 = y;
      if (x > x1) x1 = x;
      if (y > y1) y1 = y;
    }
  }
  if (x1 < 0) {
    return { mask, width, height };
  }
  const cropW = x1 - x0 + 1;
  const cropH = y1 - y0 + 1;
  const out = new Uint8Array(cropW * cropH);
  for (let y = 0; y < cropH; y++) {
    for (let x = 0; x < cropW; x++) {
      out[y * cropW + x] = mask[(y + y0) * width + (x + x0)];
    }
  }
  return { mask: out, width: cropW, height: cropH };
}

function padSquare(mask, width, height) {
  const side = Math.max(width, height);
  if (side === width && side === height) {
    return { mask, width, height };
  }
  const out = new Uint8Array(side * side);
  const ox = Math.floor((side - width) / 2);
  const oy = Math.floor((side - height) / 2);
  for (let y = 0; y < height; y++) {
    out.set(mask.subarray(y * width, y * width + width), (y + oy) * side + ox);
  }
  return { mask: out, width: side, height: side };
}

function scaleMaskNearest(mask, width, height, destW, destH) {
  if (width === destW && height === destH) {
    return { mask, width, height };
  }
  const out = new Uint8Array(destW * destH);
  for (let y = 0; y < destH; y++) {
    for (let x = 0; x < destW; x++) {
      const sx = Math.min(width - 1, Math.floor(((x + 0.5) * width) / destW));
      const sy = Math.min(height - 1, Math.floor(((y + 0.5) * height) / destH));
      out[y * destW + x] = mask[sy * width + sx];
    }
  }
  return { mask: out, width: destW, height: destH };
}


function underscoredId(id) {
  return id.replace(/-/g, "_");
}

function resolveSvg(id, svgDir, sourceDir) {
  const localSvg = path.join(svgDir, `${id}.svg`);
  const sourceSvg = path.join(sourceDir, `${id}.svg`);
  if (fs.existsSync(localSvg)) {
    return localSvg;
  }
  if (fs.existsSync(sourceSvg)) {
    fs.copyFileSync(sourceSvg, localSvg);
    return localSvg;
  }
  throw new Error(`Weather SVG not found: ${id}.svg`);
}

function sliceOne(id, destW, destH, outPath, svgDir, sourceDir) {
  const srcSvg = resolveSvg(id, svgDir, sourceDir);
  const raw = fs.readFileSync(srcSvg, "utf8");
  const wrapped = prepareSvg(raw);
  const renderW = Math.max(destW * 4, 128);
  const rendered = new Resvg(wrapped, {
    fitTo: { mode: "width", value: renderW },
    background: "white",
  }).render();

  let sliced = thresholdMask(rendered.pixels, rendered.width, rendered.height, 96);
  if (destW >= 48) {
    sliced = cropMask(sliced.mask, sliced.width, sliced.height);
    sliced = padSquare(sliced.mask, sliced.width, sliced.height);
  }
  sliced = scaleMaskNearest(sliced.mask, sliced.width, sliced.height, destW, destH);
  const i1 = maskToI1(sliced.mask, sliced.width, sliced.height);
  fs.mkdirSync(path.dirname(outPath), { recursive: true });
  fs.writeFileSync(outPath, i1);
  console.log(
    `sliced ${id} -> ${path.relative(path.join(__dirname, "../.."), outPath)} ` +
      `(${sliced.width}x${sliced.height}, ${i1.length} bytes)`
  );
}

function main() {
  const manifest = loadManifest();
  const entries = manifest.icons;
  if (!entries || entries.length === 0) {
    console.log("No weather icons in weather.json");
    return;
  }

  const svgDir = path.join(__dirname, manifest.svgDir);
  const sourceDir = path.resolve(__dirname, manifest.sourceDir);
  const outputDir = path.resolve(__dirname, manifest.outputDir);
  fs.mkdirSync(svgDir, { recursive: true });
  fs.mkdirSync(outputDir, { recursive: true });

  console.log(`Weather SVG source: ${sourceDir}`);
  console.log(`Local SVG cache: ${svgDir}`);

  for (const entry of entries) {
    if (entry.file && entry.size) {
      const outPath = path.resolve(__dirname, manifest.outputDir, entry.file);
      sliceOne(entry.id, entry.size, entry.size, outPath, svgDir, sourceDir);
      continue;
    }
    const sizes = entry.sizes || [entry.size];
    for (const size of sizes) {
      if (!size) {
        throw new Error(`Missing size for ${entry.id}`);
      }
      const file = `${underscoredId(entry.outId || entry.id)}_${size}.i1`;
      if (file.length > 31) {
        throw new Error(`LittleFS name too long (${file.length}): ${file}`);
      }
      const outPath = path.join(outputDir, file);
      sliceOne(entry.id, size, size, outPath, svgDir, sourceDir);
    }
  }
}

main();
