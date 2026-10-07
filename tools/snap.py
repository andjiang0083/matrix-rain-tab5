#!/usr/bin/env python3
"""snap.py — capture a screenshot from Matrix Rain Clock over serial.

Firmware side (src/main.cpp, doSerialSnap):
  host opens the port at 115200 and sends 'S'
  device prints "SNAP...\r\n", renders one frame, flushes at 115200,
  switches to 921600, waits ~30 ms, prints "BMP:<size>\r\n",
  streams a <size>-byte 24-bit BMP, prints "\nEND\r\n",
  switches back to 115200.

The line is silent between the flush and "BMP:" — so this host switches
its baud right after consuming "SNAP...", then waits for the header. That
ordering is what makes the capture survive the device's mid-stream switch;
the reverse order (host waiting) loses bytes and corrupts the file.

Usage:
  python3 tools/snap.py [port] [-o out.bmp] [--timeout SECONDS]

Port auto-detects /dev/cu.usbmodem* (macOS) or /dev/ttyACM* (Linux).
Keep sessions short: USB-CDC competes with the DSI scan-out for PSRAM
bandwidth on this board (BUILDING.md §4). Grab the frame, unplug.
"""
import argparse
import glob
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial required:  python3 -m pip install pyserial")

BAUD_LOW = 115200
BAUD_HIGH = 921600


def detect_port():
    pats = ["/dev/cu.usbmodem*", "/dev/ttyACM*"]
    for pat in pats:
        ports = sorted(glob.glob(pat))
        if len(ports) == 1:
            return ports[0]
        if len(ports) > 1:
            sys.exit("multiple serial ports, pick one explicitly: %s" % ", ".join(ports))
    sys.exit("no /dev/cu.usbmodem* or /dev/ttyACM* found — plug in the Tab5")


def read_until(sp, marker, timeout):
    """Read until `marker` appears in the stream; return it (with prefix)."""
    buf = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        n = sp.in_waiting
        if n:
            buf += sp.read(n)
            i = buf.find(marker)
            if i >= 0:
                rest = buf[i + len(marker):]
                return buf[:i], rest
        else:
            time.sleep(0.01)
    raise TimeoutError("no %r within %.0fs — is the firmware running at %d baud?"
                       % (marker, timeout, sp.baudrate))


def main():
    ap = argparse.ArgumentParser(description="Capture a screenshot from Matrix Rain Clock over serial")
    ap.add_argument("port", nargs="?", help="serial port (default: auto-detect)")
    ap.add_argument("-o", "--out", default=None, help="output .bmp path")
    ap.add_argument("--timeout", type=float, default=90.0,
                    help="seconds to wait for the frame (default 90)")
    args = ap.parse_args()

    port = args.port or detect_port()
    out_path = args.out or time.strftime("matrix-rain-%Y%m%d-%H%M%S.bmp")

    sp = serial.Serial(port, BAUD_LOW, timeout=0.05)
    try:
        time.sleep(0.2)                      # let the port settle after open (DTR reset pulse)
        sp.reset_input_buffer()

        # 1. ask for a frame at the console baud
        sp.write(b"S")
        sp.flush()

        # 2. consume "SNAP..." at 115200 — the device renders silently after it
        pre, rest = read_until(sp, b"SNAP", args.timeout)
        if b"NO_FB" in pre:
            sys.exit("device reported NO_FB — PSRAM sprite allocation failed (BUILDING.md §8)")

        # 3. the line is idle until "BMP:" at 921600; switch now
        try:
            sp.baudrate = BAUD_HIGH
        except Exception as e:  # some hosts (pty, odd drivers) lack high bauds
            print("note: could not switch to %d (%s); staying at %d" % (BAUD_HIGH, e, BAUD_LOW))
        sp.reset_input_buffer()             # drop any switch-boundary garbage

        hdr_line, rest = read_until(sp, b"\n", args.timeout)
        if not hdr_line.startswith(b"BMP:"):
            sys.exit("expected 'BMP:<size>', got %r" % hdr_line[:40])
        size = int(hdr_line.split(b":")[1])

        # 4. stream the pixels (≈2.7 MB at ~90 KB/s)
        print("capturing %d bytes at %d baud…" % (size, sp.baudrate), flush=True)
        data = rest
        deadline = time.time() + args.timeout
        while len(data) < size and time.time() < deadline:
            n = sp.in_waiting
            data += sp.read(n if n else 1)
        if len(data) < size:
            sys.exit("truncated: %d/%d bytes — raise --timeout" % (len(data), size))

        tail, _ = read_until(sp, b"END", args.timeout)
        if data[:2] != b"BM":
            sys.exit("not a BMP (magic %r) — corrupt capture, retry" % data[:2])

        with open(out_path, "wb") as f:
            f.write(data)
        print("%s  %d bytes  (END ok)" % (out_path, len(data)))
    finally:
        # leave the console usable at the normal baud
        try:
            sp.baudrate = BAUD_LOW
        except Exception:
            pass
        sp.close()


if __name__ == "__main__":
    main()
