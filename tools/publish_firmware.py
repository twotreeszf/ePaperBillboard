#!/usr/bin/env python3

import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENV_PATH = ROOT / ".env"
VERSION_HEADER = ROOT / "src" / "Base" / "TTFirmwareVersion.h"
VERSION_RE = re.compile(r'#define\s+TT_FW_VERSION\s+"(\d{12})"')
VERSION_PLACEHOLDER = "000000000000"
PIO_ENV = "esp32"
FIRMWARE_BIN = ROOT / ".pio" / "build" / PIO_ENV / "firmware.bin"
RES_ROOT = ROOT / "data" / "res"
MANIFEST_NAME = "manifest.json"
NOTES_PATH = ROOT / "release-notes.txt"
CONTENT_ROOTS = ("src", "include", "lib")
CONTENT_FILES = ("platformio.ini", "partitions_8MB.csv")


def load_env(path):
    if not path.is_file():
        raise SystemExit(f"missing {path}")
    values = {}
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip().strip('"').strip("'")
    required = ("TOS_ACCESS_KEY_ID", "TOS_SECRET_ACCESS_KEY", "TOS_BUCKET", "TOS_REGION")
    missing = [key for key in required if not values.get(key)]
    if missing:
        raise SystemExit("missing .env keys: " + ", ".join(missing))
    values.setdefault("TOS_PREFIX", "firmware")
    return values


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def iter_files(root):
    for path in root.rglob("*"):
        if path.is_file() and path.name != ".DS_Store":
            yield path


def normalized_version_text(text):
    updated, count = VERSION_RE.subn(
        f'#define TT_FW_VERSION "{VERSION_PLACEHOLDER}"', text, count=1)
    if count != 1:
        raise SystemExit(f"version macro missing in {VERSION_HEADER}")
    return updated


def content_fingerprint():
    lines = []
    header_rel = VERSION_HEADER.relative_to(ROOT).as_posix()
    for rel_root in CONTENT_ROOTS:
        base = ROOT / rel_root
        if not base.is_dir():
            continue
        for path in iter_files(base):
            rel = path.relative_to(ROOT).as_posix()
            if rel == header_rel:
                digest = sha256_bytes(normalized_version_text(path.read_text()).encode())
            else:
                digest = sha256_file(path)
            lines.append(f"{rel} {digest}")
    for name in CONTENT_FILES:
        path = ROOT / name
        if path.is_file():
            lines.append(f"{name} {sha256_file(path)}")
    if not RES_ROOT.is_dir():
        raise SystemExit(f"missing {RES_ROOT}")
    for path in iter_files(RES_ROOT):
        rel = path.relative_to(ROOT).as_posix()
        lines.append(f"{rel} {sha256_file(path)}")
    lines.sort()
    return sha256_bytes(("\n".join(lines) + "\n").encode())


def read_version():
    match = VERSION_RE.search(VERSION_HEADER.read_text())
    if not match:
        raise SystemExit(f"version macro missing in {VERSION_HEADER}")
    return match.group(1)


def write_version(version):
    text = VERSION_HEADER.read_text()
    updated, count = VERSION_RE.subn(f'#define TT_FW_VERSION "{version}"', text, count=1)
    if count != 1:
        raise SystemExit(f"version macro missing in {VERSION_HEADER}")
    if updated != text:
        VERSION_HEADER.write_text(updated)


def tos_client(env):
    try:
        import tos
    except ImportError:
        raise SystemExit("missing tos package: python3 -m pip install tos")
    endpoint = env.get("TOS_ENDPOINT") or f"tos-{env['TOS_REGION']}.volces.com"
    return tos.TosClientV2(
        env["TOS_ACCESS_KEY_ID"],
        env["TOS_SECRET_ACCESS_KEY"],
        endpoint,
        env["TOS_REGION"],
    )


def object_key(env, path):
    prefix = env["TOS_PREFIX"].strip("/")
    return f"{prefix}/{path}"


def load_manifest(client, env):
    from tos.exceptions import TosServerError
    key = object_key(env, MANIFEST_NAME)
    try:
        result = client.get_object(env["TOS_BUCKET"], key)
    except TosServerError as exc:
        if exc.status_code == 404 or exc.code in ("NoSuchKey", "NotFound"):
            print(f"manifest missing: {key}")
            return None
        raise
    return json.loads(result.read().decode())


def build_firmware():
    print("building firmware")
    subprocess.run(["pio", "run", "-e", PIO_ENV], cwd=ROOT, check=True)
    if not FIRMWARE_BIN.is_file():
        raise SystemExit(f"missing {FIRMWARE_BIN}")


def load_notes():
    if not NOTES_PATH.is_file():
        return ""
    return NOTES_PATH.read_text().strip()


def upload_bytes(client, env, key, body):
    with tempfile.NamedTemporaryFile() as tmp:
        tmp.write(body)
        tmp.flush()
        upload_file(client, env, key, Path(tmp.name))


def package_files():
    files = [("firmware.bin", FIRMWARE_BIN)]
    for path in iter_files(RES_ROOT):
        rel = Path("res") / path.relative_to(RES_ROOT)
        files.append((rel.as_posix(), path))
    return files


def upload_file(client, env, key, path):
    print(f"upload {key}")
    client.put_object_from_file(env["TOS_BUCKET"], key, str(path))


def publish(env, client, version, fingerprint):
    packaged = package_files()
    entries = []
    for rel, path in packaged:
        remote = f"{version}/{rel}"
        upload_file(client, env, object_key(env, remote), path)
        entries.append({
            "path": remote,
            "size": path.stat().st_size,
            "sha256": sha256_file(path),
        })
    manifest = {
        "version": version,
        "notes": load_notes(),
        "content": fingerprint,
        "files": entries,
    }
    body = json.dumps(manifest, indent=2).encode() + b"\n"
    upload_bytes(client, env, object_key(env, MANIFEST_NAME), body)
    print(f"published {version} files={len(entries)}")


def main():
    env = load_env(ENV_PATH)
    client = tos_client(env)
    fingerprint = content_fingerprint()
    manifest = load_manifest(client, env)
    remote_content = manifest.get("content") if isinstance(manifest, dict) else None
    remote_version = manifest.get("version") if isinstance(manifest, dict) else None
    notes = load_notes()
    if remote_content == fingerprint:
        if isinstance(manifest, dict) and manifest.get("notes") == notes:
            print(f"no update version={remote_version}")
            return
        manifest["notes"] = notes
        body = json.dumps(manifest, indent=2).encode() + b"\n"
        upload_bytes(client, env, object_key(env, MANIFEST_NAME), body)
        print(f"updated notes version={remote_version}")
        return
    version = datetime.now().strftime("%Y%m%d%H%M")
    print(f"update {remote_version or '-'} -> {version}")
    previous = read_version()
    write_version(version)
    try:
        build_firmware()
        publish(env, client, version, fingerprint)
    except Exception:
        write_version(previous)
        raise


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as exc:
        sys.exit(exc.returncode)
