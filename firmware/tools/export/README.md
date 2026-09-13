# Getting photos off the camera

Two tools, both read-only as far as the camera is concerned.

```
python3 -m pip install -r requirements.txt
```

## `dump_flash.py` — works with any firmware, run this first

Reads the LittleFS partition straight out of the flash with esptool and unpacks
it on the host. The camera does not need any particular firmware, and nothing is
written to it.

```
python3 dump_flash.py --port /dev/cu.usbmodem1101
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
python3 pull_serial.py --port /dev/cu.usbmodem1101
```
