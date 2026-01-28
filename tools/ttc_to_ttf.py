#!/usr/bin/env python3
"""
TTC to TTF Converter
Extracts individual TTF fonts from a TrueType Collection (.ttc) file

Usage:
    python ttc_to_ttf.py <input.ttc> [output_dir]

Example:
    python ttc_to_ttf.py Helvetica.ttc ./output
"""

import sys
from pathlib import Path

try:
    from fontTools.ttLib import TTCollection
except ImportError:
    print("Error: fontTools not installed")
    print("Install with: pip install fonttools")
    sys.exit(1)


def convert_ttc_to_ttf(ttc_path: str, output_dir: str = None):
    """Convert TTC file to individual TTF files"""
    
    ttc_path = Path(ttc_path)
    if not ttc_path.exists():
        print(f"Error: File not found: {ttc_path}")
        return False
    
    if not ttc_path.suffix.lower() == '.ttc':
        print(f"Warning: File does not have .ttc extension: {ttc_path}")
    
    # Set output directory
    if output_dir:
        out_dir = Path(output_dir)
    else:
        out_dir = ttc_path.parent
    
    out_dir.mkdir(parents=True, exist_ok=True)
    
    print(f"Input: {ttc_path}")
    print(f"Output directory: {out_dir}")
    print()
    
    try:
        ttc = TTCollection(str(ttc_path))
    except Exception as e:
        print(f"Error reading TTC file: {e}")
        return False
    
    print(f"Found {len(ttc)} fonts in collection:")
    print()
    
    base_name = ttc_path.stem
    
    for i, font in enumerate(ttc):
        # Try to get font name from name table
        try:
            name_table = font['name']
            # Try different name IDs for font family name
            font_name = None
            for name_id in [4, 1, 6]:  # Full name, Family name, PostScript name
                for record in name_table.names:
                    if record.nameID == name_id:
                        try:
                            font_name = record.toUnicode()
                            break
                        except:
                            continue
                if font_name:
                    break
            
            if not font_name:
                font_name = f"{base_name}_{i}"
        except:
            font_name = f"{base_name}_{i}"
        
        # Clean font name for filename
        safe_name = "".join(c if c.isalnum() or c in "- _" else "_" for c in font_name)
        output_path = out_dir / f"{safe_name}.ttf"
        
        # Handle duplicate names
        counter = 1
        while output_path.exists():
            output_path = out_dir / f"{safe_name}_{counter}.ttf"
            counter += 1
        
        print(f"  [{i}] {font_name}")
        print(f"      -> {output_path}")
        
        try:
            font.save(str(output_path))
        except Exception as e:
            print(f"      Error saving: {e}")
            continue
    
    print()
    print("Done!")
    return True


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    
    ttc_path = sys.argv[1]
    output_dir = sys.argv[2] if len(sys.argv) > 2 else None
    
    success = convert_ttc_to_ttf(ttc_path, output_dir)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
