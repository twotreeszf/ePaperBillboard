#!/usr/bin/env python3
"""Pack a 1-bit BDF into an LVGL font bin (bpp 1, uncompressed).

The layout matches lv_font_conv --format bin --bpp 1 --no-compress, which
TTFontLoader reads. Glyph bitmaps are copied as drawn; they are not re-rasterized.
"""

import argparse
import struct
import sys
from pathlib import Path


def align4(size):
    return size if size % 4 == 0 else size + 4 - (size % 4)


def count_bits(val):
    count = 0
    val = int(val)
    while val:
        count += 1
        val >>= 1
    return count


def signed_bits(val):
    if val >= 0:
        return count_bits(val) + 1
    return count_bits(abs(val) - 1) + 1


class BitWriter:
    def __init__(self):
        self.data = bytearray()
        self.acc = 0
        self.n = 0

    def write(self, value, bits):
        if bits <= 0:
            return
        value &= (1 << bits) - 1
        for shift in range(bits - 1, -1, -1):
            self.acc = (self.acc << 1) | ((value >> shift) & 1)
            self.n += 1
            if self.n == 8:
                self.data.append(self.acc)
                self.acc = 0
                self.n = 0

    def finish(self):
        if self.n:
            self.data.append(self.acc << (8 - self.n))
            self.acc = 0
            self.n = 0
        return bytes(self.data)


def parse_bdf(path):
    glyphs = {}
    ascent = 11
    descent = 3
    pixel_size = 14
    encoding = None
    advance = 0
    box = (0, 0, 0, 0)
    rows = []
    in_bitmap = False
    with path.open("r", encoding="ascii", errors="replace") as handle:
        for raw in handle:
            line = raw.strip()
            if line.startswith("FONT_ASCENT "):
                ascent = int(line.split()[1])
            elif line.startswith("FONT_DESCENT "):
                descent = int(line.split()[1])
            elif line.startswith("PIXEL_SIZE "):
                pixel_size = int(line.split()[1])
            elif line == "STARTCHAR" or line.startswith("STARTCHAR "):
                encoding = None
                advance = 0
                box = (0, 0, 0, 0)
                rows = []
                in_bitmap = False
            elif line.startswith("ENCODING "):
                encoding = int(line.split()[1])
            elif line.startswith("DWIDTH "):
                advance = int(line.split()[1])
            elif line.startswith("BBX "):
                parts = line.split()
                box = (int(parts[1]), int(parts[2]), int(parts[3]), int(parts[4]))
            elif line == "BITMAP":
                in_bitmap = True
                rows = []
            elif line == "ENDCHAR":
                in_bitmap = False
                if encoding is None or encoding < 0:
                    continue
                width, height, xoff, yoff = box
                bitmap = []
                stride = max(1, (width + 7) // 8)
                for row in rows[:height]:
                    value = int(row, 16) if row else 0
                    bits = []
                    for col in range(width):
                        byte_index = col // 8
                        mask = 0x80 >> (col % 8)
                        byte = (value >> (8 * (stride - 1 - byte_index))) & 0xFF
                        bits.append(1 if byte & mask else 0)
                    bitmap.append(bits)
                while len(bitmap) < height:
                    bitmap.append([0] * width)
                glyphs[encoding] = {
                    "adv": advance,
                    "x": xoff,
                    "y": yoff,
                    "w": width,
                    "h": height,
                    "bits": bitmap,
                }
            elif in_bitmap and line:
                rows.append(line)
    if not glyphs:
        raise SystemExit(f"no glyphs in {path}")
    return pixel_size, ascent, descent, glyphs


def plan_cmap(codepoints):
    tables = []
    index = 0
    count = len(codepoints)
    while index < count:
        end = index + 1
        while end < count and codepoints[end] == codepoints[end - 1] + 1:
            end += 1
        if end - index >= 8:
            run = codepoints[index:end]
            while run:
                tables.append(("tiny", run[:65535]))
                run = run[65535:]
            index = end
            continue
        start_cp = codepoints[index]
        sparse = []
        while index < count:
            end = index + 1
            while end < count and codepoints[end] == codepoints[end - 1] + 1:
                end += 1
            if end - index >= 8:
                break
            if codepoints[end - 1] - start_cp + 1 > 65535:
                break
            sparse.extend(codepoints[index:end])
            index = end
        tables.append(("sparse", sparse))
    return tables


def compile_glyphs(glyphs, xy_bits, wh_bits, adv_bits):
    blobs = [b""]
    for cp in sorted(glyphs):
        glyph = glyphs[cp]
        writer = BitWriter()
        writer.write(glyph["adv"], adv_bits)
        writer.write(glyph["x"], xy_bits)
        writer.write(glyph["y"], xy_bits)
        writer.write(glyph["w"], wh_bits)
        writer.write(glyph["h"], wh_bits)
        for row in glyph["bits"]:
            for pixel in row:
                writer.write(pixel, 1)
        blobs.append(writer.finish())
    return blobs


def table(tag, body):
    payload = tag.encode("ascii") + body
    raw_len = 4 + len(payload)
    total = align4(raw_len)
    return struct.pack("<I", total) + payload + b"\x00" * (total - raw_len)


def build_bin(pixel_size, ascent, descent, glyphs):
    codepoints = sorted(glyphs)
    xy_bits = max(signed_bits(glyphs[cp]["x"]) for cp in codepoints)
    xy_bits = max(xy_bits, max(signed_bits(glyphs[cp]["y"]) for cp in codepoints))
    wh_bits = max(count_bits(glyphs[cp]["w"]) for cp in codepoints)
    wh_bits = max(wh_bits, max(count_bits(glyphs[cp]["h"]) for cp in codepoints))
    adv_bits = max(signed_bits(glyphs[cp]["adv"]) for cp in codepoints)
    blobs = compile_glyphs(glyphs, xy_bits, wh_bits, adv_bits)
    glyf_body = b"".join(blobs)
    glyf_size = align4(8 + len(glyf_body))
    loc_format = 1 if glyf_size > 65535 else 0

    min_y = min(glyphs[cp]["y"] for cp in codepoints)
    max_y = max(glyphs[cp]["y"] + glyphs[cp]["h"] for cp in codepoints)
    head = bytearray(40)
    struct.pack_into("<I", head, 0, 1)
    struct.pack_into("<H", head, 4, 3)
    struct.pack_into("<H", head, 6, pixel_size)
    struct.pack_into("<H", head, 8, ascent)
    struct.pack_into("<h", head, 10, -descent)
    struct.pack_into("<H", head, 12, ascent)
    struct.pack_into("<h", head, 14, -descent)
    struct.pack_into("<H", head, 16, 0)
    struct.pack_into("<h", head, 18, min_y)
    struct.pack_into("<h", head, 20, max_y)
    struct.pack_into("<H", head, 22, 0)
    struct.pack_into("<H", head, 24, 16)
    head[26] = loc_format
    head[27] = 1 if len(codepoints) > 255 else 0
    head[28] = 0
    head[29] = 1
    head[30] = xy_bits
    head[31] = wh_bits
    head[32] = adv_bits
    head[33] = 0
    head[34] = 0

    gid = {cp: i + 1 for i, cp in enumerate(codepoints)}
    heads = []
    datas = []
    for kind, cps in plan_cmap(codepoints):
        header = bytearray(16)
        span = cps[-1] - cps[0] + 1
        struct.pack_into("<I", header, 4, cps[0])
        struct.pack_into("<H", header, 8, span)
        struct.pack_into("<H", header, 10, gid[cps[0]])
        if kind == "tiny":
            struct.pack_into("<H", header, 12, span)
            header[14] = 2
            datas.append(b"")
        else:
            struct.pack_into("<H", header, 12, len(cps))
            header[14] = 3
            raw = b"".join(struct.pack("<H", cp - cps[0]) for cp in cps)
            datas.append(raw + b"\x00" * (align4(len(raw)) - len(raw)))
        heads.append(header)
    head_bytes = b"".join(heads)
    data_bytes = b"".join(datas)
    base = 12 + len(head_bytes)
    cursor = base
    for header, data in zip(heads, datas):
        struct.pack_into("<I", header, 0, cursor)
        cursor += len(data)
    cmap_body = struct.pack("<I", len(heads)) + b"".join(heads) + data_bytes

    offsets = []
    cursor = 8
    for blob in blobs:
        offsets.append(cursor)
        cursor += len(blob)
    if loc_format == 0:
        loca_body = struct.pack("<I", len(offsets)) + b"".join(struct.pack("<H", off) for off in offsets)
    else:
        loca_body = struct.pack("<I", len(offsets)) + b"".join(struct.pack("<I", off) for off in offsets)

    return b"".join([
        table("head", bytes(head)),
        table("cmap", cmap_body),
        table("loca", loca_body),
        table("glyf", glyf_body),
    ]), {
        "glyphs": len(codepoints),
        "xy_bits": xy_bits,
        "wh_bits": wh_bits,
        "adv_bits": adv_bits,
        "subtables": len(heads),
        "ascent": ascent,
        "descent": descent,
    }


HZK_ZONES = 87
HZK_POSITIONS = 94
HZK_WIDTH = 14
HZK_HEIGHT = 14
HZK_ROW_BYTES = 2
HZK_GLYPH_BYTES = HZK_HEIGHT * HZK_ROW_BYTES
HZK_CJK_ADV = 14
HZK_DIGIT_ADV = 14
HZK_COLON_ADV = 6
HZK_SPACE_ADV = 7


def hzk_rows(data, index):
    raw = data[index * HZK_GLYPH_BYTES:(index + 1) * HZK_GLYPH_BYTES]
    rows = []
    for row in range(HZK_HEIGHT):
        word = (raw[row * 2] << 8) | raw[row * 2 + 1]
        rows.append([(word >> (15 - col)) & 1 for col in range(HZK_WIDTH)])
    return rows


def hzk_ink(rows):
    return any(pixel for row in rows for pixel in row)


def hzk_cell(rows, advance):
    return {
        "adv": advance,
        "x": 0,
        "y": 0,
        "w": HZK_WIDTH,
        "h": HZK_HEIGHT,
        "bits": rows,
    }


def hzk_ascii_rows(data, code):
    byte2 = 0xA1 + (code - 0x21)
    index = (0xA3 - 0xA1) * HZK_POSITIONS + (byte2 - 0xA1)
    return hzk_rows(data, index)


def hzk_crop(rows):
    columns = [col for row in rows for col, pixel in enumerate(row) if pixel]
    if not columns:
        return None
    left = min(columns)
    right = max(columns)
    return [row[left:right + 1] for row in rows]


def load_hzk14(path):
    data = path.read_bytes()
    expected = HZK_ZONES * HZK_POSITIONS * HZK_GLYPH_BYTES
    if len(data) != expected:
        raise SystemExit(f"{path} is {len(data)} bytes, expected {expected}")
    glyphs = {}
    for zone in range(1, HZK_ZONES + 1):
        for position in range(1, HZK_POSITIONS + 1):
            try:
                text = bytes([zone + 0xA0, position + 0xA0]).decode("gb2312")
            except UnicodeDecodeError:
                continue
            if len(text) != 1:
                continue
            rows = hzk_rows(data, (zone - 1) * HZK_POSITIONS + (position - 1))
            if not hzk_ink(rows):
                continue
            glyphs[ord(text)] = hzk_cell(rows, HZK_CJK_ADV)
    glyphs[0x20] = {"adv": HZK_SPACE_ADV, "x": 0, "y": 0, "w": 0, "h": 0, "bits": []}
    for code in range(0x21, 0x7F):
        rows = hzk_ascii_rows(data, code)
        if not hzk_ink(rows):
            continue
        if 0x30 <= code <= 0x39:
            glyphs[code] = hzk_cell(rows, HZK_DIGIT_ADV)
            continue
        if code == 0x3A:
            colon = [[0, row[2], row[3], 0] for row in rows]
            glyphs[code] = {"adv": HZK_COLON_ADV, "x": 0, "y": 0, "w": 4, "h": HZK_HEIGHT, "bits": colon}
            continue
        cropped = hzk_crop(rows)
        glyphs[code] = {
            "adv": len(cropped[0]) + 1,
            "x": 0,
            "y": 0,
            "w": len(cropped[0]),
            "h": HZK_HEIGHT,
            "bits": cropped,
        }
    return HZK_HEIGHT, HZK_HEIGHT, 0, glyphs


PIXEL_UNIT = 100
PIXEL_ASCENT = 12
PIXEL_DESCENT = 2
PIXEL_SPACE_ADV = 4


def _rect_pen(glyph_set):
    from fontTools.pens.basePen import BasePen

    class RectPen(BasePen):
        def __init__(self, glyphs):
            super().__init__(glyphs)
            self.rects = []
            self._cur = []

        def _moveTo(self, p):
            self._cur = [p]

        def _lineTo(self, p):
            self._cur.append(p)

        def _curveToOne(self, p1, p2, p3):
            raise SystemExit("pixel font contour is not a straight rectangle")

        def _qCurveToOne(self, p1, p2):
            raise SystemExit("pixel font contour is not a straight rectangle")

        def _closePath(self):
            self._store()

        def _endPath(self):
            self._store()

        def _store(self):
            if len(self._cur) < 4:
                self._cur = []
                return
            xs = [int(p[0]) for p in self._cur]
            ys = [int(p[1]) for p in self._cur]
            x0, x1 = min(xs), max(xs)
            y0, y1 = min(ys), max(ys)
            for x, y in zip(xs, ys):
                if x not in (x0, x1) or y not in (y0, y1):
                    raise SystemExit("pixel font contour is not a rectangle")
            self.rects.append((x0, y0, x1, y1))
            self._cur = []

    return RectPen(glyph_set)


def _pixels_in_rect(x0, y0, x1, y1):
    px0 = (x0 - 50 + PIXEL_UNIT - 1) // PIXEL_UNIT
    px1 = (x1 - 50 - 1) // PIXEL_UNIT
    py0 = (y0 - 50 + PIXEL_UNIT - 1) // PIXEL_UNIT
    py1 = (y1 - 50 - 1) // PIXEL_UNIT
    return px0, px1, py0, py1


def load_pixel_ttf(path):
    from fontTools.ttLib import TTFont

    font = TTFont(path)
    cmap = font.getBestCmap()
    hmtx = font["hmtx"]
    glyph_set = font.getGlyphSet()
    glyphs = {}
    for code, name in cmap.items():
        advance, _lsb = hmtx[name]
        if advance % PIXEL_UNIT:
            raise SystemExit(f"U+{code:04X} advance {advance} is not a whole pixel")
        pen = _rect_pen(glyph_set)
        glyph_set[name].draw(pen)
        ink = set()
        for rect in pen.rects:
            px0, px1, py0, py1 = _pixels_in_rect(*rect)
            for py in range(py0, py1 + 1):
                for px in range(px0, px1 + 1):
                    ink.add((px, py))
        adv = advance // PIXEL_UNIT
        if not ink:
            glyphs[code] = {"adv": adv, "x": 0, "y": 0, "w": 0, "h": 0, "bits": []}
            continue
        px0 = min(px for px, _py in ink)
        px1 = max(px for px, _py in ink)
        py0 = min(py for _px, py in ink)
        py1 = max(py for _px, py in ink)
        rows = []
        for py in range(py1, py0 - 1, -1):
            rows.append([1 if (px, py) in ink else 0 for px in range(px0, px1 + 1)])
        glyphs[code] = {
            "adv": adv,
            "x": px0,
            "y": py0,
            "w": px1 - px0 + 1,
            "h": py1 - py0 + 1,
            "bits": rows,
        }
    space = glyphs.get(0x20)
    if space is not None and space["w"] == 0 and space["adv"] > PIXEL_SPACE_ADV:
        space["adv"] = PIXEL_SPACE_ADV
    return PIXEL_ASCENT + PIXEL_DESCENT, PIXEL_ASCENT, PIXEL_DESCENT, glyphs


def main():
    parser = argparse.ArgumentParser(description="Pack a BDF, HZK14, or 100-unit pixel TTF into an LVGL 1-bit font bin.")
    parser.add_argument("bdf", nargs="?", type=Path)
    parser.add_argument("--hzk", type=Path, help="UCDOS HZK14 bitmap (87 zones, 28 bytes per glyph)")
    parser.add_argument("--pixel-ttf", type=Path, help="Pixel TTF drawn on a 100-unit grid")
    parser.add_argument("-o", "--output", type=Path, required=True)
    args = parser.parse_args()
    if args.hzk is not None:
        pixel_size, ascent, descent, glyphs = load_hzk14(args.hzk)
    elif args.pixel_ttf is not None:
        pixel_size, ascent, descent, glyphs = load_pixel_ttf(args.pixel_ttf)
    elif args.bdf is not None:
        pixel_size, ascent, descent, glyphs = parse_bdf(args.bdf)
    else:
        raise SystemExit("pass a BDF path, --hzk, or --pixel-ttf")
    blob, info = build_bin(pixel_size, ascent, descent, glyphs)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(blob)
    print(
        f"wrote {args.output} {len(blob)} bytes glyphs={info['glyphs']} "
        f"line={info['ascent'] + info['descent']} subtables={info['subtables']} "
        f"bits xy={info['xy_bits']} wh={info['wh_bits']} adv={info['adv_bits']}"
    )
    if args.hzk is not None:
        print(
            f"adv cjk={HZK_CJK_ADV} digit={HZK_DIGIT_ADV} colon={HZK_COLON_ADV} "
            f"space={glyphs[0x20]['adv']}"
        )


if __name__ == "__main__":
    main()
