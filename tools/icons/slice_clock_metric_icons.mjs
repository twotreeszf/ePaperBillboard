#!/usr/bin/env node
/**
 * Pixel-exact 32x32 clock-mode T/H icons.
 *
 * Usage:
 *   cd tools/icons && node slice_clock_metric_icons.mjs
 */

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { maskToI1 } from "./write_i1.mjs";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const WIDTH = 32;
const HEIGHT = 32;

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

function paintRows(mask, rows, x0 = 0, y0 = 0) {
  for (let y = 0; y < rows.length; y++) {
    const row = rows[y];
    for (let x = 0; x < row.length; x++) {
      if (row[x] === "#") {
        setPixel(mask, x0 + x, y0 + y);
      }
    }
  }
}

function paintTemp(mask) {
  paintRows(mask, [
    "..............####..............",
    ".............##..##.............",
    "............##....##............",
    "............##....##............",
    "............##....##..######....",
    "............##....##..######....",
    "............##....##............",
    "............##....##............",
    "............###..###............",
    "............##....##..######....",
    "............##....##..######....",
    "............##....##............",
    "............###..###............",
    "............##....##............",
    "............##....##............",
    "............##....##..######....",
    "............##....##..######....",
    "............##....##............",
    "...........###....###...........",
    "..........##........##..........",
    ".........##..######..##.........",
    "........##...######...##........",
    "........##..########..##........",
    "........##..########..##........",
    "........##...######...##........",
    ".........##..######..##.........",
    "..........##........##..........",
    "...........###....###...........",
    ".............######.............",
  ], 0, 1);
}

function paintHum(mask) {
  const outer = [
    [15, 2, 2],
    [14, 3, 4],
    [13, 4, 6],
    [12, 5, 8],
    [11, 6, 10],
    [10, 7, 12],
    [9, 8, 14],
    [8, 9, 16],
    [7, 10, 18],
    [6, 11, 20],
    [6, 12, 20],
    [5, 13, 22],
    [5, 14, 22],
    [5, 15, 22],
    [5, 16, 22],
    [5, 17, 22],
    [5, 18, 22],
    [5, 19, 22],
    [6, 20, 20],
    [6, 21, 20],
    [7, 22, 18],
    [8, 23, 16],
    [9, 24, 14],
    [11, 25, 10],
    [13, 26, 6],
  ];
  const inner = [
    [15, 4, 2],
    [14, 5, 4],
    [13, 6, 6],
    [12, 7, 8],
    [11, 8, 10],
    [10, 9, 12],
    [9, 10, 14],
    [8, 11, 16],
    [8, 12, 16],
    [7, 13, 18],
    [7, 14, 18],
    [7, 15, 18],
    [7, 16, 18],
    [7, 17, 18],
    [7, 18, 18],
    [8, 19, 16],
    [8, 20, 16],
    [9, 21, 14],
    [10, 22, 12],
    [11, 23, 10],
    [13, 24, 6],
  ];
  for (const [x, y, w] of outer) {
    fillRect(mask, x, y, w, 1);
  }
  for (const [x, y, w] of inner) {
    for (let xx = 0; xx < w; xx++) {
      const px = x + xx;
      if (px >= 0 && px < WIDTH && y >= 0 && y < HEIGHT) {
        mask[y * WIDTH + px] = 0;
      }
    }
  }
  fillRect(mask, 7, 17, 18, 1);
  const fill = [
    [7, 18, 18],
    [8, 19, 16],
    [8, 20, 16],
    [9, 21, 14],
    [10, 22, 12],
    [11, 23, 10],
    [13, 24, 6],
  ];
  for (const [x, y, w] of fill) {
    fillRect(mask, x, y, w, 1);
  }
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
  const outputDir = path.resolve(__dirname, "../../data/icons/weather");
  fs.mkdirSync(svgDir, { recursive: true });
  fs.mkdirSync(outputDir, { recursive: true });

  const icons = [
    { file: "temp_32.i1", svg: "temp_32.svg", paint: paintTemp },
    { file: "humidity_32.i1", svg: "humidity_32.svg", paint: paintHum },
  ];
  for (const icon of icons) {
    const mask = new Uint8Array(WIDTH * HEIGHT);
    icon.paint(mask);
    const i1 = maskToI1(mask, WIDTH, HEIGHT);
    fs.writeFileSync(path.join(outputDir, icon.file), i1);
    fs.writeFileSync(path.join(svgDir, icon.svg), maskToSvg(mask));
    console.log(`${icon.file}\n${maskToAscii(mask)}`);
  }
}

main();
