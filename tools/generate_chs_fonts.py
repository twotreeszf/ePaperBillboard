#!/usr/bin/env python3
"""
Chinese Font Generator for LVGL
Generates binary font files for multiple sizes from a Chinese TTF font.
Uses fixed Unicode ranges for CJK characters.
"""

import sys
import subprocess
from pathlib import Path

# Fixed Chinese font ranges
CHS_RANGES = [
    "0x20-0x7E",      # ASCII (Basic Latin)
    "0x3000-0x303F",  # CJK Symbols and Punctuation (。、「」 etc.)
    "0xFF00-0xFFEF",  # Fullwidth Forms (，：？！ etc.)
    "0x4E00-0x9FFF",  # CJK Unified Ideographs
]


def generate_font(font_path, actual_size, output_path, nominal_size=None):
    """Generate a single font file using lv_font_conv"""
    cmd = [
        "lv_font_conv",
        "--font", str(font_path),
        "--size", str(actual_size),
        "--bpp", "1",
        "--format", "bin",
        "--no-compress",
    ]
    
    # Add each range separately
    for r in CHS_RANGES:
        cmd.extend(["--range", r])
    
    cmd.extend(["-o", str(output_path)])
    
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
        print("Usage: python generate_chs_fonts.py <font.ttf> <sizes> [size_offset]")
        print()
        print("Arguments:")
        print("  font.ttf     Source Chinese TTF font file")
        print("  sizes        Comma-separated sizes (e.g., 12,14,16)")
        print("  size_offset  Size offset for actual rendering (default: 0)")
        print()
        print("Fixed Unicode ranges:")
        for r in CHS_RANGES:
            print(f"  {r}")
        print()
        print("Example:")
        print("  python generate_chs_fonts.py fonts/simsun.ttf 12,14,16")
        print("  -> Generates: chs_12.bin, chs_14.bin, chs_16.bin")
        print()
        print("  python generate_chs_fonts.py fonts/simsun.ttf 12,14,16 2")
        print("  -> Generates: chs_12.bin (14px), chs_14.bin (16px), chs_16.bin (18px)")
        sys.exit(1)
    
    font_path = Path(sys.argv[1])
    sizes_str = sys.argv[2]
    size_offset = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    prefix = "chs"
    
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
    if size_offset != 0:
        print(f"Size offset: {size_offset:+d}")
    print(f"Output: {output_dir}/")
    print()
    print("Unicode ranges:")
    for r in CHS_RANGES:
        print(f"  {r}")
    print()
    print("Generating fonts (this may take a while for CJK)...")
    
    # Generate each size
    success_count = 0
    for size in sizes:
        actual_size = size + size_offset
        output_path = output_dir / f"{prefix}_{size}.bin"
        if generate_font(font_path, actual_size, output_path, size):
            success_count += 1
    
    print()
    print(f"Done! Generated {success_count}/{len(sizes)} fonts.")
    
    if success_count == len(sizes):
        print()
        print("Usage in code:")
        print(f'  fontLoader.begin("/fonts/{prefix}_{sizes[0]}.bin", "/fonts/en_{sizes[0]}.bin");')


if __name__ == "__main__":
    main()
