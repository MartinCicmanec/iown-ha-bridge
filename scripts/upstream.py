"""
Load the upstream iown-homecontrol Python modules from the git submodule.

Upstream ships them with hyphenated file names (Iown-ioCrypto.py, Iown-AES.py),
which cannot be imported directly. Usage: `from upstream import ioCrypto`.
"""

import importlib.util
import sys
from pathlib import Path

UPSTREAM_SCRIPTS = Path(__file__).resolve().parent.parent / "extern" / "iown-homecontrol" / "scripts"


def _load(name, filename):
  path = UPSTREAM_SCRIPTS / filename
  if not path.exists():
    sys.exit(f"{path} missing: run `git submodule update --init`")
  spec = importlib.util.spec_from_file_location(name, path)
  module = importlib.util.module_from_spec(spec)
  sys.modules[name] = module
  spec.loader.exec_module(module)
  return module


aes = _load("aes", "Iown-AES.py") # imported by Iown-ioCrypto.py as `aes`
ioCrypto = _load("ioCrypto", "Iown-ioCrypto.py")
