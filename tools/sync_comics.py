#!/usr/bin/env python3
"""Build Comics/Manifest.json from Publish and sync that tree to TOS.

Name each series folder in Chinese. The script turns that name into the TOS
pinyin directory. Pages are 0001.i1, 0002.i1, ... with no gaps. Manifest order
follows the Chinese names, and the first item is the device default.
"""

import hashlib
import json
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import publish_firmware
from tos.enum import ACLType

COMICS_PREFIX = "Comics"
MANIFEST_NAME = "Manifest.json"
PINYIN_RE = re.compile(r"^[a-z0-9_-]{1,31}$")
PINYIN_PART_RE = re.compile(r"[a-z0-9]+")
PAGE_RE = re.compile(r"^(\d{4})\.i1$")
MAX_SERIES = 24
MAX_NAME_BYTES = 47
MAX_IMAGE_BYTES = 32 * 1024
MAX_MANIFEST_BYTES = 8 * 1024
MAX_IMAGE_W = 400
MAX_IMAGE_H = 300


def project_root():
    return Path(__file__).resolve().parents[1]


def md5_file(path):
    digest = hashlib.md5()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def check_i1(path):
    data = path.read_bytes()
    if len(data) < 8 or data[:4] != b"TTI1":
        raise SystemExit(f"not a TTI1 file: {path}")
    width, height = struct.unpack_from("<HH", data, 4)
    stride = (width + 7) // 8
    expect = 8 + stride * height
    if width <= 0 or height <= 0 or width > MAX_IMAGE_W or height > MAX_IMAGE_H:
        raise SystemExit(f"image size {width}x{height} out of range: {path}")
    if len(data) != expect or len(data) > MAX_IMAGE_BYTES:
        raise SystemExit(f"image length {len(data)} != {expect}: {path}")


def load_pinyin():
    try:
        from pypinyin import Style, lazy_pinyin
    except ImportError:
        raise SystemExit("missing pypinyin package: python3 -m pip install pypinyin")
    return Style, lazy_pinyin


def to_pinyin(name, lazy_pinyin, style):
    parts = []
    for part in lazy_pinyin(name.strip(), style=style, errors="default"):
        token = part.lower().replace("ü", "v")
        if token.strip() == "":
            continue
        if PINYIN_PART_RE.fullmatch(token) is None:
            raise SystemExit(f"cannot make a pinyin path for {name}: {part}")
        parts.append(token)
    pinyin = "".join(parts)
    if PINYIN_RE.fullmatch(pinyin) is None:
        raise SystemExit(f"pinyin path out of range for {name}: {pinyin}")
    return pinyin


def scan_series(publish):
    if not publish.is_dir():
        raise SystemExit(f"Publish directory not found: {publish}")
    style, lazy_pinyin = load_pinyin()
    series = []
    seen = {}
    for folder in sorted(path for path in publish.iterdir() if path.is_dir()):
        name = folder.name.strip()
        if len(name.encode("utf-8")) > MAX_NAME_BYTES:
            raise SystemExit(f"name longer than {MAX_NAME_BYTES} bytes: {folder.name}")
        pinyin = to_pinyin(name, lazy_pinyin, style.NORMAL)
        if pinyin in seen:
            raise SystemExit(f"pinyin {pinyin} is used by both {seen[pinyin]} and {name}")
        seen[pinyin] = name
        pages = []
        for path in folder.iterdir():
            if not path.is_file() or path.name == ".DS_Store":
                continue
            match = PAGE_RE.match(path.name)
            if match is None:
                raise SystemExit(f"unexpected file in {name}: {path.name}")
            pages.append((int(match.group(1)), path))
        if not pages:
            raise SystemExit(f"no pages in {folder}")
        pages.sort()
        numbers = [number for number, _ in pages]
        expect = list(range(1, numbers[-1] + 1))
        if numbers != expect:
            raise SystemExit(f"{name} must be 0001.i1 through {numbers[-1]:04d}.i1 with no gaps")
        for _, path in pages:
            check_i1(path)
        series.append({
            "name": name,
            "pinyin": pinyin,
            "count": numbers[-1],
            "pages": [path for _, path in pages],
        })
    if not series:
        raise SystemExit(f"no comic series in {publish}")
    if len(series) > MAX_SERIES:
        raise SystemExit(f"too many series: {len(series)} > {MAX_SERIES}")
    return series


def manifest_bytes(series):
    items = [
        {"name": item["name"], "pinyin": item["pinyin"], "count": item["count"]}
        for item in series
    ]
    body = (json.dumps({"items": items}, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    if len(body) > MAX_MANIFEST_BYTES:
        raise SystemExit(f"manifest is {len(body)} bytes, limit is {MAX_MANIFEST_BYTES}")
    return body


def local_objects(publish, series, manifest_path):
    objects = {manifest_path: f"{COMICS_PREFIX}/{MANIFEST_NAME}"}
    for item in series:
        for path in item["pages"]:
            objects[path] = f"{COMICS_PREFIX}/{item['pinyin']}/{path.name}"
    return objects


def list_remote(client, bucket):
    prefix = COMICS_PREFIX + "/"
    token = ""
    found = {}
    while True:
        result = client.list_objects_type2(
            bucket, prefix=prefix, continuation_token=token or None, max_keys=1000)
        for item in result.contents or []:
            found[item.key] = item
        if not getattr(result, "is_truncated", False):
            break
        token = result.next_continuation_token
    return found


def remote_same(item, path):
    etag = str(getattr(item, "etag", "")).strip('"')
    return getattr(item, "size", None) == path.stat().st_size and etag == md5_file(path)


def content_type(key):
    if key.endswith(".json"):
        return "application/json; charset=utf-8"
    return "application/octet-stream"


def sync_objects(client, bucket, objects, remote):
    uploaded = 0
    kept = 0
    manifest_key = f"{COMICS_PREFIX}/{MANIFEST_NAME}"
    ordered = sorted(objects.items(), key=lambda item: (item[1] == manifest_key, item[1]))
    for path, key in ordered:
        current = remote.get(key)
        if current is not None and remote_same(current, path):
            print(f"keep {key}")
            kept += 1
            continue
        print(f"upload {key}")
        client.put_object_from_file(
            bucket, key, str(path), content_type=content_type(key), acl=ACLType.ACL_Public_Read)
        uploaded += 1
    for key in objects.values():
        client.put_object_acl(bucket, key, acl=ACLType.ACL_Public_Read)
    deleted = 0
    for key in sorted(remote):
        if key in objects.values():
            continue
        if not key.startswith(COMICS_PREFIX + "/"):
            continue
        print(f"delete {key}")
        client.delete_object(bucket, key)
        deleted += 1
    print(f"synced upload={uploaded} keep={kept} delete={deleted}")


def publish(publish_dir):
    series = scan_series(publish_dir)
    body = manifest_bytes(series)
    manifest_path = publish_dir / MANIFEST_NAME
    manifest_path.write_bytes(body)
    print(f"manifest {manifest_path} series={len(series)}")
    for item in series:
        print(f"series {item['pinyin']} name={item['name']} count={item['count']}")

    env = publish_firmware.load_env(publish_firmware.ENV_PATH)
    client = publish_firmware.tos_client(env)
    bucket = env["TOS_BUCKET"]
    objects = local_objects(publish_dir, series, manifest_path)
    remote = list_remote(client, bucket)
    print(f"remote {COMICS_PREFIX}/ objects={len(remote)}")
    sync_objects(client, bucket, objects, remote)


def main():
    publish(project_root() / "tools" / "comics" / "Publish")


if __name__ == "__main__":
    main()
