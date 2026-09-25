#!/usr/bin/env python3
"""Read raw serial output from the board without needing an interactive TTY.

`pio device monitor` refuses to run with piped/redirected stdin, which is
what any non-interactive agent invocation looks like. Use this instead.

By default this also hard-resets the board before reading (same DTR/RTS
dance esptool uses), since opening the port after the board already booted
and finished printing means the output is just gone. Pass --no-reset to
skip that if you want to observe the board in whatever state it's in.

Usage: serial_read.py [seconds] [port] [baud] [--no-reset]
"""
import sys
import time
import serial

args = [a for a in sys.argv[1:] if not a.startswith("--")]
reset = "--no-reset" not in sys.argv[1:]

seconds = float(args[0]) if len(args) > 0 else 3.0
port = args[1] if len(args) > 1 else "/dev/ttyUSB0"
baud = int(args[2]) if len(args) > 2 else 115200

ser = serial.Serial(port, baud, timeout=1)

if reset:
    # Pulse EN (RTS) only; leave DTR (GPIO0) alone so the board boots the
    # flashed app normally instead of dropping into the download bootloader.
    ser.dtr = False
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False

end = time.time() + seconds
buf = b""
while time.time() < end:
    buf += ser.read(1024)
ser.close()
sys.stdout.write(buf.decode(errors="replace"))
