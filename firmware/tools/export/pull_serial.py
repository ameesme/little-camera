#!/usr/bin/env python3
"""
Pull every photo off a little-camera over its USB console.

    python3 pull_serial.py --port /dev/cu.usbmodem1101

Needs firmware with the serial console (docs/protocol.md §6). The camera sleeps
ten seconds after its last activity and does not listen while asleep, so press
the shutter just before running this; each command counts as activity and keeps
it awake for the rest of the transfer.

Writes backup/<timestamp>/NNNN.pbm (the exact bytes from the device, CRC
checked) and NNNN.png.
"""

import argparse
import datetime as dt
import os
import sys
import time
import zlib

import serial

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from pbmpng import parse_pbm, pbm_to_png  # noqa: E402


def readline(port, timeout=3.0):
    port.timeout = timeout
    line = port.readline()
    if not line:
        raise TimeoutError("no answer from the camera — is it awake? press the shutter and retry")
    return line.decode("ascii", "replace").rstrip("\r\n")


def command(port, text):
    port.reset_input_buffer()
    port.write((text + "\n").encode("ascii"))
    port.flush()


def list_photos(port):
    command(port, "ls")
    photos = []
    while True:
        line = readline(port)
        if line == "ok":
            return photos
        if line.startswith("err"):
            raise RuntimeError(line)
        parts = line.split()
        if len(parts) == 3 and parts[0].isdigit():
            photos.append((int(parts[0]), int(parts[1]), parts[2] == "1"))
        # Anything else is log noise from the firmware; ignore it.


def get_photo(port, index):
    command(port, f"get {index}")
    while True:
        line = readline(port)
        if line.startswith("begin "):
            break
        if line.startswith("err"):
            raise RuntimeError(line)
    _, idx, size = line.split()
    size = int(size)
    data = bytearray()
    while True:
        line = readline(port)
        if line.startswith("end "):
            crc = int(line.split()[1], 16)
            break
        if line.startswith("err"):
            raise RuntimeError(line)
        try:
            data.extend(bytes.fromhex(line))
        except ValueError:
            continue  # log line in the middle of the dump
    if len(data) != size:
        raise RuntimeError(f"#{index}: got {len(data)} bytes, expected {size}")
    if zlib.crc32(bytes(data)) & 0xFFFFFFFF != crc:
        raise RuntimeError(f"#{index}: CRC mismatch")
    return bytes(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--out", default=os.path.join(os.getcwd(), "backup"))
    args = ap.parse_args()

    out_dir = os.path.join(args.out, dt.datetime.now().strftime("%Y%m%d-%H%M%S"))
    os.makedirs(out_dir, exist_ok=True)

    with serial.Serial(args.port, 115200) as port:
        time.sleep(0.3)
        command(port, "stat")
        print(readline(port))
        photos = list_photos(port)
        print(f"{len(photos)} photo(s) on the camera")
        for index, size, synced in photos:
            data = get_photo(port, index)
            base = os.path.join(out_dir, f"{index:04d}")
            with open(base + ".pbm", "wb") as f:
                f.write(data)
            try:
                w, h, rows, meta = parse_pbm(data)
                with open(base + ".png", "wb") as f:
                    f.write(pbm_to_png(w, h, rows))
                print(f"  #{index:04d} {size} bytes {'synced' if synced else 'unsynced'} {meta or ''}")
            except ValueError as e:
                print(f"  #{index:04d} kept raw ({e})")
    print(f"saved to {out_dir}")


if __name__ == "__main__":
    main()
