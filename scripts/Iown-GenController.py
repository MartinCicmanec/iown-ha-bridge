#!/usr/bin/env python3
"""
Generate the LoRa32's own 1W controller identity into include/iown_key.h.

Writes a random base address and a random 16-byte secret as IOWN_CTRL_ADDR and
IOWN_CTRL_SECRET, inside a marked block that scripts/Iown-GenKeyHeader.py keeps.
Like a Situo (see captures/README.md), blind i uses address base + i, and its key
is derived from the one secret:

  key(addr) = AES(transfer_key, IV(addr)) XOR secret   (upstream encrypt_1W_key)

so every 0x30 "send 1W key" frame carries the secret itself.

The secret is never printed. Back up the header: losing the identity means
re-pairing every blind. An existing identity is only replaced with --force; the
previous header is then copied to ~/.config/iown/ first.

Usage: Iown-GenController.py [header] [--force]   (header defaults to include/iown_key.h)
"""

import argparse
import os
import re
import secrets
import shutil
import sys
import time
from pathlib import Path

BEGIN = "// BEGIN controller identity"
END = "// END controller identity"
SITUO_PREFIX = bytes.fromhex("B51A") # avoid the Situo's address range
MAX_BLINDS = 16                      # room for base + 0 .. base + 15


def c_array(b):
  return "{ " + ", ".join(f"0x{x:02X}" for x in b) + " }"


def random_address():
  while True:
    a = secrets.token_bytes(3)
    n = int.from_bytes(a, "big")
    # non-zero first byte: upstream treats addresses below 0x010000 specially (docs/linklayer.md)
    if a[0] in (0x00, 0xFF) or a[:2] == SITUO_PREFIX or n + MAX_BLINDS > 0xFFFFFF:
      continue
    return a


def main():
  ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  ap.add_argument("header", nargs="?", default="include/iown_key.h")
  ap.add_argument("--force", action="store_true", help="replace an existing controller identity")
  a = ap.parse_args()

  path = Path(a.header)
  if not path.exists():
    sys.exit(f"{path} missing: generate it with scripts/Iown-GenKeyHeader.py first")
  text = path.read_text()

  if BEGIN in text:
    if not a.force:
      sys.exit(f"{path} already has a controller identity; paired blinds depend on it. Use --force to replace it.")
    backup_dir = Path.home() / ".config" / "iown"
    backup_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
    backup = backup_dir / f"{path.name}.{time.strftime('%Y%m%d-%H%M%S')}.bak"
    shutil.copy2(path, backup)
    os.chmod(backup, 0o600)
    print(f"backed up {path} to {backup}")
    text = re.sub(re.escape(BEGIN) + r".*?" + re.escape(END) + r"\n?", "", text, flags=re.S)

  addr = random_address()
  secret = secrets.token_bytes(16)
  block = f"""{BEGIN} (scripts/Iown-GenController.py) - back this up!
// The LoRa32's own 1W controller. Blind i uses address IOWN_CTRL_ADDR + i; its key is
// AES(transfer_key, IV(address)) XOR IOWN_CTRL_SECRET. Losing this means re-pairing every blind.
#define IOWN_CTRL_ADDR   {c_array(addr)} // 3 bytes, frame order (base address)
#define IOWN_CTRL_SECRET {c_array(secret)} // 16 bytes
{END}
"""
  path.write_text(text.rstrip("\n") + "\n\n" + block)
  os.chmod(path, 0o600)
  print(f"wrote controller identity to {path}: base address {addr.hex().upper()} (secret not shown)")


if __name__ == "__main__":
  main()
