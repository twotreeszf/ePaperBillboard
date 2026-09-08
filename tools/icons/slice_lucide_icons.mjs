#!/usr/bin/env node
/**
 * Slice Lucide icons (https://lucide.dev/) into high-contrast PNGs for e-paper.
 *
 * Usage (from repo root or this directory):
 *   cd tools/icons && npm install && node slice_lucide_icons.mjs
 *
 * Reads icons.json, copies source SVGs into svg/, renders at 2x, averages to
 * the target size, then thresholds so Lucide stroke weight is preserved.
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
const LUCIDE_ICONS_DIR = path.join(
  path.dirname(require.resolve("lucide-static/package.json")),
  "icons"
);

function loadManifest() {
  return JSON.parse(fs.readFileSync(MANIFEST_PATH, "utf8"));
}

function prepareSvg(raw, size, strokeWidth) {
  const sw = strokeWidth ?? 2;
  const inner = raw
    .replace(/<!--[\s\S]*?-->/g, "")
    .replace(/<svg\b[\s\S]*?>/, "")
    .replace(/<\/svg>\s*$/, "")
    .trim();
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="none">
  <g fill="none" stroke="#000000" stroke-width="${sw}" stroke-linecap="round" stroke-linejoin="round">
  ${inner}
  </g>
</svg>`;
}

function pixelLum(rgba, i) {
  const o = i * 4;
  if (rgba[o + 3] < 128) {
    return 255;
  }
  return (rgba[o] * 77 + rgba[o + 1] * 150 + rgba[o + 2] * 29) >> 8;
}

function averageDownsample2(rgba, width, height, threshold) {
  const outW = width >> 1;
  const outH = height >> 1;
  const mask = new Uint8Array(outW * outH);
  for (let y = 0; y < outH; y++) {
    for (let x = 0; x < outW; x++) {
      const i00 = (y * 2) * width + x * 2;
      const avg =
        (pixelLum(rgba, i00) +
          pixelLum(rgba, i00 + 1) +
          pixelLum(rgba, i00 + width) +
          pixelLum(rgba, i00 + width + 1)) /
        4;
      if (avg <= threshold) {
        mask[y * outW + x] = 1;
      }
    }
  }
  return { mask, width: outW, height: outH };
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
  const srcSvg = path.join(LUCIDE_ICONS_DIR, `${entry.id}.svg`);
  if (!fs.existsSync(srcSvg)) {
    throw new Error(`Lucide SVG not found: ${srcSvg}`);
  }
  const raw = fs.readFileSync(srcSvg, "utf8");
  fs.writeFileSync(path.join(svgDir, `${entry.id}.svg`), raw);

  const renderSize = entry.size * 2;
  const wrapped = prepareSvg(raw, renderSize, entry.strokeWidth);
  const rendered = new Resvg(wrapped, {
    fitTo: { mode: "width", value: renderSize },
    background: "white",
  }).render();

  if (rendered.width !== renderSize || rendered.height !== renderSize) {
    throw new Error(
      `${entry.id}: unexpected raster size ${rendered.width}x${rendered.height}`
    );
  }

  const mono = averageDownsample2(rendered.pixels, rendered.width, rendered.height, 96);
  const png = maskToPng(mono.mask, mono.width, mono.height);
  const outPath = path.join(outputDir, entry.file);
  fs.writeFileSync(outPath, png);
  console.log(
    `sliced ${entry.id} -> ${path.relative(path.join(__dirname, "../.."), outPath)} ` +
      `(${mono.width}x${mono.height}, ${png.length} bytes)`
  );
}

function main() {
  const manifest = loadManifest();
  const svgDir = path.join(__dirname, manifest.svgDir);
  const outputDir = path.resolve(__dirname, manifest.outputDir);
  fs.mkdirSync(svgDir, { recursive: true });
  fs.mkdirSync(outputDir, { recursive: true });

  console.log(`Lucide source: ${manifest.source}`);
  console.log(`lucide-static icons: ${LUCIDE_ICONS_DIR}`);
  for (const entry of manifest.icons) {
    sliceIcon(entry, svgDir, outputDir);
  }
}

main();
