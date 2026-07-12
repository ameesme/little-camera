#!/usr/bin/env python3
"""
Convert Lucide SVG icons to C bitmap arrays for Sharp Memory LCD.
Uses rsvg-convert for SVG rasterization, then ImageMagick for processing.
"""

import subprocess
import sys
import tempfile
import re
from pathlib import Path

# Icon definitions: (svg_file, c_name)
ICONS = [
    ("lucide-battery.svg", "BATTERY_EMPTY"),
    ("lucide-battery-low.svg", "BATTERY_LOW"),
    ("lucide-battery-medium.svg", "BATTERY_MED"),
    ("lucide-battery-full.svg", "BATTERY_FULL"),
    ("lucide-signal-low.svg", "SIGNAL_1"),
    ("lucide-signal-medium.svg", "SIGNAL_2"),
    ("lucide-signal-high.svg", "SIGNAL_3"),
    ("lucide-signal.svg", "SIGNAL_FULL"),
    ("lucide-mail.svg", "MAIL"),
]

SIZE = 24
BYTES_PER_ROW = (SIZE + 7) // 8  # 3 bytes for 24 pixels


def fix_svg_colors(svg_content: str) -> str:
    """Replace currentColor with black for proper rendering."""
    return svg_content.replace('currentColor', '#000000')


def svg_to_bitmap(svg_path: Path, size: int = 24, threshold: int = 128) -> list[list[int]]:
    """Rasterize SVG to monochrome bitmap."""
    # Read and fix the SVG
    svg_content = svg_path.read_text()
    fixed_svg = fix_svg_colors(svg_content)

    with tempfile.NamedTemporaryFile(suffix='.svg', mode='w', delete=False) as f:
        f.write(fixed_svg)
        temp_svg = f.name

    with tempfile.NamedTemporaryFile(suffix='.png', delete=False) as f:
        temp_png = f.name

    try:
        # Render SVG to PNG using rsvg-convert
        subprocess.run([
            'rsvg-convert', '-w', str(size), '-h', str(size),
            temp_svg, '-o', temp_png
        ], check=True, capture_output=True)

        # Use ImageMagick to composite onto white background and get pixel data
        result = subprocess.run([
            'magick', '-size', f'{size}x{size}', 'xc:white',
            temp_png, '-composite', '-colorspace', 'gray', 'txt:-'
        ], capture_output=True, text=True, check=True)

        # Parse pixel data
        bitmap = [[1] * size for _ in range(size)]  # Default white (1)

        for line in result.stdout.strip().split('\n'):
            if line.startswith('#'):
                continue
            # Parse "0,0: (value)  #HEXHEX  gray(N)"
            match = re.match(r'(\d+),(\d+):\s+\((\d+)\)', line)
            if match:
                x, y, val = int(match.group(1)), int(match.group(2)), int(match.group(3))
                if x < size and y < size:
                    # Threshold: below threshold = black (1 = set bit), above = white (0)
                    # We want icon strokes (black in SVG) to have bits SET
                    bitmap[y][x] = 0 if val >= threshold else 1

        return bitmap

    finally:
        Path(temp_svg).unlink(missing_ok=True)
        Path(temp_png).unlink(missing_ok=True)


def bitmap_to_c_array(bitmap: list[list[int]], name: str) -> str:
    """Convert bitmap to C array declaration."""
    lines = []
    lines.append(f"static const uint8_t {name}[{SIZE} * {BYTES_PER_ROW}] = {{")

    for row in bitmap:
        row_bytes = []
        for byte_idx in range(BYTES_PER_ROW):
            byte_val = 0
            for bit in range(8):
                pixel_idx = byte_idx * 8 + bit
                if pixel_idx < SIZE and row[pixel_idx]:
                    byte_val |= (1 << (7 - bit))  # MSB first
            row_bytes.append(f"0x{byte_val:02X}")
        lines.append(f"    {', '.join(row_bytes)},")

    lines.append("};")
    return '\n'.join(lines)


def bitmap_to_ascii(bitmap: list[list[int]]) -> str:
    """Convert bitmap to ASCII art for debugging."""
    result = []
    for row in bitmap:
        line = ''.join('#' if p else '.' for p in row)  # 1 = stroke (#), 0 = background (.)
        result.append(line)
    return '\n'.join(result)


def main():
    script_dir = Path(__file__).parent

    # Check for --debug flag
    debug = '--debug' in sys.argv

    if not debug:
        print("#pragma once")
        print()
        print("// Auto-generated from Lucide SVG icons")
        print("// Run: python3 convert_lucide_icons.py > ../src/icons.h")
        print()
        print("#include <stdint.h>")
        print()
        print("namespace Icons {")
        print()
        print(f"constexpr int SIZE = {SIZE};")
        print()

    # Add empty signal icon (no bars) - all zeros = no strokes
    if not debug:
        print("// Empty signal (no bars)")
        empty_bitmap = [[0] * SIZE for _ in range(SIZE)]
        print(bitmap_to_c_array(empty_bitmap, "SIGNAL_0"))
        print()

    for svg_file, c_name in ICONS:
        svg_path = script_dir / svg_file
        if not svg_path.exists():
            print(f"// WARNING: {svg_file} not found", file=sys.stderr)
            continue

        bitmap = svg_to_bitmap(svg_path, SIZE)

        if debug:
            print(f"=== {c_name} ({svg_file}) ===")
            print(bitmap_to_ascii(bitmap))
            print()
        else:
            print(f"// {svg_file}")
            print(bitmap_to_c_array(bitmap, c_name))
            print()

    if not debug:
        print("}  // namespace Icons")


if __name__ == "__main__":
    main()
