#!/usr/bin/env python3
"""
Back up every photo on a little-camera by reading its flash. Writes nothing to
the device.

    python3 dump_flash.py --port /dev/cu.usbmodem1101

Reads the partition table from the chip, finds the LittleFS partition (the one
the table labels `spiffs`), pulls the whole partition with esptool, mounts the
image with littlefs-python and writes each photo out as .pbm (the original
bytes) and .png (for previewing). The raw partition image is kept too, so the
backup can be re-extracted later with different tooling if this script's
LittleFS decode ever disagrees with the firmware's.

Works with any firmware version, including the original one that had no way
to export at all. Run it before flashing anything new; run it again whenever
you feel like it.

The XIAO ESP32-S3 has to be in the bootloader for esptool to talk to it.
esptool normally gets there on its own over the native USB port. If it times
out, hold BOOT while plugging the cable in, then run again.
"""

import argparse
import datetime as dt
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from pbmpng import parse_pbm, pbm_to_png  # noqa: E402

PARTITION_TABLE_OFFSET = 0x8000
PARTITION_TABLE_SIZE = 0xC00
ENTRY_MAGIC = 0x50AA
BLOCK_SIZE = 4096  # esp_littlefs uses the flash sector as the LittleFS block


def esptool_cmd():
    """esptool 5 renamed read_flash to read-flash; older versions only know the
    underscore form. Ask the installed one which it is."""
    import esptool  # noqa: F401  (import error is the friendliest failure)
    version = getattr(esptool, "__version__", "0")
    major = int(version.split(".")[0]) if version[:1].isdigit() else 0
    return [sys.executable, "-m", "esptool"], ("read-flash" if major >= 5 else "read_flash")


def read_flash(port, baud, offset, size, out_path):
    base, verb = esptool_cmd()
    cmd = base + ["--port", port, "--baud", str(baud), "--after", "hard-reset" if verb == "read-flash" else "hard_reset",
                  verb, f"0x{offset:X}", f"0x{size:X}", out_path]
    print("+", " ".join(cmd))
    subprocess.run(cmd, check=True)
    with open(out_path, "rb") as f:
        return f.read()


def parse_partition_table(blob):
    """32-byte entries: magic u16, type u8, subtype u8, offset u32, size u32, label[16], flags u32."""
    parts = []
    for i in range(0, len(blob), 32):
        entry = blob[i:i + 32]
        if len(entry) < 32:
            break
        magic, ptype, subtype, offset, size = struct.unpack("<HBBII", entry[:12])
        if magic != ENTRY_MAGIC:
            if magic == 0xEBEB:  # md5 checksum entry
                continue
            if entry == b"\xff" * 32:
                break
            continue
        label = entry[12:28].split(b"\0", 1)[0].decode("ascii", "replace")
        parts.append({"type": ptype, "subtype": subtype, "offset": offset, "size": size, "label": label})
    return parts


def find_littlefs(parts):
    # The firmware mounts by label ("spiffs" is the label default_8MB.csv
    # gives it; the format is LittleFS regardless). Fall back to the data
    # subtype 0x82 in case the label ever changes.
    for p in parts:
        if p["label"] == "spiffs":
            return p
    for p in parts:
        if p["type"] == 1 and p["subtype"] == 0x82:
            return p
    return None


def extract_with_littlefs_python(image, out_dir):
    from littlefs import LittleFS

    # read/prog/cache/lookahead mirror esp_littlefs' defaults; only block_size
    # and block_count are on disk, but the others must be internally consistent.
    fs = LittleFS(block_size=BLOCK_SIZE, block_count=len(image) // BLOCK_SIZE,
                  read_size=128, prog_size=128, cache_size=512, lookahead_size=128,
                  mount=False)
    fs.context.buffer = bytearray(image)
    fs.mount()
    names = sorted(n for n in fs.listdir("/") if n.lower().endswith(".pbm"))
    files = []
    for name in names:
        with fs.open("/" + name, "rb") as f:
            files.append((name, f.read()))
    return files


def extract_with_mklittlefs(image_path, out_dir):
    """Fallback: PlatformIO ships mklittlefs; `-u` unpacks an image."""
    tool = shutil.which("mklittlefs")
    if not tool:
        home = os.path.expanduser("~/.platformio/packages/tool-mklittlefs/mklittlefs")
        tool = home if os.path.exists(home) else None
    if not tool:
        return None
    tmp = os.path.join(out_dir, "_unpacked")
    os.makedirs(tmp, exist_ok=True)
    subprocess.run([tool, "-u", tmp, "-b", str(BLOCK_SIZE), "-p", "256", image_path], check=True)
    files = []
    for name in sorted(os.listdir(tmp)):
        if name.lower().endswith(".pbm"):
            with open(os.path.join(tmp, name), "rb") as f:
                files.append((name, f.read()))
    return files


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="serial port, e.g. /dev/cu.usbmodem1101")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--out", default=os.path.join(os.getcwd(), "backup"), help="backup root (default ./backup)")
    ap.add_argument("--image", help="skip the device and extract from an existing partition image")
    args = ap.parse_args()

    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = os.path.join(args.out, stamp)
    os.makedirs(out_dir, exist_ok=True)

    if args.image:
        image_path = args.image
        with open(image_path, "rb") as f:
            image = f.read()
    else:
        table_path = os.path.join(out_dir, "partitions.bin")
        table = read_flash(args.port, args.baud, PARTITION_TABLE_OFFSET, PARTITION_TABLE_SIZE, table_path)
        parts = parse_partition_table(table)
        for p in parts:
            print(f"  partition {p['label']:<10} type {p['type']} sub 0x{p['subtype']:02x} "
                  f"@ 0x{p['offset']:06X} size 0x{p['size']:06X}")
        lfs = find_littlefs(parts)
        if not lfs:
            sys.exit("no LittleFS/spiffs partition in the table; nothing to back up")
        image_path = os.path.join(out_dir, "littlefs.bin")
        image = read_flash(args.port, args.baud, lfs["offset"], lfs["size"], image_path)

    files = None
    try:
        files = extract_with_littlefs_python(image, out_dir)
    except Exception as e:  # keep going: the raw image is already on disk
        print(f"littlefs-python could not mount the image ({e}); trying mklittlefs")
        try:
            files = extract_with_mklittlefs(image_path, out_dir)
        except Exception as e2:
            print(f"mklittlefs failed too ({e2})")
    if files is None:
        sys.exit(f"could not decode the filesystem. The raw image is at {image_path}; "
                 "keep it — it contains every photo and can be unpacked later.")

    count = 0
    for name, data in files:
        base = os.path.splitext(name)[0]
        with open(os.path.join(out_dir, base + ".pbm"), "wb") as f:
            f.write(data)
        try:
            w, h, rows, meta = parse_pbm(data)
            with open(os.path.join(out_dir, base + ".png"), "wb") as f:
                f.write(pbm_to_png(w, h, rows))
            extra = f"  {meta}" if meta else ""
            print(f"  {name}  {w}x{h}{extra}")
        except ValueError as e:
            print(f"  {name}  (kept as-is, not a valid PBM: {e})")
        count += 1
    print(f"backed up {count} photo(s) to {out_dir}")


if __name__ == "__main__":
    main()
