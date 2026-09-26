#!/usr/bin/env python3
"""Rename prepared comics to 4-digit names and pack TTI1 files for TOS.

Source layout is mirrored under Publish. A 1024 image is fit into a
280x280 TTI1 so the device can store and draw it.
"""

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import eink_comic

IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".webp", ".gif"}
PANE_W = 280
PANE_H = 280
INK_THRESHOLD = 160
INK_COVERAGE = 20


def project_root():
    return Path(__file__).resolve().parents[1]


def list_images(folder):
    return sorted(
        path
        for path in folder.iterdir()
        if path.is_file() and path.suffix.lower() in IMAGE_SUFFIXES
    )


def page_number(path):
    digits = "".join(ch for ch in path.stem if ch.isdigit())
    if not digits:
        raise SystemExit(f"No page number in {path.name}")
    number = int(digits)
    if number < 1 or number > 9999:
        raise SystemExit(f"Page number out of range: {path.name}")
    return number


def organize_folder(folder):
    images = list_images(folder)
    planned = []
    seen = {}
    for src in images:
        number = page_number(src)
        dest = src.with_name(f"{number:04d}{src.suffix.lower()}")
        if number in seen:
            raise SystemExit(f"Duplicate page {number:04d} in {folder}")
        seen[number] = dest
        planned.append((src, dest))

    pending = []
    for src, dest in planned:
        if src == dest:
            continue
        temporary = src.with_name(f".renaming-{dest.name}")
        if temporary.exists():
            raise SystemExit(f"Rename leftover exists: {temporary}")
        src.rename(temporary)
        pending.append((src.name, temporary, dest))
    for original, temporary, dest in pending:
        if dest.exists():
            raise SystemExit(f"Rename target exists: {dest}")
        temporary.rename(dest)
        print(f"renamed {original} -> {dest.name}")
    return list_images(folder)


def fit_pane(width, height, gray):
    out_w = max(1, min(PANE_W, int(round(width * min(PANE_W / width, PANE_H / height)))))
    out_h = max(1, min(PANE_H, int(round(height * min(PANE_W / width, PANE_H / height)))))
    origin_x = (PANE_W - out_w) // 2
    origin_y = (PANE_H - out_h) // 2
    ink = bytearray(PANE_W * PANE_H)
    for y in range(out_h):
        src_y0 = y * height // out_h
        src_y1 = max(src_y0 + 1, (y + 1) * height // out_h)
        for x in range(out_w):
            src_x0 = x * width // out_w
            src_x1 = max(src_x0 + 1, (x + 1) * width // out_w)
            total = 0
            dark = 0
            for src_y in range(src_y0, min(src_y1, height)):
                row = src_y * width
                for src_x in range(src_x0, min(src_x1, width)):
                    total += 1
                    if gray[row + src_x] < INK_THRESHOLD:
                        dark += 1
            if total and dark * 100 >= total * INK_COVERAGE:
                ink[(origin_y + y) * PANE_W + origin_x + x] = 1
    return ink


def pack_i1(ink):
    stride = (PANE_W + 7) // 8
    data = bytearray(8 + stride * PANE_H)
    data[0:4] = b"TTI1"
    data[4:6] = struct.pack("<H", PANE_W)
    data[6:8] = struct.pack("<H", PANE_H)
    for index in range(8, len(data)):
        data[index] = 0xFF
    for y in range(PANE_H):
        for x in range(PANE_W):
            if not ink[y * PANE_W + x]:
                continue
            offset = 8 + y * stride + (x >> 3)
            data[offset] &= ~(1 << (7 - (x & 7)))
    return bytes(data)


def convert_image(source, destination):
    width, height, gray = eink_comic.decode_png_gray(eink_comic.to_png_bytes(source))
    payload = pack_i1(fit_pane(width, height, gray))
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(payload)
    print(f"packed {source.name} {width}x{height} -> {destination} ({len(payload)} bytes)")


def publish_tree(prepare, publish):
    if not prepare.is_dir():
        raise SystemExit(f"Prepare directory not found: {prepare}")
    folders = [prepare] if list_images(prepare) else []
    folders.extend(path for path in sorted(prepare.rglob("*")) if path.is_dir())
    written = 0
    for folder in folders:
        images = organize_folder(folder)
        if not images:
            continue
        relative = folder.relative_to(prepare)
        for source in images:
            destination = (publish / relative / f"{page_number(source):04d}").with_suffix(".i1")
            convert_image(source, destination)
            written += 1
    if written == 0:
        raise SystemExit(f"No comic pages in {prepare}")
    print(f"published {written} pages to {publish}")


def main():
    root = project_root()
    parser = argparse.ArgumentParser(description="Pack prepared comics as 4-digit TTI1 pages.")
    parser.add_argument("--prepare", type=Path, default=root / "tools" / "comics" / "prepare")
    parser.add_argument("--publish", type=Path, default=root / "tools" / "comics" / "Publish")
    args = parser.parse_args()
    publish_tree(args.prepare, args.publish)


if __name__ == "__main__":
    main()
