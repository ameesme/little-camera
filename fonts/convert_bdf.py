#!/usr/bin/env python3
"""Convert BDF font files to C header arrays - full ASCII support."""

import re

def parse_bdf(filename):
    """Parse BDF file and extract all printable ASCII characters (32-126)."""
    glyphs = {}

    with open(filename, 'r') as f:
        content = f.read()

    # Extract font bounding box for dimensions
    bbx_match = re.search(r'FONTBOUNDINGBOX\s+(\d+)\s+(\d+)', content)
    if bbx_match:
        font_width = int(bbx_match.group(1))
        font_height = int(bbx_match.group(2))
    else:
        font_width, font_height = 6, 12

    # Split into character blocks
    char_blocks = re.split(r'STARTCHAR\s+', content)[1:]

    for block in char_blocks:
        lines = block.strip().split('\n')

        encoding = None
        bitmap_start = None
        for i, line in enumerate(lines):
            if line.startswith('ENCODING'):
                encoding = int(line.split()[1])
            if line.strip() == 'BITMAP':
                bitmap_start = i + 1
                break

        if encoding is None or bitmap_start is None:
            continue

        # Only include printable ASCII (32-126)
        if encoding < 32 or encoding > 126:
            continue

        # Extract bitmap data
        bitmap = []
        for line in lines[bitmap_start:]:
            if line.strip() == 'ENDCHAR':
                break
            if line.strip():
                try:
                    bitmap.append(int(line.strip(), 16))
                except ValueError:
                    pass

        # Pad to font_height
        while len(bitmap) < font_height:
            bitmap.append(0)
        bitmap = bitmap[:font_height]

        glyphs[encoding] = bitmap

    return glyphs, font_width, font_height


def generate_c_header(glyphs, font_width, font_height, font_name):
    """Generate C header code for the font with full ASCII support."""
    output = []
    output.append(f"// {font_name} font - {font_width}x{font_height}")
    output.append(f"// Printable ASCII (32-126)")
    output.append(f"")
    output.append(f"constexpr int {font_name.upper()}_WIDTH = {font_width};")
    output.append(f"constexpr int {font_name.upper()}_HEIGHT = {font_height};")
    output.append(f"constexpr int {font_name.upper()}_FIRST_CHAR = 32;")
    output.append(f"constexpr int {font_name.upper()}_LAST_CHAR = 126;")
    output.append(f"")
    output.append(f"static const uint8_t {font_name.upper()}_DATA[][{font_height}] = {{")

    for encoding in range(32, 127):
        if encoding in glyphs:
            bitmap = glyphs[encoding]
            hex_vals = ', '.join(f'0x{b:02X}' for b in bitmap)
            char_repr = chr(encoding) if encoding >= 33 else 'space'
            if char_repr == '\\':
                char_repr = 'backslash'
            elif char_repr == "'":
                char_repr = 'apostrophe'
            output.append(f"    {{{hex_vals}}},  // {encoding}: '{char_repr}'")
        else:
            # Empty glyph for missing characters
            hex_vals = ', '.join(['0x00'] * font_height)
            output.append(f"    {{{hex_vals}}},  // {encoding}: (missing)")

    output.append("};")
    output.append("")

    # Helper function to get glyph
    output.append(f"static inline const uint8_t* {font_name}_getGlyph(char c) {{")
    output.append(f"    if (c < {font_name.upper()}_FIRST_CHAR || c > {font_name.upper()}_LAST_CHAR) {{")
    output.append(f"        return {font_name.upper()}_DATA[0];  // Return space for out-of-range")
    output.append(f"    }}")
    output.append(f"    return {font_name.upper()}_DATA[c - {font_name.upper()}_FIRST_CHAR];")
    output.append(f"}}")

    return '\n'.join(output)


if __name__ == '__main__':
    # Parse 6x12
    glyphs_6x12, w1, h1 = parse_bdf('spleen-6x12.bdf')
    print(f"Spleen 6x12: found {len(glyphs_6x12)} glyphs")
    header_6x12 = generate_c_header(glyphs_6x12, 6, 12, 'spleen6x12')

    # Parse 8x16
    glyphs_8x16, w2, h2 = parse_bdf('spleen-8x16.bdf')
    print(f"Spleen 8x16: found {len(glyphs_8x16)} glyphs")
    header_8x16 = generate_c_header(glyphs_8x16, 8, 16, 'spleen8x16')

    # Write to file
    with open('spleen_fonts.h', 'w') as f:
        f.write("#pragma once\n\n")
        f.write("// Spleen bitmap fonts by Frederic Cambus\n")
        f.write("// https://github.com/fcambus/spleen\n")
        f.write("// License: BSD-2-Clause\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(header_6x12)
        f.write("\n\n")
        f.write(header_8x16)

    print("Generated spleen_fonts.h")
