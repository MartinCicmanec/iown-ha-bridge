#!/usr/bin/env python3
"""
Decode io-homecontrol frames from a sniffer log or a frames file.

Accepts lines containing "frame=<hex>" (sniffer output, see src/sniffer.cpp) or
lines that are a bare hex frame (captures/*_frames.txt). For each frame it
prints the header fields, the command, and whether the CRC (and optionally the
1W MAC) is valid. Field layout and command names follow upstream
iown-homecontrol docs/linklayer.md and docs/commands.md.

Usage: Iown-Decode.py <file> [--unique] [--key-header include/iown_key.h] [--frames-out out.txt]

  --unique        print each distinct frame once (repeats of a press are identical)
  --key-header    verify 1W MACs: the AES key is derived per source address from
                  IOWN_PRODUCT_KEY in the header (see scripts/Iown-GenKeyHeader.py)
  --frames-out    write the distinct, CRC-valid frames (one hex per line) to a file,
                  e.g. for scripts/Iown-VerifyQRKey.py
"""

import argparse
import re
import sys

from upstream import ioCrypto

COMMANDS = {
  0x00: "execute function",
  0x01: "activate mode",
  0x20: "private (manufacturer)",
  0x28: "discover",
  0x2E: "unknown (seen in discover mode)",
  0x30: "send 1W key",
  0x39: "remove 1W controller",
}

# main parameter of command 0x00 (docs/commands.md "Main Parameter")
def main_param(mp):
  if mp <= 0xC800:
    return f"{mp / 0xC800 * 100:.0f}% (0=open/up, 100=closed/down)"
  if mp == 0xD200:
    return "current (stop / My)"
  return f"0x{mp:04X}"


def parse_lines(path):
  for line in open(path):
    m = re.search(r"frame=([0-9A-Fa-f]+)", line)
    hexstr = m.group(1) if m else line.strip()
    if hexstr and re.fullmatch(r"[0-9A-Fa-f]+", hexstr) and len(hexstr) % 2 == 0:
      rssi = re.search(r"rssi=(-?[0-9.]+)", line)
      yield bytes.fromhex(hexstr), (rssi.group(1) if rssi else None)


def trim(frame):
  """Cut the frame to the length in control byte 0 (+1 ctrl0, +2 CRC); the sniffer may append junk."""
  n = (frame[0] & 0x1F) + 3
  return frame[:n] if len(frame) >= n else None


def read_product_key(path):
  s = open(path).read()
  m = re.search(r"#define\s+IOWN_PRODUCT_KEY\s*\{([^}]*)\}", s)
  if not m:
    sys.exit(f"{path}: IOWN_PRODUCT_KEY not found")
  return bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1)))


def decode(frame, product_key):
  c0, c1 = frame[0], frame[1]
  oneway = bool(c0 & 0x20)
  dest, src = frame[2:5], frame[5:8]
  crc_ok = ioCrypto.compute_crc_8408(frame) == 0
  out = [
    f"{'1W' if oneway else '2W'} ctrl0={c0:02X} (len {c0 & 0x1F}) ctrl1={c1:02X}",
    f"dest={dest.hex().upper()} src={src.hex().upper()}",
  ]
  if oneway:
    data, counter, mac = frame[8:-10], frame[-10:-8], frame[-8:-2]
  else:
    data, counter, mac = frame[8:-2], None, None
  cmd = data[0] if data else None
  out.append(f"cmd={cmd:02X} ({COMMANDS.get(cmd, '?')}) params={data[1:].hex().upper()}" if data else "cmd=-")
  if cmd == 0x00 and len(data) >= 5:
    out.append(f"position={main_param(int.from_bytes(data[3:5], 'big'))}")
  if counter is not None:
    out.append(f"counter={counter.hex().upper()} mac={mac.hex().upper()}")
  out.append(f"crc={'ok' if crc_ok else 'BAD'}")
  if oneway and product_key and crc_ok:
    key = bytes(ioCrypto.encrypt_1W_key(src, product_key))
    out.append(f"mac={'ok' if ioCrypto.create_1W_hmac(counter, key, data) == mac else 'BAD'}")
  return "  ".join(out), crc_ok


def main():
  ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  ap.add_argument("file")
  ap.add_argument("--unique", action="store_true")
  ap.add_argument("--key-header")
  ap.add_argument("--frames-out")
  a = ap.parse_args()

  product_key = read_product_key(a.key_header) if a.key_header else None
  seen, valid = set(), []
  for raw, rssi in parse_lines(a.file):
    frame = trim(raw)
    if frame is None:
      continue
    if a.unique and frame in seen:
      continue
    seen.add(frame)
    text, crc_ok = decode(frame, product_key)
    if crc_ok and frame not in valid:
      valid.append(frame)
    print((f"[{rssi} dBm] " if rssi else "") + text)

  if a.frames_out:
    with open(a.frames_out, "w") as f:
      f.writelines(fr.hex().upper() + "\n" for fr in valid)
    print(f"wrote {len(valid)} CRC-valid frames to {a.frames_out}", file=sys.stderr)


if __name__ == "__main__":
  main()
