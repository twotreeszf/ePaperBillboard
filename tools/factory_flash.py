#!/usr/bin/env python3

import argparse
import subprocess
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import publish_firmware

PIO_ENV = "esp32"


def run(cmd):
    print("+ " + " ".join(cmd))
    subprocess.run(cmd, cwd=ROOT, check=True)


def pio_upload(target, port):
    cmd = ["pio", "run", "-e", PIO_ENV, "-t", target]
    if port:
        cmd.extend(["--upload-port", port])
    run(cmd)


def main():
    parser = argparse.ArgumentParser(
        description="Flash the current local firmware and LittleFS image."
    )
    parser.add_argument("--port", help="Serial port for esptool, if auto-detect is wrong.")
    args = parser.parse_args()
    pio_upload("upload", args.port)
    notes = "量产版本-" + datetime.now().strftime("%Y%m%d%H%M")
    publish_firmware.write_local_manifest(
        publish_firmware.read_version(),
        publish_firmware.content_fingerprint(),
        notes,
    )
    pio_upload("uploadfs", args.port)
    print("factory flash done")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as exc:
        sys.exit(exc.returncode)
