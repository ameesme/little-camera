"""
Minimal PBM (P4) reader and 1-bit PNG writer, standard library only.

Shared by the export tools so a backup lands on disk both as the original
bytes (.pbm) and as something Finder can preview (.png). No Pillow: the
export tools must run on a machine that has never installed anything but
esptool.
"""

import struct
import zlib


def parse_pbm(data: bytes):
    """Return (width, height, rows, meta). rows is the packed raster, set bit = black.

    Accepts the two header shapes the firmware has produced: the original
    `P4\\n320 240\\n` and the newer one with a `# boot= up= t=` comment line.
    Any `#` comment between tokens is skipped, per the PBM spec.
    """
    if data[:2] != b"P4":
        raise ValueError("not a binary PBM (P4)")
    pos = 2
    tokens = []
    meta = {}
    while len(tokens) < 2:
        if pos >= len(data):
            raise ValueError("truncated header")
        c = data[pos:pos + 1]
        if c == b"#":
            end = data.find(b"\n", pos)
            if end < 0:
                raise ValueError("truncated comment")
            for part in data[pos + 1:end].decode("ascii", "replace").split():
                if "=" in part:
                    k, v = part.split("=", 1)
                    if v.lstrip("-").isdigit():
                        meta[k] = int(v)
            pos = end + 1
        elif c.isspace():
            pos += 1
        else:
            end = pos
            while end < len(data) and data[end:end + 1].isdigit():
                end += 1
            if end == pos:
                raise ValueError("bad header token")
            tokens.append(int(data[pos:end]))
            pos = end
    pos += 1  # single whitespace byte before the raster
    width, height = tokens
    stride = (width + 7) // 8
    rows = data[pos:pos + stride * height]
    if len(rows) != stride * height:
        raise ValueError(f"raster is {len(rows)} bytes, expected {stride * height}")
    return width, height, rows, meta


def _chunk(kind: bytes, payload: bytes) -> bytes:
    return (struct.pack(">I", len(payload)) + kind + payload
            + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))


def pbm_to_png(width: int, height: int, rows: bytes) -> bytes:
    """1-bit greyscale PNG. PNG's 1 is white and PBM's 1 is black, so invert."""
    stride = (width + 7) // 8
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter: none
        raw.extend(b ^ 0xFF for b in rows[y * stride:(y + 1) * stride])
    ihdr = struct.pack(">IIBBBBB", width, height, 1, 0, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", ihdr)
            + _chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + _chunk(b"IEND", b""))
