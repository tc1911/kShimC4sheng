#!/usr/bin/env python3
"""Windows-friendly wrapper around packer/make_boot.py.

Adds project defaults so a user only needs to supply the stock boot image and
a distro boot image (or drag both onto 一键构建.bat).
"""

import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAKE = os.path.join(ROOT, "packer", "make_boot.py")


def has(flag):
    return flag in sys.argv[1:]


def main():
    args = list(sys.argv[1:])
    if not has("--stock-boot") or not has("--distro-boot"):
        print("usage: windows\\build.py --stock-boot <boot_a.img> "
              "--distro-boot <linux-boot.img> [options]", file=sys.stderr)
        print("       (or run 一键构建.bat)", file=sys.stderr)
        return 2
    if not has("--runtime"):
        args += ["--runtime", os.path.join(ROOT, "prebuilt", "sheng-runtime.bin")]
    if not has("--packer"):
        args += ["--packer", os.path.join(ROOT, "prebuilt", "windows",
                                          "MultiBootKernelPatcher.exe")]
    if not has("--out"):
        args += ["--out", os.path.join(ROOT, "out", "boot_a_win.img")]
    if not has("--bootargs"):
        args += ["--bootargs",
                 "root=PARTLABEL=linux rw rootwait udmabuf.size_limit_mb=256"]
    packer = args[args.index("--packer") + 1]
    if not os.path.isfile(packer):
        print("missing packer: %s" % packer, file=sys.stderr)
        return 1
    cmd = [sys.executable, MAKE] + args
    print(" ".join('"%s"' % a if " " in a else a for a in cmd))
    return subprocess.call(cmd, cwd=ROOT)


if __name__ == "__main__":
    sys.exit(main())
