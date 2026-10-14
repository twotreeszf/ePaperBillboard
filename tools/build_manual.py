#!/usr/bin/env python3
"""Build the manual for publishing and optionally upload it to TOS.

Edit docs/index.html and docs/shots/. The script inlines every image into one
HTML file, renders that page at phone width into a single long JPEG with
headless Chrome, and with --publish uploads the JPEG to TOS as a public object.
"""

import argparse
import base64
import re
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs"
DEFAULT_SOURCE = DOCS / "index.html"
DEFAULT_HTML = DOCS / "publish" / "index.html"
DEFAULT_IMAGE = DOCS / "publish" / "manual.jpg"
DEFAULT_KEY = "manual/manual.jpg"
DEFAULT_CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
MOBILE_WIDTH = 390
RENDER_SCALE = 2
PROBE_HEIGHT = 32000
JPEG_MAX_SIDE = 65500
JPEG_QUALITY = 85
IMAGE_CONTENT_TYPE = "image/jpeg"
OBJECT_CACHE_CONTROL = "no-cache"
MOBILE_STYLE = f"<style>main {{ max-width: {MOBILE_WIDTH}px; margin: 0; }}</style>"
SRC_RE = re.compile(r'(src=")([^"]+)(")')
MIME_BY_SUFFIX = {
    ".png": "image/png",
    ".jpg": "image/jpeg",
    ".jpeg": "image/jpeg",
    ".svg": "image/svg+xml",
    ".webp": "image/webp",
    ".gif": "image/gif",
}


def data_uri(path):
    mime = MIME_BY_SUFFIX.get(path.suffix.lower(), "application/octet-stream")
    encoded = base64.b64encode(path.read_bytes()).decode("ascii")
    return f"data:{mime};base64,{encoded}"


def inline_images(html, source_dir):
    cache = {}
    missing = []
    used = []

    def replace(match):
        url = match.group(2)
        if url.startswith(("http://", "https://", "data:", "#")):
            return match.group(0)
        path = (source_dir / url).resolve()
        try:
            path.relative_to(source_dir.resolve())
        except ValueError:
            missing.append(url)
            return match.group(0)
        if not path.is_file():
            missing.append(url)
            return match.group(0)
        if path not in cache:
            cache[path] = data_uri(path)
        used.append(path)
        return f"{match.group(1)}{cache[path]}{match.group(3)}"

    built = SRC_RE.sub(replace, html)
    return built, used, missing


def build_html(source, output):
    html = source.read_text(encoding="utf-8")
    built, used, missing = inline_images(html, source.parent)
    if missing:
        for url in missing:
            print(f"missing image: {url}", file=sys.stderr)
        raise SystemExit(1)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(built, encoding="utf-8")
    unique = {path.name for path in used}
    print(f"wrote {output} ({output.stat().st_size} bytes, {len(used)} images, {len(unique)} files)")
    return built


def chrome_screenshot(chrome, page, profile, height, scale, output):
    command = [
        chrome,
        "--headless=new",
        "--disable-gpu",
        "--hide-scrollbars",
        "--no-first-run",
        "--no-default-browser-check",
        f"--user-data-dir={profile}",
        f"--window-size={MOBILE_WIDTH},{height}",
        f"--force-device-scale-factor={scale}",
        f"--screenshot={output}",
        page.as_uri(),
    ]
    print(f"render {MOBILE_WIDTH}x{height} scale={scale}")
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not output.is_file():
        raise SystemExit(f"chrome produced no screenshot: {output}")


def content_height(image):
    from PIL import Image

    with Image.open(image) as raw:
        frame = raw.convert("RGB")
    width, height = frame.size
    background = frame.getpixel((width - 1, height - 1))
    pixels = frame.load()
    for y in range(height - 1, -1, -1):
        if any(pixels[x, y] != background for x in range(0, width, 3)):
            return y + 1
    raise SystemExit("rendered page is blank")


def render_image(html, output, chrome):
    from PIL import Image

    if not Path(chrome).is_file():
        raise SystemExit(f"missing chrome: {chrome}")
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        page = work / "manual.html"
        page.write_text(html.replace("</head>", f"{MOBILE_STYLE}\n</head>", 1), encoding="utf-8")
        profile = work / "profile"
        probe = work / "probe.png"
        chrome_screenshot(chrome, page, profile, PROBE_HEIGHT, 1, probe)
        height = content_height(probe)
        print(f"content height {height}")
        if height >= PROBE_HEIGHT:
            raise SystemExit(f"page taller than {PROBE_HEIGHT}px; raise PROBE_HEIGHT")
        if height * RENDER_SCALE > JPEG_MAX_SIDE:
            raise SystemExit(f"image height {height * RENDER_SCALE}px exceeds JPEG limit {JPEG_MAX_SIDE}px")
        shot = work / "shot.png"
        chrome_screenshot(chrome, page, profile, height, RENDER_SCALE, shot)
        output.parent.mkdir(parents=True, exist_ok=True)
        with Image.open(shot) as raw:
            frame = raw.convert("RGB")
            frame.save(output, "JPEG", quality=JPEG_QUALITY, optimize=True, progressive=True)
            size = frame.size
    print(f"wrote {output} ({output.stat().st_size} bytes, {size[0]}x{size[1]})")


def public_url(env, key):
    endpoint = env.get("TOS_ENDPOINT") or f"tos-{env['TOS_REGION']}.volces.com"
    return f"https://{env['TOS_BUCKET']}.{endpoint}/{key}"


def publish(image, key):
    import publish_firmware
    from tos.enum import ACLType

    env = publish_firmware.load_env(publish_firmware.ENV_PATH)
    client = publish_firmware.tos_client(env)
    print(f"upload {key} bucket={env['TOS_BUCKET']}")
    client.put_object_from_file(
        env["TOS_BUCKET"],
        key,
        str(image),
        content_type=IMAGE_CONTENT_TYPE,
        cache_control=OBJECT_CACHE_CONTROL,
        acl=ACLType.ACL_Public_Read,
    )
    print(f"published {public_url(env, key)}")


def main():
    parser = argparse.ArgumentParser(description="Build the manual as one HTML file and one long mobile JPEG.")
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--html", type=Path, default=DEFAULT_HTML, help="single-file HTML output")
    parser.add_argument("--image", type=Path, default=DEFAULT_IMAGE, help="long JPEG output")
    parser.add_argument("--chrome", default=DEFAULT_CHROME, help="Chrome executable used for rendering")
    parser.add_argument("--publish", action="store_true", help="upload the JPEG to TOS")
    parser.add_argument("--key", default=DEFAULT_KEY, help="TOS object key for the JPEG")
    args = parser.parse_args()
    if not args.source.is_file():
        raise SystemExit(f"missing {args.source}")
    image = args.image.resolve()
    html = build_html(args.source.resolve(), args.html.resolve())
    render_image(html, image, args.chrome)
    if args.publish:
        publish(image, args.key.strip("/"))


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as exc:
        sys.exit(exc.returncode)
