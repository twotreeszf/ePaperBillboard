#!/usr/bin/env python3
"""Render one e-ink pictorial per line of a UTF-8 text file.

The font is LXGW WenKai (霞鹜文楷). Font size and line breaks are chosen
so the ink fills a rectangle (default 280x280, the pictorial pane).
Line spacing is 1.2 times the font size. Each edge keeps 16px clear.
The first line is indented by one em.

    python tools/render_quotes.py tools/quotes/关于欲望.txt
"""

import argparse
import json
import struct
import subprocess
import urllib.error
import urllib.request
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

USER_AGENT = "ePaperBillboard render_quotes/1.0"
FONT_URL = (
    "https://github.com/lxgw/LxgwWenKai/releases/download/v1.521/"
    "LXGWWenKai-Regular.ttf"
)
FONT_FILE = "LXGWWenKai-Regular.ttf"
BOX = 280
MIN_FONT = 8
PROBE_SIZE = 64
TOP_WRAPS = 8
MARGIN = 16
FIRST_INDENT = 1
INDENT_CHAR = "\u3000"

NO_LINE_START = set("，。、；：！？）》」』】〕〉”’…—～·,.;:!?)]}")
NO_LINE_END = set("（《「『【〔〈“‘([{")


def project_root():
    return Path(__file__).resolve().parents[1]


def load_quotes(path):
    if not path.is_file():
        raise SystemExit(f"Quote file not found: {path}")
    quotes = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line:
            quotes.append(line)
    if not quotes:
        raise SystemExit(f"No quotes in {path}")
    return quotes


def font_candidates():
    root = project_root()
    home = Path.home()
    return [
        root / "tools" / "fonts" / FONT_FILE,
        home / "Library" / "Fonts" / FONT_FILE,
        Path("/Library/Fonts") / FONT_FILE,
        home / ".local" / "share" / "fonts" / FONT_FILE,
        Path("/usr/local/share/fonts") / FONT_FILE,
        Path("/usr/share/fonts") / FONT_FILE,
    ]


def font_from_fc():
    try:
        output = subprocess.check_output(
            ["fc-list", "LXGW WenKai:style=Regular", "file"],
            text=True,
            stderr=subprocess.DEVNULL,
        )
    except (OSError, subprocess.CalledProcessError):
        return None
    for line in output.splitlines():
        path = Path(line.split(":", 1)[0].strip())
        if path.name == FONT_FILE and path.is_file():
            return path
    return None


def download_font(destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(".ttf.part")
    print(f"download {FONT_URL}")
    request = urllib.request.Request(FONT_URL, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(request, timeout=120) as response:
            temporary.write_bytes(response.read())
    except urllib.error.URLError as error:
        raise SystemExit(f"Font download failed: {error}") from error
    temporary.replace(destination)
    return destination


def find_font(explicit):
    if explicit is not None:
        path = explicit.expanduser()
        if not path.is_file():
            raise SystemExit(f"Font not found: {path}")
        return path
    for path in font_candidates():
        if path.is_file():
            return path
    listed = font_from_fc()
    if listed is not None:
        return listed
    return download_font(project_root() / "tools" / "fonts" / FONT_FILE)


def wrap_cols(text, cols):
    lines = []
    index = 0
    count = len(text)
    first = True
    while index < count:
        budget = max(1, cols - FIRST_INDENT) if first else cols
        end = min(index + budget, count)
        while end < count and text[end] in NO_LINE_START and end - index < budget:
            end += 1
        if end < count and text[end] in NO_LINE_START:
            back = end - 1
            while back > index and text[back] in NO_LINE_START:
                back -= 1
            if back > index:
                end = back
        if end < count and end - index > 1 and text[end - 1] in NO_LINE_END:
            end -= 1
        if end == index:
            end = index + 1
        piece = text[index:end]
        if first:
            piece = INDENT_CHAR * FIRST_INDENT + piece
        lines.append(piece)
        index = end
        first = False
    return lines


def measure(font, lines, size, leading, boxes):
    step = size * leading
    y_offsets = [index * step for index in range(len(lines))]
    left = min(box[0] for box in boxes)
    right = max(box[2] for box in boxes)
    ink_top = min(y_offsets[index] + boxes[index][1] for index in range(len(lines)))
    ink_bottom = max(y_offsets[index] + boxes[index][3] for index in range(len(lines)))
    return right - left, ink_bottom - ink_top, left, ink_top, y_offsets


def fit_quote(text, font_path, box_w, box_h, leading, margin, threshold):
    fonts = {}
    box_cache = {}

    def font_at(size):
        font = fonts.get(size)
        if font is None:
            font = ImageFont.truetype(str(font_path), size)
            fonts[size] = font
        return font

    def line_box(size, line):
        key = (size, line)
        box = box_cache.get(key)
        if box is None:
            box = font_at(size).getbbox(line)
            box_cache[key] = box
        return box

    def layout_at(lines, size):
        boxes = [line_box(size, line) for line in lines]
        width, height, left, ink_top, y_offsets = measure(
            font_at(size), lines, size, leading, boxes
        )
        return {
            "size": size,
            "lines": lines,
            "left": left,
            "ink_top": ink_top,
            "y_offsets": y_offsets,
            "width": width,
            "height": height,
        }

    def fits(layout):
        return (
            0 < layout["width"] <= box_w - 2 * margin
            and 0 < layout["height"] <= box_h - 2 * margin
        )

    def largest(lines):
        lo = MIN_FONT * 2
        hi = max(box_w, box_h) * 2
        found = None
        while lo <= hi:
            mid = (lo + hi) // 2
            layout = layout_at(lines, mid / 2)
            if fits(layout):
                found = layout
                lo = mid + 1
            else:
                hi = mid - 1
        return found

    ranked = []
    seen = set()
    for cols in range(1, len(text) + 1):
        lines = tuple(wrap_cols(text, cols))
        if lines in seen:
            continue
        seen.add(lines)
        sample = layout_at(lines, PROBE_SIZE)
        if sample["width"] <= 0 or sample["height"] <= 0:
            continue
        scale = min(
            (box_w - 2 * margin) / sample["width"],
            (box_h - 2 * margin) / sample["height"],
        )
        ranked.append((min(sample["width"], sample["height"]) * scale, lines))
    if not ranked:
        raise SystemExit(f"Cannot fit quote into {box_w}x{box_h}: {text}")
    ranked.sort(key=lambda item: item[0], reverse=True)

    chosen = None
    for _score, lines in ranked[:TOP_WRAPS]:
        layout = largest(lines)
        if layout is None:
            continue
        score = (min(layout["width"], layout["height"]), layout["width"] * layout["height"], layout["size"])
        if chosen is None or score > chosen[0]:
            chosen = (score, layout)
    if chosen is None:
        raise SystemExit(f"Cannot fit quote into {box_w}x{box_h}: {text}")

    layout = chosen[1]
    while layout["size"] >= MIN_FONT:
        rendered = render_layout(layout, font_at(layout["size"]), box_w, box_h, margin, threshold)
        if rendered is not None:
            return rendered
        layout = layout_at(layout["lines"], layout["size"] - 0.5)
    raise SystemExit(f"Ink does not fit {box_w}x{box_h}: {text}")


def render_layout(layout, font, box_w, box_h, margin, threshold):
    pad = 12
    canvas_w = int(layout["width"]) + pad * 2 + 8
    canvas_h = int(layout["height"]) + pad * 2 + 8
    canvas = Image.new("L", (canvas_w, canvas_h), 255)
    draw = ImageDraw.Draw(canvas)
    origin_x = pad - layout["left"]
    origin_y = pad - layout["ink_top"]
    for line, y_offset in zip(layout["lines"], layout["y_offsets"]):
        draw.text((origin_x, origin_y + y_offset), line, font=font, fill=0, anchor="la")
    inked = canvas.point(lambda pixel: 0 if pixel < threshold else 255)
    bbox = Image.eval(inked, lambda pixel: 255 - pixel).getbbox()
    if bbox is None:
        return None
    ink_w = bbox[2] - bbox[0]
    ink_h = bbox[3] - bbox[1]
    left = (box_w - ink_w) // 2
    top = (box_h - ink_h) // 2
    if ink_w > box_w - 2 * margin or ink_h > box_h - 2 * margin or left < margin or top < margin:
        return None
    page = Image.new("L", (box_w, box_h), 255)
    page.paste(inked.crop(bbox), (left, top))
    return page, ink_w, ink_h, layout


def pack_i1(image):
    width, height = image.size
    stride = (width + 7) // 8
    data = bytearray(8 + stride * height)
    data[0:4] = b"TTI1"
    data[4:6] = struct.pack("<H", width)
    data[6:8] = struct.pack("<H", height)
    for index in range(8, len(data)):
        data[index] = 0xFF
    pixels = image.tobytes()
    for y in range(height):
        row = y * width
        for x in range(width):
            if pixels[row + x] >= 128:
                continue
            offset = 8 + y * stride + (x >> 3)
            data[offset] &= ~(1 << (7 - (x & 7)))
    return bytes(data)


def safe_name(name):
    cleaned = name.strip().replace("/", " ").replace("\\", " ")
    if not cleaned or cleaned in (".", ".."):
        raise SystemExit(f"Invalid series name: {name!r}")
    return cleaned


def clear_pages(folder):
    for pattern in ("*.png", "*.i1"):
        for path in folder.glob(pattern):
            path.unlink()


def render_series(quotes, folder, font_path, box_w, box_h, leading, margin, threshold, source):
    folder.mkdir(parents=True, exist_ok=True)
    clear_pages(folder)
    pages = []
    for index, text in enumerate(quotes, start=1):
        image, ink_w, ink_h, layout = fit_quote(
            text, font_path, box_w, box_h, leading, margin, threshold
        )
        stem = f"{index:04d}"
        png_path = folder / f"{stem}.png"
        i1_path = folder / f"{stem}.i1"
        image.save(png_path, optimize=True)
        i1_path.write_bytes(pack_i1(image))
        record = {
            "file": png_path.name,
            "text": text,
            "font_size": layout["size"],
            "lines": list(layout["lines"]),
            "ink": [ink_w, ink_h],
        }
        pages.append(record)
        print(
            f"{stem} size={layout['size']:g} ink={ink_w}x{ink_h} "
            f"lines={len(layout['lines'])} {text}"
        )
    manifest = {
        "source": source,
        "font": str(font_path),
        "box": [box_w, box_h],
        "margin": margin,
        "quotes": pages,
    }
    (folder / "index.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"wrote {len(pages)} pages to {folder}")


def main():
    root = project_root()
    parser = argparse.ArgumentParser(description="Render quote pictorials in LXGW WenKai.")
    parser.add_argument("source", type=Path, help="UTF-8 text file, one quote per line")
    parser.add_argument("--name", help="Output folder name; defaults to the file stem")
    parser.add_argument(
        "--output",
        type=Path,
        default=root / "tools" / "quotes" / "output",
        help="Parent directory for the series folder",
    )
    parser.add_argument("--width", type=int, default=BOX)
    parser.add_argument("--height", type=int, default=BOX)
    parser.add_argument("--font", type=Path, help="TTF path; default is LXGW WenKai Regular")
    parser.add_argument(
        "--leading",
        type=float,
        default=1.2,
        help="Line spacing as a multiple of the font size",
    )
    parser.add_argument(
        "--margin",
        type=int,
        default=MARGIN,
        help="Padding on each edge, in pixels",
    )
    parser.add_argument(
        "--threshold",
        type=int,
        default=160,
        help="Pixels darker than this become black ink",
    )
    args = parser.parse_args()
    if args.width < 8 or args.height < 8:
        raise SystemExit("--width and --height must be at least 8")
    if args.leading <= 0:
        raise SystemExit("--leading must be greater than 0")
    if args.margin < 0 or args.margin * 2 >= min(args.width, args.height):
        raise SystemExit("--margin must leave room inside the box")
    if not 1 <= args.threshold <= 254:
        raise SystemExit("--threshold must be between 1 and 254")
    quotes = load_quotes(args.source)
    name = safe_name(args.name or args.source.stem)
    font_path = find_font(args.font)
    print(f"font {font_path}")
    print(f"{args.source} quotes={len(quotes)}")
    render_series(
        quotes,
        args.output / name,
        font_path,
        args.width,
        args.height,
        args.leading,
        args.margin,
        args.threshold,
        str(args.source),
    )


if __name__ == "__main__":
    main()
