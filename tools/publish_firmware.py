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
NOTES_PATH = ROOT / "release-notes.md"
NOTES_MAX_CHARS = 100
HISTORY_MAX = 24
CONTENT_ROOTS = ("src", "include", "lib")
CONTENT_FILES = ("platformio.ini", "partitions_8MB.csv")
CHANGELOG_PATHS = ("src", "include", "lib", "data/res", "platformio.ini", "partitions_8MB.csv")


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


def ensure_notes_length(notes):
    if not notes:
        raise SystemExit(f"{NOTES_PATH.name} is empty")
    count = len(notes)
    if count > NOTES_MAX_CHARS:
        raise SystemExit(
            f"{NOTES_PATH.name} has {count} characters; the limit is {NOTES_MAX_CHARS}"
        )


def git_output(*args):
    try:
        return subprocess.check_output(["git", *args], cwd=ROOT, text=True, stderr=subprocess.DEVNULL)
    except (subprocess.CalledProcessError, FileNotFoundError):
        return ""


def firmware_version_commits(limit=10):
    text = git_output("log", f"-{limit}", "--format=%H", "--", str(VERSION_HEADER.relative_to(ROOT)))
    return [line.strip() for line in text.splitlines() if line.strip()]


def version_at_commit(sha):
    text = git_output("show", f"{sha}:{VERSION_HEADER.relative_to(ROOT)}")
    match = VERSION_RE.search(text)
    return match.group(1) if match else None


def notes_at_commit(sha):
    for name in ("release-notes.md", "release-notes.txt"):
        text = git_output("show", f"{sha}:{name}")
        if text.strip():
            return text.strip()
    return ""


def previous_published_commit():
    commits = firmware_version_commits()
    if len(commits) < 2:
        return None
    return commits[1]


def print_release_summary():
    prev = previous_published_commit()
    if prev is None:
        print("no previous firmware version commit in git history")
        return
    prev_version = version_at_commit(prev) or prev[:8]
    current = read_version()
    print(f"changes since published {prev_version} (commit {prev[:8]})")
    print(f"current header version {current}")
    print("")
    log = git_output(
        "log",
        f"{prev}..HEAD",
        "--oneline",
        "--no-merges",
        "--",
        *CHANGELOG_PATHS,
    )
    if log.strip():
        print(log.rstrip())
    else:
        print("(no commits touching firmware paths)")
    print("")
    prev_notes = notes_at_commit(prev)
    if prev_notes:
        print(f"{NOTES_PATH.name} at last publish:")
        print(prev_notes)
        print("")
    print(f"Edit {NOTES_PATH.name} with a user-facing summary of the above, then run publish_firmware.py.")


def ensure_release_notes_updated():
    prev = previous_published_commit()
    if prev is None:
        return
    prev_notes = notes_at_commit(prev)
    notes = load_notes()
    if not notes:
        raise SystemExit(
            f"{NOTES_PATH.name} is empty; summarize changes since {version_at_commit(prev) or prev[:8]}"
        )
    ensure_notes_length(notes)
    if notes == prev_notes:
        raise SystemExit(
            f"{NOTES_PATH.name} unchanged since last publish; "
            "run publish_firmware.py --summary and update it before releasing"
        )


def package_files():
    if not FIRMWARE_BIN.is_file():
        raise SystemExit(f"missing {FIRMWARE_BIN}")
    files = [("firmware.bin", FIRMWARE_BIN)]
    for path in iter_res_files():
        rel = Path("res") / path.relative_to(RES_ROOT)
        files.append((rel.as_posix(), path))
    return files


def read_history(path):
    if path is None or not Path(path).is_file():
        return []
    text = Path(path).read_text(encoding="utf-8")
    if '"history"' not in text:
        fields = read_manifest_fields(Path(path))
        version = fields.get("version")
        if version:
            return [{"version": version, "notes": fields.get("notes", "")}]
        return []
    history = []
    in_history = False
    for line in text.splitlines():
        if not in_history:
            if '"history"' in line:
                in_history = not line.lstrip().startswith("]") and "]" not in line.split("[", 1)[-1]
            continue
        if line.lstrip().startswith("]"):
            break
        payload = line.strip().rstrip(",")
        if not payload.startswith("{"):
            continue
        try:
            item = json.loads(payload)
        except json.JSONDecodeError as exc:
            raise SystemExit(f"manifest history is not valid JSON: {exc}")
        if not isinstance(item, dict):
            continue
        version = item.get("version")
        notes = item.get("notes", "")
        if isinstance(version, str) and version:
            history.append({
                "version": version,
                "notes": notes if isinstance(notes, str) else "",
            })
    return history


def history_text(history):
    lines = ['  "history": [\n']
    last = len(history) - 1
    for index, item in enumerate(history):
        payload = json.dumps(
            {"version": item["version"], "notes": item["notes"]},
            ensure_ascii=False,
            separators=(", ", ": "),
        )
        comma = "," if index != last else ""
        lines.append(f"    {payload}{comma}\n")
    lines.append("  ],\n")
    return "".join(lines)


def build_history(remote_path, version, notes):
    previous = [item for item in read_history(remote_path) if item["version"] != version]
    history = [{"version": version, "notes": notes}] + previous
    return history[:HISTORY_MAX]


def write_local_manifest(version, fingerprint, notes=None, history=None):
    note = load_notes() if notes is None else notes
    ensure_notes_length(note)
    if history is None:
        history = [{"version": version, "notes": note}]
    path = RES_ROOT / MANIFEST_NAME
    with path.open("w", encoding="utf-8") as out:
        out.write("{\n")
        out.write(f'  "version": {json.dumps(version)},\n')
        out.write(f'  "notes": {json.dumps(note, ensure_ascii=False)},\n')
        out.write(history_text(history))
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


def rewrite_published_notes(src, dst, version, notes):
    ensure_notes_length(notes)
    history = build_history(src, version, notes)
    lines = src.read_text(encoding="utf-8").splitlines(keepends=True)
    has_history = any('"history"' in line for line in lines)
    replaced = False
    skipping = False
    written = []
    for line in lines:
        if skipping:
            if line.lstrip().startswith("]"):
                skipping = False
            continue
        if '"history"' in line:
            written.append(history_text(history))
            skipping = "]" not in line
            continue
        if not replaced and '"notes"' in line:
            written.append("  \"notes\": " + json.dumps(notes, ensure_ascii=False) + ",\n")
            replaced = True
            if not has_history:
                written.append(history_text(history))
            continue
        written.append(line)
    if not replaced:
        raise SystemExit("manifest notes field missing")
    dst.write_text("".join(written), encoding="utf-8")


def store_manifest(client, env, version, path):
    upload_file(client, env, object_key(env, MANIFEST_NAME), path)
    upload_file(client, env, object_key(env, f"{version}/res/{MANIFEST_NAME}"), path)
    print(f"manifest stored version={version} res={version}/res/{MANIFEST_NAME}")


def upload_file(client, env, key, path):
    print(f"upload {key}")
    client.put_object_from_file(env["TOS_BUCKET"], key, str(path))


def publish(env, client, version, fingerprint, history):
    for rel, path in package_files():
        upload_file(client, env, object_key(env, f"{version}/{rel}"), path)
    body = write_local_manifest(version, fingerprint, history=history)
    store_manifest(client, env, version, body)
    print(f"published {version}")


def main():
    if len(sys.argv) > 1 and sys.argv[1] in ("--summary", "-s"):
        print_release_summary()
        return
    env = load_env(ENV_PATH)
    client = tos_client(env)
    fingerprint = content_fingerprint()
    notes = load_notes()
    ensure_notes_length(notes)
    remote_version = None
    prior_history = []
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
            rewrite_published_notes(remote_path, local_path, remote_version, notes)
            store_manifest(client, env, remote_version, local_path)
            print(f"updated notes version={remote_version}")
            return
        prior_history = read_history(remote_path if remote else None)
    ensure_release_notes_updated()
    version = datetime.now().strftime("%Y%m%d%H%M")
    history = [{"version": version, "notes": notes}]
    history.extend(item for item in prior_history if item["version"] != version)
    history = history[:HISTORY_MAX]
    print(f"update {remote_version or '-'} -> {version} history={len(history)}")
    previous = read_version()
    write_version(version)
    try:
        build_firmware()
        publish(env, client, version, fingerprint, history)
    except Exception:
        write_version(previous)
        raise


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as exc:
        sys.exit(exc.returncode)
