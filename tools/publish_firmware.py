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


def iter_res_files():
    for path in iter_files(RES_ROOT):
        if path.name == MANIFEST_NAME and path.parent == RES_ROOT:
            continue
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
    for path in iter_res_files():
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


def json_line_string(line, key):
    mark = f'"{key}"'
    idx = line.find(mark)
    if idx < 0:
        return None
    rest = line[idx + len(mark):]
    colon = rest.find(":")
    if colon < 0:
        return None
    raw = rest[colon + 1:].strip()
    if raw.endswith(","):
        raw = raw[:-1].strip()
    if not raw.startswith('"'):
        return None
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        return None


def read_manifest_fields(path):
    found = {}
    with path.open(encoding="utf-8") as handle:
        for line in handle:
            for key in ("version", "notes", "content"):
                if key in found or f'"{key}"' not in line:
                    continue
                value = json_line_string(line, key)
                if isinstance(value, str):
                    found[key] = value
            if len(found) == 3:
                break
    return found


def download_manifest(client, env, dest):
    from tos.exceptions import TosServerError
    key = object_key(env, MANIFEST_NAME)
    try:
        result = client.get_object(env["TOS_BUCKET"], key)
    except TosServerError as exc:
        if exc.status_code == 404 or exc.code in ("NoSuchKey", "NotFound"):
            print(f"manifest missing: {key}")
            return False
        raise
    with dest.open("wb") as out:
        while True:
            chunk = result.read(8192)
            if not chunk:
                break
            out.write(chunk)
    return True


def build_firmware():
    print("building firmware")
    subprocess.run(["pio", "run", "-e", PIO_ENV], cwd=ROOT, check=True)
    if not FIRMWARE_BIN.is_file():
        raise SystemExit(f"missing {FIRMWARE_BIN}")


def load_notes():
    if not NOTES_PATH.is_file():
        return ""
    return NOTES_PATH.read_text().strip()


def package_files():
    if not FIRMWARE_BIN.is_file():
        raise SystemExit(f"missing {FIRMWARE_BIN}")
    files = [("firmware.bin", FIRMWARE_BIN)]
    for path in iter_res_files():
        rel = Path("res") / path.relative_to(RES_ROOT)
        files.append((rel.as_posix(), path))
    return files


def write_local_manifest(version, fingerprint, notes=None):
    note = load_notes() if notes is None else notes
    path = RES_ROOT / MANIFEST_NAME
    with path.open("w", encoding="utf-8") as out:
        out.write("{\n")
        out.write(f'  "version": {json.dumps(version)},\n')
        out.write(f'  "notes": {json.dumps(note, ensure_ascii=False)},\n')
        out.write(f'  "content": {json.dumps(fingerprint)},\n')
        out.write('  "files": [\n')
        first = True
        for rel, file_path in package_files():
            if not first:
                out.write(",\n")
            first = False
            remote = f"{version}/{rel}"
            size = file_path.stat().st_size
            digest = sha256_file(file_path)
            out.write("    {\n")
            out.write(f'      "path": {json.dumps(remote, ensure_ascii=False)},\n')
            out.write(f'      "size": {size},\n')
            out.write(f'      "sha256": {json.dumps(digest)}\n')
            out.write("    }")
        out.write("\n  ]\n}\n")
    print(f"local manifest {path} version={version}")
    return path


def rewrite_notes(src, dst, notes):
    replaced = False
    with src.open(encoding="utf-8") as inp, dst.open("w", encoding="utf-8") as out:
        for line in inp:
            if not replaced and '"notes"' in line:
                out.write('  "notes": ' + json.dumps(notes, ensure_ascii=False) + ",\n")
                replaced = True
            else:
                out.write(line)
    if not replaced:
        raise SystemExit("manifest notes field missing")


def store_manifest(client, env, version, path):
    upload_file(client, env, object_key(env, MANIFEST_NAME), path)
    upload_file(client, env, object_key(env, f"{version}/res/{MANIFEST_NAME}"), path)
    print(f"manifest stored version={version} res={version}/res/{MANIFEST_NAME}")


def upload_file(client, env, key, path):
    print(f"upload {key}")
    client.put_object_from_file(env["TOS_BUCKET"], key, str(path))


def publish(env, client, version, fingerprint):
    for rel, path in package_files():
        upload_file(client, env, object_key(env, f"{version}/{rel}"), path)
    body = write_local_manifest(version, fingerprint)
    store_manifest(client, env, version, body)
    print(f"published {version}")


def main():
    env = load_env(ENV_PATH)
    client = tos_client(env)
    fingerprint = content_fingerprint()
    notes = load_notes()
    with tempfile.TemporaryDirectory() as tmp:
        remote_path = Path(tmp) / MANIFEST_NAME
        remote = read_manifest_fields(remote_path) if download_manifest(client, env, remote_path) else None
        remote_content = remote.get("content") if remote else None
        remote_version = remote.get("version") if remote else None
        if remote_content == fingerprint:
            if remote and remote.get("notes") == notes:
                print(f"no update version={remote_version}")
                return
            local_path = RES_ROOT / MANIFEST_NAME
            rewrite_notes(remote_path, local_path, notes)
            store_manifest(client, env, remote_version, local_path)
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
