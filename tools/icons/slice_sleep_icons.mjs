#!/usr/bin/env node
/**
 * Pixel-exact 12x12 sleep / standby icons.
 * Sleep is a 1px outline of Material Symbols bedtime (crescent).
 *
 * Usage:
 *   cd tools/icons && node slice_sleep_icons.mjs
 */

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { maskToI1 } from "./write_i1.mjs";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const WIDTH = 12;
const HEIGHT = 12;
function paintSleepBedtime() {
  return rowsToMask([
    "............",
    "...##.......",
    "..###.......",
    "..#..#......",
    ".#....#.....",
    ".#.....#....",
    ".#......#...",
    ".#.......##.",
    "..#.......#.",
    "...#.....#..",
    "....#####...",
    "............",
  ]);
}

function rowsToMask(rows) {
  if (rows.length !== HEIGHT) {
    throw new Error(`expected ${HEIGHT} rows, got ${rows.length}`);
  }
  const mask = new Uint8Array(WIDTH * HEIGHT);
  for (let y = 0; y < HEIGHT; y++) {
    if (rows[y].length !== WIDTH) {
      throw new Error(`row ${y} width ${rows[y].length}, expected ${WIDTH}`);
    }
    for (let x = 0; x < WIDTH; x++) {
      if (rows[y][x] === "#") {
        mask[y * WIDTH + x] = 1;
      }
    }
  }
  return mask;
}

const ICONS = [
  {
    file: "sleep_sm.i1",
    svg: "bedtime.svg",
    mask: paintSleepBedtime(),
  },
  {
    file: "standby_sm.i1",
    svg: "mode_standby.svg",
    rows: [
      "............",
      "....####....",
      "..##....##..",
      "..#......#..",
      ".#........#.",
      ".#...##...#.",
      ".#...##...#.",
      ".#........#.",
      "..#......#..",
      "..##....##..",
      "....####....",
      "............",
    ],
  },
];

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
    const mask = icon.mask != null ? icon.mask : rowsToMask(icon.rows);
    const i1 = maskToI1(mask, WIDTH, HEIGHT);
    fs.writeFileSync(path.join(outputDir, icon.file), i1);
    fs.writeFileSync(path.join(svgDir, icon.svg), maskToSvg(mask));
    console.log(`${icon.file} ${WIDTH}x${HEIGHT}\n${maskToAscii(mask)}`);
  }
}

main();
