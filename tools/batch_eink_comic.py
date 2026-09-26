#!/usr/bin/env python3
"""Redraw every comic page under an input directory.

Successful pages are skipped on the next run. Failed pages are recorded and
retried. Run the script again until it exits 0.
"""

import argparse
import json
import os
import sys
import traceback
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import eink_comic

IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".webp", ".gif"}
DEFAULT_INPUT = eink_comic.project_root() / "tools" / "comics" / "input"
DEFAULT_OUTPUT = eink_comic.project_root() / "tools" / "comics" / "output"
PROGRESS_NAME = "progress.json"


def list_pages(input_dir):
    pages = [
        path
        for path in input_dir.rglob("*")
        if path.is_file() and path.suffix.lower() in IMAGE_SUFFIXES
    ]
    return sorted(pages, key=lambda path: path.relative_to(input_dir).as_posix())


def relative_key(input_dir, path):
    return path.relative_to(input_dir).as_posix()


def output_path_for(output_dir, key):
    return (output_dir / key).with_suffix(".png")


def load_progress(path):
    if not path.is_file():
        return {"done": {}, "failed": {}, "skipped": {}}
    data = json.loads(path.read_text())
    data.setdefault("done", {})
    data.setdefault("failed", {})
    data.setdefault("skipped", {})
    for key, message in list(data["failed"].items()):
        if "IMAGE_SAFETY" in message:
            data["skipped"][key] = "IMAGE_SAFETY"
            del data["failed"][key]
    return data


def save_progress(path, progress):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(progress, ensure_ascii=False, indent=2) + "\n")
    temporary.replace(path)


def redraw_page(source, destination, api_key, base_url, model, threshold):
    eink_comic.render_page(source, destination, api_key, base_url, model, threshold)


def run_batch(input_dir, output_dir, api_key, base_url, model, threshold):
    pages = list_pages(input_dir)
    if not pages:
        raise SystemExit(f"No comic pages in {input_dir}")
    progress_path = output_dir / PROGRESS_NAME
    progress = load_progress(progress_path)
    save_progress(progress_path, progress)
    total = len(pages)
    skipped = 0
    blocked = 0
    finished = 0

    for index, source in enumerate(pages, start=1):
        key = relative_key(input_dir, source)
        destination = output_path_for(output_dir, key)
        if key in progress["skipped"]:
            skipped += 1
            print(f"[{index}/{total}] skip {key}")
            continue
        if key in progress["done"] and destination.is_file():
            skipped += 1
            print(f"[{index}/{total}] skip {key}")
            continue
        print(f"[{index}/{total}] draw {key}")
        try:
            redraw_page(source, destination, api_key, base_url, model, threshold)
        except eink_comic.ImageBlocked:
            progress["done"].pop(key, None)
            progress["failed"].pop(key, None)
            progress["skipped"][key] = "IMAGE_SAFETY"
            save_progress(progress_path, progress)
            blocked += 1
            print(f"[{index}/{total}] skip {key}: IMAGE_SAFETY")
            continue
        except (Exception, SystemExit) as error:
            message = str(error).strip() or traceback.format_exc(limit=1).strip()
            progress["done"].pop(key, None)
            progress["failed"][key] = message
            save_progress(progress_path, progress)
            print(f"[{index}/{total}] fail {key}: {message}")
            continue
        progress["failed"].pop(key, None)
        progress["skipped"].pop(key, None)
        progress["done"][key] = destination.relative_to(output_dir).as_posix()
        save_progress(progress_path, progress)
        finished += 1

    remaining = len(progress["failed"])
    print(
        f"done {len(progress['done'])}/{total}, "
        f"this run finished {finished}, skipped {skipped + blocked}, "
        f"blocked {len(progress['skipped'])}, failed {remaining}"
    )
    print(f"progress {progress_path}")
    if remaining:
        print("rerun the same command to retry failed pages")
        return 1
    return 0


def main():
    eink_comic.load_dotenv(eink_comic.project_root() / ".env")
    parser = argparse.ArgumentParser(description="Batch-redraw comic pages for e-ink.")
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--threshold", type=int, default=eink_comic.DEFAULT_THRESHOLD)
    parser.add_argument("--model", default=os.environ.get("UUROUTE_MODEL", eink_comic.DEFAULT_MODEL))
    parser.add_argument("--base-url", default=os.environ.get("UUROUTE_BASE_URL", eink_comic.DEFAULT_BASE_URL))
    args = parser.parse_args()

    api_key = os.environ.get("UUROUTE_API_KEY", "")
    if not api_key:
        raise SystemExit("Set UUROUTE_API_KEY in the environment or .env")
    if not args.input.is_dir():
        raise SystemExit(f"Input directory not found: {args.input}")

    code = run_batch(args.input, args.output, api_key, args.base_url, args.model, args.threshold)
    raise SystemExit(code)


if __name__ == "__main__":
    main()
