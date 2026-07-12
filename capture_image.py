#!/usr/bin/env python3
"""
Capture image from little-camera serial output.
Usage: python capture_image.py [port] [output.pgm]
Example: python capture_image.py /dev/cu.usbmodem201301 capture.pgm
"""
import sys
import serial
import re

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem201301"
output = sys.argv[2] if len(sys.argv) > 2 else "capture.pgm"

print(f"Listening on {port}... Press button to capture.")
ser = serial.Serial(port, 115200, timeout=30)

capturing = False
hex_data = ""
width = height = 0

while True:
    line = ser.readline().decode('utf-8', errors='ignore').strip()
    if not line:
        continue

    print(line)

    if "---IMAGE_START---" in line:
        capturing = True
        hex_data = ""
        print("\n>>> Capturing image data...")
        continue

    if "---IMAGE_END---" in line:
        capturing = False
        print(f">>> Got {len(hex_data)//2} bytes")

        # Convert hex to binary
        pixels = bytes.fromhex(hex_data)

        # Write PGM file
        with open(output, 'wb') as f:
            f.write(f"P5\n{width} {height}\n255\n".encode())
            f.write(pixels)

        print(f">>> Saved to {output}")
        print(f">>> View with: open {output}")
        break

    if capturing:
        if line.startswith("P5 "):
            # Parse header: P5 320 240 255
            parts = line.split()
            width, height = int(parts[1]), int(parts[2])
            print(f">>> Image size: {width}x{height}")
        else:
            hex_data += line

ser.close()
