# Getting photos off the camera

Two tools, both read-only as far as the camera is concerned.

```
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
```

Then run the tools with `.venv/bin/python`. (A venv rather than a plain
`pip install`: on a Homebrew Python that is refused, and it keeps the script's
interpreter and its packages together.) PlatformIO's own Python works too:
`~/.platformio/penv/bin/pip install littlefs-python pyserial`, then run with
`~/.platformio/penv/bin/python`.

## `dump_flash.py` — works with any firmware, run this first

Reads the LittleFS partition straight out of the flash with esptool and unpacks
it on the host. The camera does not need any particular firmware, and nothing is
written to it.

```
.venv/bin/python dump_flash.py --port /dev/cu.usbmodem1101
# → backup/<timestamp>/NNNN.pbm + NNNN.png + littlefs.bin (raw image, keep it)
```

If esptool cannot get the board into the bootloader, hold BOOT while plugging in
the cable and run it again. The board resets afterwards.

## `pull_serial.py` — over the USB console, needs firmware with the console

Talks to the serial console (`ls`, `get NNNN`, see `docs/protocol.md` §6) and
saves the same .pbm/.png pairs. Convenient once the new firmware is on, no
bootloader dance. The camera sleeps after 10 s idle; press the shutter to wake
it just before running this.

```
.venv/bin/python pull_serial.py --port /dev/cu.usbmodem1101
```
