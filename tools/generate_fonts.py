#!/usr/bin/env python3
"""
Batch Font Generator for LVGL
Generates binary font files for multiple sizes from a TTF font.
"""

import sys
import subprocess
from pathlib import Path
from fontTools.ttLib import TTFont


# Unicode ranges to exclude (Traditional Chinese / CJK extensions)
# Excluding these reduces font size when only Simplified Chinese is needed
EXCLUDED_RANGES = [
    (0x3400, 0x4DBF),   # CJK Unified Ideographs Extension A
    (0xF900, 0xFAFF),   # CJK Compatibility Ideographs
    (0x20000, 0x2A6DF), # CJK Unified Ideographs Extension B
    (0x2A700, 0x2B73F), # CJK Unified Ideographs Extension C
    (0x2B740, 0x2B81F), # CJK Unified Ideographs Extension D
    (0x2B820, 0x2CEAF), # CJK Unified Ideographs Extension E
    (0x2CEB0, 0x2EBEF), # CJK Unified Ideographs Extension F
    (0x30000, 0x3134F), # CJK Unified Ideographs Extension G
]


def _subtract_range(from_range, exclude_range):
    """Subtract exclude_range from from_range. Returns list of 0, 1, or 2 sub-ranges."""
    a, b = from_range
    e_start, e_end = exclude_range
    if b < e_start or a > e_end:
        return [from_range]
    out = []
    if a < e_start and e_start - 1 >= a:
        out.append((a, min(b, e_start - 1)))
    if e_end < b and e_end + 1 <= b:
        out.append((max(a, e_end + 1), b))
    return out


def _subtract_excluded_from_ranges(ranges):
    """Subtract all EXCLUDED_RANGES from ranges (set difference on ranges)."""
    result = list(ranges)
    for excl in EXCLUDED_RANGES:
        next_result = []
        for r in result:
            next_result.extend(_subtract_range(r, excl))
        result = next_result
    return result


def find_continuous_ranges(codepoints):
    """Find continuous ranges in a sorted list of codepoints"""
    if not codepoints:
        return []
    
    ranges = []
    start = codepoints[0]
    end = codepoints[0]
    
    for cp in codepoints[1:]:
        if cp == end + 1:
            end = cp
        else:
            ranges.append((start, end))
            start = cp
            end = cp
    
    ranges.append((start, end))
    return ranges


def get_font_ranges(font_path):
    """Extract encoding ranges from TTF font"""
    font = TTFont(font_path)
    cmap = font.getBestCmap()
    
    if cmap is None:
        raise ValueError(f"No cmap table found in {font_path}")
    
    codepoints = sorted(cmap.keys())
    ranges = find_continuous_ranges(codepoints)
    ranges = _subtract_excluded_from_ranges(ranges)

    # Format as lv_font_conv --range parameter
    range_args = []
    for start, end in ranges:
        if start == end:
            range_args.append(f"0x{start:04X}")
        else:
            range_args.append(f"0x{start:04X}-0x{end:04X}")
    
    font.close()
    return ",".join(range_args)


def generate_font(font_path, actual_size, output_path, ranges, nominal_size=None):
    """Generate a single font file using lv_font_conv"""
    cmd = [
        "lv_font_conv",
        "--font", str(font_path),
        "--size", str(actual_size),
        "--bpp", "1",
        "--format", "bin",
        "--no-compress",
        f"--range={ranges}",
        "-o", str(output_path)
    ]
    
    if nominal_size and nominal_size != actual_size:
        print(f"  Generating {output_path.name} (nominal {nominal_size}, actual {actual_size})...")
    else:
        print(f"  Generating {output_path.name} (size {actual_size})...")
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"    Error: {result.stderr.strip()}")
            return False
        return True
    except FileNotFoundError:
        print("    Error: lv_font_conv not found. Install with: npm install -g lv_font_conv")
        return False


def main():
    if len(sys.argv) < 3:
        print("Usage: python generate_fonts.py <font.ttf> <sizes> [prefix] [size_offset]")
        print()
        print("Arguments:")
        print("  font.ttf     Source TTF font file")
        print("  sizes        Comma-separated sizes (e.g., 12,14,16)")
        print("  prefix       Output filename prefix (default: 'en')")
        print("  size_offset Size offset for actual rendering (default: 0)")
        print()
        print("Example:")
        print("  python generate_fonts.py fonts/pixel.ttf 12,14,16")
        print("  -> Generates: en_12.bin (12px), en_14.bin (14px), en_16.bin (16px)")
        print()
        print("  python generate_fonts.py fonts/pixel.ttf 12,14,16 pixel")
        print("  -> Generates: pixel_12.bin (12px), pixel_14.bin (14px), pixel_16.bin (16px)")
        print()
        print("  python generate_fonts.py fonts/pixel.ttf 12,14,16 pixel 2")
        print("  -> Generates: pixel_12.bin (14px), pixel_14.bin (16px), pixel_16.bin (18px)")
        sys.exit(1)
    
    font_path = Path(sys.argv[1])
    sizes_str = sys.argv[2]
    
    # Parse optional arguments
    prefix = "en"
    size_offset = 0
    
    if len(sys.argv) > 3:
        # Check if 3rd arg is a number (size_offset) or string (prefix)
        try:
            size_offset = int(sys.argv[3])
            # If 4th arg exists, it's prefix
            if len(sys.argv) > 4:
                prefix = sys.argv[4]
        except ValueError:
            # 3rd arg is prefix
            prefix = sys.argv[3]
            # If 4th arg exists, it's size_offset
            if len(sys.argv) > 4:
                try:
                    size_offset = int(sys.argv[4])
                except ValueError:
                    print(f"Error: Invalid size_offset: {sys.argv[4]}")
                    sys.exit(1)
    
    # Validate font file
    if not font_path.exists():
        print(f"Error: Font file not found: {font_path}")
        sys.exit(1)
    
    # Parse sizes
    try:
        sizes = [int(s.strip()) for s in sizes_str.split(",")]
    except ValueError:
        print(f"Error: Invalid sizes format: {sizes_str}")
        print("Expected comma-separated integers, e.g., 12,14,16")
        sys.exit(1)
    
    # Determine output directory
    script_dir = Path(__file__).parent.parent
    output_dir = script_dir / "data" / "fonts"
    output_dir.mkdir(parents=True, exist_ok=True)
    
    print(f"Font: {font_path}")
    print(f"Sizes: {sizes}")
    print(f"Prefix: {prefix}")
    if size_offset != 0:
        print(f"Size offset: {size_offset:+d}")
    print(f"Output: {output_dir}/")
    print()
    
    # Extract font ranges
    print("Analyzing font encoding ranges...")
    try:
        ranges = get_font_ranges(font_path)
        print(f"  Found ranges: {ranges[:80]}{'...' if len(ranges) > 80 else ''}")
    except Exception as e:
        print(f"Error analyzing font: {e}")
        sys.exit(1)
    
    print()
    print("Generating fonts...")
    
    # Generate each size
    success_count = 0
    for size in sizes:
        actual_size = size + size_offset
        output_path = output_dir / f"{prefix}_{size}.bin"
        if generate_font(font_path, actual_size, output_path, ranges, size):
            success_count += 1
    
    print()
    print(f"Done! Generated {success_count}/{len(sizes)} fonts.")
    
    if success_count == len(sizes):
        print()
        print("Usage in code:")
        print(f'  fontLoader.begin("/fonts/chs_{sizes[0]}.bin", "/fonts/{prefix}_{sizes[0]}.bin");')


if __name__ == "__main__":
    main()
