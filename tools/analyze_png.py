#!/usr/bin/env python3
"""
Parse a PNG file and report structure + scanline filter bytes.
Use: python analyze_png.py [path/to/image.png]
      python analyze_png.py --checksum path/to/image.png  # print byte sum to compare with device log
Default: data/icons/square.png relative to project root.
"""
import struct
import sys
import zlib
from pathlib import Path


def byte_sum(path):
    """Sum of all file bytes (uint32 truncation), matches device log."""
    with open(path, "rb") as f:
        data = f.read()
    return sum(data) & 0xFFFFFFFF

PNG_SIG = b"\x89PNG\r\n\x1a\n"


def read_chunks(f):
    """Yield (type, data) for each chunk. Caller must skip signature before first call."""
    while True:
        raw = f.read(8)
        if len(raw) < 8:
            break
        length, ctype = struct.unpack(">I4s", raw)
        payload = f.read(length + 4)  # data + crc
        if len(payload) < length + 4:
            break
        yield (ctype.decode("latin1"), payload[:length])


def parse_ihdr(data):
    w, h, depth, color_type, comp, filter_method, interlace = struct.unpack(
        ">IIBBBBB", data[:13]
    )
    return {
        "width": w,
        "height": h,
        "bit_depth": depth,
        "color_type": color_type,
        "compression": comp,
        "filter_method": filter_method,
        "interlace": interlace,
    }


def bytes_per_row(ihdr):
    """Raw bytes per row (excluding filter byte) for deflate stream."""
    w, depth, color_type = ihdr["width"], ihdr["bit_depth"], ihdr["color_type"]
    if color_type == 0:  # grayscale
        samples = 1
    elif color_type == 2:  # rgb
        samples = 3
    elif color_type == 3:  # palette
        samples = 1
    elif color_type == 4:  # gray+alpha
        samples = 2
    else:  # 6 = rgba
        samples = 4
    bits_per_row = w * samples * depth
    return (bits_per_row + 7) // 8


def main():
    repo = Path(__file__).resolve().parent.parent
    argv = sys.argv[1:]
    if argv and argv[0] == "--checksum":
        argv = argv[1:]
        path = Path(argv[0]) if argv else repo / "data" / "icons" / "square.png"
        path = path if path.is_absolute() else (repo / path)
        if not path.exists():
            print(f"File not found: {path}")
            sys.exit(1)
        print("size=%u sum=%lu" % (path.stat().st_size, byte_sum(path)))
        return
    path = Path(argv[0]) if argv else repo / "data" / "icons" / "square.png"
    path = path if path.is_absolute() else (repo / path)
    if not path.exists():
        print(f"File not found: {path}")
        sys.exit(1)

    with open(path, "rb") as f:
        head = f.read(8)
        if head != PNG_SIG:
            print(f"Invalid PNG signature: {head!r}")
            sys.exit(1)
        idat_parts = []
        ihdr = None
        for ctype, data in read_chunks(f):
            if ctype == "IHDR":
                ihdr = parse_ihdr(data)
            elif ctype == "IDAT":
                idat_parts.append(data)
            elif ctype == "IEND":
                break
        print("Chunks seen: IHDR=%s IDAT=%d IEND=%s" % (
            ihdr is not None, len(idat_parts), ctype == "IEND" if idat_parts else "n/a"))

    if not ihdr:
        print("No IHDR chunk")
        sys.exit(1)
    print("IHDR:", ihdr)
    if ihdr["filter_method"] != 0:
        print("WARNING: filter_method is not 0 (standard)")

    if not idat_parts:
        print("No IDAT chunk")
        sys.exit(1)
    raw_idat = b"".join(idat_parts)
    try:
        decompressed = zlib.decompress(raw_idat)
    except zlib.error as e:
        print(f"zlib decompress error: {e}")
        sys.exit(1)

    bpr = bytes_per_row(ihdr)
    row_bytes_with_filter = 1 + bpr
    h = ihdr["height"]
    interlace = ihdr["interlace"]

    if interlace == 0:
        expected_len = h * row_bytes_with_filter
        if len(decompressed) != expected_len:
            print(
                f"Length mismatch: decompressed={len(decompressed)} "
                f"expected={expected_len} (h={h} row_bytes_with_filter={row_bytes_with_filter})"
            )
        print(f"Decompressed length: {len(decompressed)} (expected {expected_len})")
        print("Scanline filter bytes (first byte of each row, must be 0-4):")
        invalid = []
        for row in range(h):
            off = row * row_bytes_with_filter
            if off + 1 > len(decompressed):
                print(f"  row {row}: (truncated)")
                break
            fb = decompressed[off]
            if fb > 4:
                invalid.append((row, fb))
            print(f"  row {row}: filter={fb}")
        if invalid:
            print(f"INVALID filter bytes (must be 0-4): {invalid}")
    else:
        print("Interlaced (Adam7): showing first few bytes of decompressed stream:")
        print("  First 32 bytes (hex):", decompressed[:32].hex())
        print("  First byte (filter for first subimage row):", decompressed[0])


if __name__ == "__main__":
    main()
