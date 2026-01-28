#!/usr/bin/env python3
"""
TTF Font Character Map Analyzer
Analyzes TTF/OTF font files and outputs encoding ranges in lv_font_conv --range format
"""

import sys
from pathlib import Path
from fontTools.ttLib import TTFont


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


def format_range_lv_font_conv(start, end):
    """Format a range for lv_font_conv --range parameter"""
    if start == end:
        return f"0x{start:04X}"
    else:
        return f"0x{start:04X}-0x{end:04X}"


def analyze_ttf(font_path):
    """Analyze TTF font and output lv_font_conv --range format"""
    font = TTFont(font_path)
    
    # Get font name
    font_name = Path(font_path).stem
    if 'name' in font:
        for record in font['name'].names:
            if record.nameID == 4:  # Full Name
                try:
                    font_name = record.toUnicode()
                    break
                except:
                    pass
    
    # Get all codepoints from cmap
    cmap = font.getBestCmap()
    if cmap is None:
        print("ERROR: No cmap table found in font", file=sys.stderr)
        return
    
    codepoints = sorted(cmap.keys())
    total_glyphs = len(codepoints)
    
    # Find continuous ranges
    ranges = find_continuous_ranges(codepoints)
    
    # Output summary to stderr
    print(f"# Font: {font_name}", file=sys.stderr)
    print(f"# Total Characters: {total_glyphs}", file=sys.stderr)
    print(f"# Total Ranges: {len(ranges)}", file=sys.stderr)
    print(file=sys.stderr)
    
    # Output lv_font_conv --range format to stdout
    range_args = []
    for start, end in ranges:
        range_args.append(format_range_lv_font_conv(start, end))
    
    # Output as comma-separated for single --range parameter
    print(f"--range={','.join(range_args)}")
    
    font.close()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python analyze_ttf_cmap.py <font.ttf>")
        print("Example: python analyze_ttf_cmap.py fonts/post_pixel-7.ttf")
        print()
        print("Output: lv_font_conv --range parameter format")
        sys.exit(1)
    
    font_path = sys.argv[1]
    
    if not Path(font_path).exists():
        print(f"ERROR: File not found: {font_path}", file=sys.stderr)
        sys.exit(1)
    
    analyze_ttf(font_path)
