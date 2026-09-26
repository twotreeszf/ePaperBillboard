#!/usr/bin/env python3
"""Redraw the most interesting panel of a comic as a square e-ink image.

Reads UUROUTE_API_KEY from the environment or the project .env file.
Photos are converted to PNG with macOS sips before upload.
"""

import argparse
import base64
import json
import os
import struct
import subprocess
import tempfile
import urllib.error
import urllib.request
import zlib
from pathlib import Path

DEFAULT_BASE_URL = "https://api.uuroute.ai"
DEFAULT_MODEL = "gemini-3.1-flash-image"
DEFAULT_THRESHOLD = 160
PNG_SIG = b"\x89PNG\r\n\x1a\n"

PROMPT = """The reference is a comic page with several panels. Read the page, then choose the single most interesting panel: the one with the strongest action, expression, or punchline.

Redraw only that panel as a new square black-and-white comic image for a monochrome e-ink screen. Keep the same characters, pose, and gag. Do not include the other panels.

Requirements:
- One panel only, with one clear subject and an obvious silhouette.
- Pure white background. No extra scenery, props, speech balloons, text, watermark, or gray.
- Do not draw an outer border, panel box, or rectangular frame. The drawing sits directly on the white background.
- Draw only with thick black outlines and solid black fills.
- No gradients, halftone, shading, or color.
- Keep the composition simple and the identifying features easy to read at a small size.
"""


class ImageBlocked(Exception):
    pass


def load_dotenv(path):
    if not path.is_file():
        return
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        os.environ.setdefault(key.strip(), value.strip())


def project_root():
    return Path(__file__).resolve().parents[1]


def png_chunk(tag, data):
    crc = zlib.crc32(tag + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)


def paeth(left, up, up_left):
    estimate = left + up - up_left
    distances = (
        abs(estimate - left),
        abs(estimate - up),
        abs(estimate - up_left),
    )
    return (left, up, up_left)[distances.index(min(distances))]


def unfilter_rows(raw, height, stride, channels):
    rows = []
    offset = 0
    for y in range(height):
        filter_type = raw[offset]
        row = bytearray(raw[offset + 1 : offset + 1 + stride])
        offset += 1 + stride
        previous = rows[-1] if rows else bytearray(stride)
        if filter_type == 1:
            for i in range(stride):
                left = row[i - channels] if i >= channels else 0
                row[i] = (row[i] + left) & 0xFF
        elif filter_type == 2:
            for i in range(stride):
                row[i] = (row[i] + previous[i]) & 0xFF
        elif filter_type == 3:
            for i in range(stride):
                left = row[i - channels] if i >= channels else 0
                row[i] = (row[i] + ((left + previous[i]) // 2)) & 0xFF
        elif filter_type == 4:
            for i in range(stride):
                left = row[i - channels] if i >= channels else 0
                up_left = previous[i - channels] if i >= channels else 0
                row[i] = (row[i] + paeth(left, previous[i], up_left)) & 0xFF
        elif filter_type != 0:
            raise SystemExit(f"Unsupported PNG filter {filter_type}")
        rows.append(row)
    return rows


def decode_png_gray(data):
    if not data.startswith(PNG_SIG):
        raise SystemExit("Expected a PNG image")
    width = height = bit_depth = color_type = None
    idat = bytearray()
    offset = len(PNG_SIG)
    while offset + 8 <= len(data):
        length = struct.unpack(">I", data[offset : offset + 4])[0]
        tag = data[offset + 4 : offset + 8]
        chunk = data[offset + 8 : offset + 8 + length]
        offset += 12 + length
        if tag == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(">IIBB", chunk[:10])
        elif tag == b"IDAT":
            idat.extend(chunk)
        elif tag == b"IEND":
            break
    if color_type not in (0, 2, 6) or bit_depth not in (1, 8):
        raise SystemExit(f"Unsupported PNG: depth={bit_depth} color={color_type}")
    if bit_depth == 1 and color_type != 0:
        raise SystemExit(f"Unsupported PNG: depth={bit_depth} color={color_type}")
    channels = {0: 1, 2: 3, 6: 4}[color_type]
    stride = (width + 7) // 8 if bit_depth == 1 else width * channels
    rows = unfilter_rows(zlib.decompress(idat), height, stride, 1 if bit_depth == 1 else channels)
    gray = bytearray(width * height)
    for y, row in enumerate(rows):
        for x in range(width):
            if bit_depth == 1:
                bit = (row[x // 8] >> (7 - (x % 8))) & 1
                gray[y * width + x] = 255 if bit else 0
                continue
            pixel = row[x * channels : (x + 1) * channels]
            if channels == 1:
                value = pixel[0]
            else:
                value = (pixel[0] * 30 + pixel[1] * 59 + pixel[2] * 11) // 100
            gray[y * width + x] = value
    return width, height, gray


def model_image_to_png(image_bytes):
    if image_bytes.startswith(PNG_SIG):
        return image_bytes
    suffix = ".jpg" if image_bytes.startswith(b"\xff\xd8") else ".img"
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / f"model{suffix}"
        converted = Path(directory) / "model.png"
        source.write_bytes(image_bytes)
        subprocess.run(
            ["sips", "-s", "format", "png", str(source), "--out", str(converted)],
            check=True,
            capture_output=True,
        )
        return converted.read_bytes()


def encode_png_1bit(width, height, black):
    stride = (width + 7) // 8
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x_byte in range(stride):
            bits = 0
            for bit in range(8):
                x = x_byte * 8 + bit
                white = x >= width or not black[y * width + x]
                if white:
                    bits |= 1 << (7 - bit)
            raw.append(bits)
    ihdr = struct.pack(">IIBBBBB", width, height, 1, 0, 0, 0, 0)
    return b"".join(
        [
            PNG_SIG,
            png_chunk(b"IHDR", ihdr),
            png_chunk(b"IDAT", zlib.compress(bytes(raw), 9)),
            png_chunk(b"IEND", b""),
        ]
    )


def to_png_bytes(image_path):
    if image_path.suffix.lower() == ".png":
        return image_path.read_bytes()
    with tempfile.TemporaryDirectory() as directory:
        converted = Path(directory) / "source.png"
        subprocess.run(
            ["sips", "-s", "format", "png", str(image_path), "--out", str(converted)],
            check=True,
            capture_output=True,
        )
        return converted.read_bytes()


def generate_comic(png_bytes, api_key, base_url, model, prompt=PROMPT):
    payload = {
        "contents": [
            {
                "parts": [
                    {
                        "inlineData": {
                            "mimeType": "image/png",
                            "data": base64.b64encode(png_bytes).decode("ascii"),
                        }
                    },
                    {"text": prompt},
                ]
            }
        ],
        "generationConfig": {
            "responseModalities": ["TEXT", "IMAGE"],
            "imageConfig": {"aspectRatio": "1:1", "imageSize": "1K"},
        },
    }
    url = f"{base_url.rstrip('/')}/v1beta/models/{model}:generateContent"
    request = urllib.request.Request(
        url,
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
        method="POST",
    )
    print(f"POST {url}")
    try:
        with urllib.request.urlopen(request, timeout=180) as response:
            raw = response.read()
            print(f"HTTP {response.status}")
    except urllib.error.HTTPError as error:
        detail = error.read().decode("utf-8", "replace")[:800]
        raise SystemExit(f"HTTP {error.code}: {detail}") from error
    return json.loads(raw)


def extract_result(result):
    notes = []
    image_bytes = None
    for candidate in result.get("candidates") or []:
        content = candidate.get("content") or {}
        for part in content.get("parts") or []:
            text = part.get("text")
            if text:
                notes.append(text.strip())
            inline = part.get("inlineData") or part.get("inline_data") or {}
            data = inline.get("data")
            if data and image_bytes is None:
                image_bytes = base64.b64decode(data)
    if image_bytes is None:
        candidate = (result.get("candidates") or [{}])[0]
        reason = candidate.get("finishReason") or "NO_IMAGE"
        message = candidate.get("finishMessage") or "response did not include an image"
        if reason == "IMAGE_SAFETY":
            raise ImageBlocked(message.split(".")[0])
        raise SystemExit(f"{reason}: {message}")
    return image_bytes, "\n".join(notes)


def render_page(source, destination, api_key, base_url, model, threshold):
    result = generate_comic(to_png_bytes(source), api_key, base_url, model, PROMPT)
    image_bytes, notes = extract_result(result)
    if notes:
        print(notes)
    to_eink(image_bytes, destination, threshold)


def to_eink(image_bytes, output_path, threshold):
    width, height, gray = decode_png_gray(model_image_to_png(image_bytes))
    side = min(width, height)
    left = (width - side) // 2
    top = (height - side) // 2
    black = bytearray(side * side)
    for y in range(side):
        src = (top + y) * width + left
        for x in range(side):
            black[y * side + x] = gray[src + x] < threshold
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(encode_png_1bit(side, side, black))
    print(f"Saved {output_path} ({side}x{side}, 1-bit)")


def main():
    load_dotenv(project_root() / ".env")
    parser = argparse.ArgumentParser(description="Make a square e-ink comic from a reference image.")
    parser.add_argument("image", type=Path, help="Reference image")
    parser.add_argument("-o", "--output", type=Path, help="Output PNG (default: <name>.eink.png)")
    parser.add_argument("--threshold", type=int, default=DEFAULT_THRESHOLD, help="Gray values below this become black")
    parser.add_argument("--model", default=os.environ.get("UUROUTE_MODEL", DEFAULT_MODEL))
    parser.add_argument("--base-url", default=os.environ.get("UUROUTE_BASE_URL", DEFAULT_BASE_URL))
    args = parser.parse_args()

    api_key = os.environ.get("UUROUTE_API_KEY", "")
    if not api_key:
        raise SystemExit("Set UUROUTE_API_KEY in the environment or .env")
    if not args.image.is_file():
        raise SystemExit(f"Image not found: {args.image}")

    output = args.output or args.image.with_name(args.image.stem + ".eink.png")
    render_page(args.image, output, api_key, args.base_url, args.model, args.threshold)


if __name__ == "__main__":
    main()
