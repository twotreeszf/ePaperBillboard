#!/usr/bin/env python3
"""
Extract images from PDF, EPUB, MOBI, or ZIP files and save them to a folder with the same name.
Images are saved with sequential names like 0001.jpg, 0002.jpg, etc.
"""

import argparse
import io
import shutil
import sys
import zipfile
from pathlib import Path

import fitz  # PyMuPDF
import mobi
from PIL import Image

IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".gif", ".webp", ".bmp", ".tiff"}


def save_image_as_jpg(image_bytes: bytes, output_path: Path) -> None:
    """Convert image bytes to JPG and save. Skip conversion if already JPEG."""
    img = Image.open(io.BytesIO(image_bytes))
    
    # If already JPEG and no mode conversion needed, save original bytes directly
    if img.format == "JPEG" and img.mode not in ("RGBA", "P"):
        with open(output_path, "wb") as f:
            f.write(image_bytes)
        return
    
    # Convert to RGB if needed and save as JPEG
    if img.mode in ("RGBA", "P"):
        img = img.convert("RGB")
    img.save(output_path, "JPEG", quality=85)


def extract_images_from_pdf(file_path: Path, output_folder: Path) -> int:
    """Extract images from a PDF file. Returns the number of images extracted."""
    doc = fitz.open(file_path)
    image_count = 0
    
    print(f"Processing {len(doc)} pages...")
    
    for page_num in range(len(doc)):
        page = doc[page_num]
        image_list = page.get_images(full=True)
        
        for img_index, img_info in enumerate(image_list):
            xref = img_info[0]
            
            try:
                base_image = doc.extract_image(xref)
                image_bytes = base_image["image"]
                
                image_count += 1
                image_filename = f"{image_count:04d}.jpg"
                image_path = output_folder / image_filename
                
                save_image_as_jpg(image_bytes, image_path)
                print(f"  Extracted: {image_filename} (page {page_num + 1})")
                
            except Exception as e:
                print(f"  Warning: Failed to extract image on page {page_num + 1}: {e}")
    
    doc.close()
    return image_count


def extract_images_from_epub(file_path: Path, output_folder: Path) -> int:
    """Extract images from an EPUB file. Returns the number of images extracted."""
    image_count = 0
    
    with zipfile.ZipFile(file_path, "r") as epub:
        # Get all image files from the EPUB
        image_files = [
            name for name in epub.namelist()
            if Path(name).suffix.lower() in IMAGE_EXTENSIONS
        ]
        
        # Sort to ensure consistent ordering
        image_files.sort()
        
        print(f"Found {len(image_files)} images in EPUB...")
        
        for image_name in image_files:
            try:
                image_bytes = epub.read(image_name)
                
                image_count += 1
                image_filename = f"{image_count:04d}.jpg"
                image_path = output_folder / image_filename
                
                save_image_as_jpg(image_bytes, image_path)
                print(f"  Extracted: {image_filename} ({image_name})")
                
            except Exception as e:
                print(f"  Warning: Failed to extract {image_name}: {e}")
    
    return image_count


def extract_images_from_zip(file_path: Path, output_folder: Path) -> int:
    """Extract images from a ZIP file. Returns the number of images extracted."""
    image_count = 0
    
    with zipfile.ZipFile(file_path, "r") as zf:
        # Get all image files from the ZIP
        image_files = [
            name for name in zf.namelist()
            if Path(name).suffix.lower() in IMAGE_EXTENSIONS
        ]
        
        # Sort to ensure consistent ordering
        image_files.sort()
        
        print(f"Found {len(image_files)} images in ZIP...")
        
        for image_name in image_files:
            try:
                image_bytes = zf.read(image_name)
                
                image_count += 1
                image_filename = f"{image_count:04d}.jpg"
                image_path = output_folder / image_filename
                
                save_image_as_jpg(image_bytes, image_path)
                print(f"  Extracted: {image_filename} ({image_name})")
                
            except Exception as e:
                print(f"  Warning: Failed to extract {image_name}: {e}")
    
    return image_count


def extract_images_from_mobi(file_path: Path, output_folder: Path) -> int:
    """Extract images from a MOBI file. Returns the number of images extracted."""
    image_count = 0
    
    # Unpack MOBI file to a temporary directory
    tempdir, extracted_file = mobi.extract(str(file_path))
    tempdir_path = Path(tempdir)
    
    try:
        print(f"Unpacked MOBI to: {tempdir_path}")
        
        # Prefer mobi8 (KF8 format) over mobi7 (older format) to avoid duplicates
        mobi8_dir = tempdir_path / "mobi8"
        mobi7_dir = tempdir_path / "mobi7"
        
        if mobi8_dir.exists():
            search_dir = mobi8_dir
            print("Using mobi8 (KF8) format...")
        elif mobi7_dir.exists():
            search_dir = mobi7_dir
            print("Using mobi7 format...")
        else:
            search_dir = tempdir_path
            print("No mobi7/mobi8 subdirectory found, searching entire directory...")
        
        # Find all image files in the selected directory
        image_files = []
        for ext in IMAGE_EXTENSIONS:
            image_files.extend(search_dir.rglob(f"*{ext}"))
        
        # Sort by filename to ensure consistent ordering
        image_files.sort(key=lambda p: p.name)
        
        print(f"Found {len(image_files)} images in MOBI...")
        
        for image_file in image_files:
            try:
                image_bytes = image_file.read_bytes()
                
                image_count += 1
                image_filename = f"{image_count:04d}.jpg"
                image_path = output_folder / image_filename
                
                save_image_as_jpg(image_bytes, image_path)
                print(f"  Extracted: {image_filename} ({image_file.name})")
                
            except Exception as e:
                print(f"  Warning: Failed to extract {image_file.name}: {e}")
    finally:
        # Clean up temporary directory
        shutil.rmtree(tempdir_path, ignore_errors=True)
        print(f"Cleaned up temporary directory: {tempdir_path}")
    
    return image_count


def extract_images(input_path: str) -> None:
    """Main function to extract images from PDF, EPUB, MOBI, or ZIP files."""
    input_file = Path(input_path)
    
    if not input_file.exists():
        print(f"Error: File not found: {input_path}")
        sys.exit(1)
    
    suffix = input_file.suffix.lower()
    if suffix not in (".pdf", ".epub", ".mobi", ".zip"):
        print(f"Error: Unsupported file format: {suffix}")
        print("Supported formats: .pdf, .epub, .mobi, .zip")
        sys.exit(1)
    
    # Create output folder with the same name as the input file (without extension)
    output_folder = input_file.parent / input_file.stem
    output_folder.mkdir(exist_ok=True)
    print(f"Output folder: {output_folder}")
    
    if suffix == ".pdf":
        image_count = extract_images_from_pdf(input_file, output_folder)
    elif suffix == ".epub":
        image_count = extract_images_from_epub(input_file, output_folder)
    elif suffix == ".zip":
        image_count = extract_images_from_zip(input_file, output_folder)
    else:  # .mobi
        image_count = extract_images_from_mobi(input_file, output_folder)
    
    if image_count == 0:
        print("No images found in the file.")
    else:
        print(f"\nDone! Extracted {image_count} images to: {output_folder}")


def main():
    parser = argparse.ArgumentParser(
        description="Extract images from PDF, EPUB, MOBI, or ZIP files."
    )
    parser.add_argument(
        "input_file",
        help="Path to the PDF, EPUB, MOBI, or ZIP file"
    )
    
    args = parser.parse_args()
    extract_images(args.input_file)


if __name__ == "__main__":
    main()
