#!/usr/bin/env python3
"""Rename prepared comics to 4-digit names and pack TTI1 files for TOS.

Optionally flatten batch output (any nested folders) into prepare with fresh
0001… page numbers, then pack. A 1024 image is fit into a 280x280 TTI1.
"""

import argparse
import shutil
import struct
import sys
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import eink_comic

IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".webp", ".gif"}
PANE_W = 280
PANE_H = 280
INK_THRESHOLD = 160
INK_COVERAGE = 20
MAX_WORKERS = 10


def project_root():
    return Path(__file__).resolve().parents[1]


def list_images(folder):
    return sorted(
        path
        for path in folder.iterdir()
        if path.is_file() and path.suffix.lower() in IMAGE_SUFFIXES
    )


def list_series_images(series_dir):
    return sorted(
        (
            path
            for path in series_dir.rglob("*")
            if path.is_file() and path.suffix.lower() in IMAGE_SUFFIXES
        ),
        key=lambda path: path.relative_to(series_dir).as_posix(),
    )


def normalized_suffix(path):
    suffix = path.suffix.lower()
    return ".jpg" if suffix == ".jpeg" else suffix


def clear_prepare_images(folder):
    for path in folder.iterdir():
        if path.is_file() and path.suffix.lower() in IMAGE_SUFFIXES:
            path.unlink()


def stage_output_to_prepare(output_dir, prepare_dir, series_filter=None):
    if not output_dir.is_dir():
        raise SystemExit(f"Output directory not found: {output_dir}")
    prepare_dir.mkdir(parents=True, exist_ok=True)
    staged_pages = 0
    staged_series = 0
    series_dirs = sorted(path for path in output_dir.iterdir() if path.is_dir())
    for series_dir in series_dirs:
        name = series_dir.name
        if series_filter is not None and name != series_filter:
            continue
        images = list_series_images(series_dir)
        if not images:
            print(f"skip {name}: no images under {series_dir}")
            continue
        dest_dir = prepare_dir / name
        dest_dir.mkdir(parents=True, exist_ok=True)
        clear_prepare_images(dest_dir)
        for index, source in enumerate(images, start=1):
            dest = dest_dir / f"{index:04d}{normalized_suffix(source)}"
            shutil.copy2(source, dest)
        staged_series += 1
        staged_pages += len(images)
        print(f"staged {name}: {len(images)} pages -> {dest_dir} (0001..{len(images):04d})")
    if staged_series == 0:
        raise SystemExit(f"No comic images under {output_dir}")
    print(f"staged {staged_pages} pages in {staged_series} series to {prepare_dir}")
    return staged_pages


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


def convert_image_worker(source, destination):
    convert_image(Path(source), Path(destination))


def publish_is_current(source, destination):
    if not destination.is_file():
        return False
    try:
        header = destination.read_bytes()[:4]
    except OSError:
        return False
    if header != b"TTI1":
        return False
    return source.stat().st_mtime_ns <= destination.stat().st_mtime_ns


def publish_tree(prepare, publish, workers):
    if not prepare.is_dir():
        raise SystemExit(f"Prepare directory not found: {prepare}")
    folders = [prepare] if list_images(prepare) else []
    folders.extend(path for path in sorted(prepare.rglob("*")) if path.is_dir())
    jobs = []
    total = 0
    kept = 0
    for folder in folders:
        images = organize_folder(folder)
        if not images:
            continue
        relative = folder.relative_to(prepare)
        for source in images:
            total += 1
            destination = (publish / relative / f"{page_number(source):04d}").with_suffix(".i1")
            if publish_is_current(source, destination):
                kept += 1
                print(f"keep {destination.relative_to(publish)}")
                continue
            jobs.append((str(source), str(destination)))
    if total == 0:
        raise SystemExit(f"No comic pages in {prepare}")
    if not jobs:
        print(f"published 0 pages to {publish} (keep {kept}/{total})")
        return
    print(f"pack {len(jobs)} pages (keep {kept}) with {workers} workers")
    written = 0
    with ProcessPoolExecutor(max_workers=workers) as pool:
        futures = {
            pool.submit(convert_image_worker, source, destination): (source, destination)
            for source, destination in jobs
        }
        for future in as_completed(futures):
            source, destination = futures[future]
            try:
                future.result()
            except Exception as error:
                raise SystemExit(f"pack failed {source} -> {destination}: {error}") from error
            written += 1
    print(f"published {written} pages to {publish} (keep {kept}, total {total})")


def main():
    root = project_root()
    parser = argparse.ArgumentParser(description="Pack prepared comics as 4-digit TTI1 pages.")
    parser.add_argument("--output", type=Path, default=root / "tools" / "comics" / "output")
    parser.add_argument("--prepare", type=Path, default=root / "tools" / "comics" / "prepare")
    parser.add_argument("--publish", type=Path, default=root / "tools" / "comics" / "Publish")
    parser.add_argument(
        "--series",
        help="Only restage this top-level folder name under output (default: all series)",
    )
    parser.add_argument(
        "--skip-stage",
        action="store_true",
        help="Pack prepare as-is; do not refresh prepare from output",
    )
    parser.add_argument(
        "--stage-only",
        action="store_true",
        help="Only restage output into prepare; do not pack to Publish",
    )
    parser.add_argument(
        "--workers",
        type=int,
        default=MAX_WORKERS,
        help=f"Parallel worker processes (1–{MAX_WORKERS}, default {MAX_WORKERS})",
    )
    args = parser.parse_args()
    workers = max(1, min(MAX_WORKERS, args.workers))
    if not args.skip_stage:
        stage_output_to_prepare(args.output, args.prepare, args.series)
    if args.stage_only:
        return
    publish_tree(args.prepare, args.publish, workers)


if __name__ == "__main__":
    main()
