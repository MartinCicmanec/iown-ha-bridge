#!/usr/bin/env python3
"""
Record sniffer output (env ttgo-lora32-v21-sniffer) from the serial port to a log file.

Every received packet line ("RX rssi=... raw=... frame=...") is echoed to the
terminal and appended to the log. Stop with Ctrl+C. Decode the log afterwards
with scripts/Iown-Decode.py.

Usage: Iown-Capture.py <log file> [--port /dev/ttyACM0] [--baud 115200]

Needs pyserial (pip install pyserial; already present in a PlatformIO venv).
"""

import argparse
import sys

try:
  import serial
except ImportError:
  sys.exit("pyserial missing: pip install pyserial")


def main():
  ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  ap.add_argument("log")
  ap.add_argument("--port", default="/dev/ttyACM0")
  ap.add_argument("--baud", type=int, default=115200)
  a = ap.parse_args()

  count = 0
  with serial.Serial(a.port, a.baud, timeout=1) as port, open(a.log, "a") as log:
    print(f"capturing from {a.port} to {a.log}, Ctrl+C to stop", file=sys.stderr)
    try:
      while True:
        line = port.readline().decode(errors="replace").strip()
        if not line:
          continue
        print(line)
        if line.startswith(("RX ", "[SNIFFER]")):
          log.write(line + "\n")
          log.flush()
          count += line.startswith("RX ")
    except KeyboardInterrupt:
      pass
  print(f"\n{count} packets written to {a.log}", file=sys.stderr)


if __name__ == "__main__":
  main()
