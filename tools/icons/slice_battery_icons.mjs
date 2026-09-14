#!/usr/bin/env node
/**
 * Pixel-exact 18x12 battery icons with 4 fill cells.
 *
 * Usage:
 *   cd tools/icons && node slice_battery_icons.mjs
 */

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { maskToI1 } from "./write_i1.mjs";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const WIDTH = 18;
const HEIGHT = 12;

const ICONS = [
  { file: "battery_sm.i1", svg: "battery.svg", bars: 0 },
  { file: "battery_low_sm.i1", svg: "battery-low.svg", bars: 1 },
  { file: "battery_medium_sm.i1", svg: "battery-medium.svg", bars: 2 },
  { file: "battery_high_sm.i1", svg: "battery-high.svg", bars: 3 },
  { file: "battery_full_sm.i1", svg: "battery-full.svg", bars: 4 },
  { file: "battery_charging_sm.i1", svg: "battery-plus.svg", bars: -1 },
];

function setPixel(mask, x, y) {
  if (x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT) {
    return;
  }
  mask[y * WIDTH + x] = 1;
}

function fillRect(mask, x, y, w, h) {
  for (let yy = 0; yy < h; yy++) {
    for (let xx = 0; xx < w; xx++) {
      setPixel(mask, x + xx, y + yy);
    }
  }
}

function paintFrame(mask) {
  fillRect(mask, 1, 2, 15, 1);
  fillRect(mask, 1, 3, 1, 9);
  fillRect(mask, 15, 3, 1, 9);
  fillRect(mask, 16, 5, 1, 4);
  fillRect(mask, 2, 11, 13, 1);
}

function paintBars(mask, count) {
  const xs = [3, 6, 9, 12];
  for (let i = 0; i < count; i++) {
    fillRect(mask, xs[i], 4, 2, 6);
  }
}

function paintBolt(mask) {
  fillRect(mask, 8, 4, 2, 1);
  fillRect(mask, 7, 5, 2, 1);
  fillRect(mask, 6, 6, 5, 1);
  fillRect(mask, 8, 7, 2, 1);
  fillRect(mask, 7, 8, 2, 1);
  fillRect(mask, 6, 9, 2, 1);
}

function buildMask(bars) {
  const mask = new Uint8Array(WIDTH * HEIGHT);
  paintFrame(mask);
  if (bars < 0) {
    paintBolt(mask);
  } else {
    paintBars(mask, bars);
  }
  return mask;
}

function maskToSvg(mask) {
  const rects = [];
  for (let y = 0; y < HEIGHT; y++) {
    let x = 0;
    while (x < WIDTH) {
      if (!mask[y * WIDTH + x]) {
        x += 1;
        continue;
      }
      let w = 1;
      while (x + w < WIDTH && mask[y * WIDTH + x + w]) {
        w += 1;
      }
      rects.push(`  <rect x="${x}" y="${y}" width="${w}" height="1"/>`);
      x += w;
    }
  }
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${WIDTH}" height="${HEIGHT}" viewBox="0 0 ${WIDTH} ${HEIGHT}" fill="#000000">\n${rects.join("\n")}\n</svg>\n`;
}

function maskToAscii(mask) {
  let out = "";
  for (let y = 0; y < HEIGHT; y++) {
    for (let x = 0; x < WIDTH; x++) {
      out += mask[y * WIDTH + x] ? "#" : ".";
    }
    out += "\n";
  }
  return out;
}

function main() {
  const svgDir = path.join(__dirname, "svg");
  const outputDir = path.resolve(__dirname, "../../data/icons");
  fs.mkdirSync(svgDir, { recursive: true });
  fs.mkdirSync(outputDir, { recursive: true });

  for (const icon of ICONS) {
    const mask = buildMask(icon.bars);
    const i1 = maskToI1(mask, WIDTH, HEIGHT);
    fs.writeFileSync(path.join(outputDir, icon.file), i1);
    fs.writeFileSync(path.join(svgDir, icon.svg), maskToSvg(mask));
    console.log(`${icon.file} ${WIDTH}x${HEIGHT}\n${maskToAscii(mask)}`);
  }
}

main();
