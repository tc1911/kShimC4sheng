import gzip
import os
import struct
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys_path = os.path.join(ROOT, "packer")


def make_image(size=8192, text_offset=0):
    k = bytearray(size)
    struct.pack_into("<I", k, 0, 0x14000040)   # b +0x100
    struct.pack_into("<I", k, 4, 0xD503201F)   # nop
    struct.pack_into("<Q", k, 8, text_offset)
    struct.pack_into("<Q", k, 16, size)
    struct.pack_into("<Q", k, 24, 0)
    struct.pack_into("<Q", k, 32, 0)
    struct.pack_into("<Q", k, 40, 0)
    struct.pack_into("<Q", k, 48, 0)
    struct.pack_into("<I", k, 56, 0x644D5241)
    return bytes(k)


def make_v4_boot(kernel, cmdline=b"androidboot.hardware=qcom"):
    h = bytearray(4096)
    h[0:8] = b"ANDROID!"
    struct.pack_into("<I", h, 8, len(kernel))
    struct.pack_into("<I", h, 12, 0)
    struct.pack_into("<I", h, 16, 0)
    struct.pack_into("<I", h, 20, 1584)
    struct.pack_into("<I", h, 40, 4)
    h[44:44 + len(cmdline)] = cmdline
    struct.pack_into("<I", h, 1580, 0)
    return bytes(h) + kernel


def make_v0_boot(kernel, ramdisk=b"", page=4096):
    h = bytearray(page)
    h[0:8] = b"ANDROID!"
    struct.pack_into("<I", h, 8, len(kernel))
    struct.pack_into("<I", h, 16, len(ramdisk))
    struct.pack_into("<I", h, 36, page)
    struct.pack_into("<I", h, 40, 0)
    struct.pack_into("<I", h, 44, 0)
    out = bytes(h) + kernel
    out += b"\0" * ((-len(out)) % page)
    out += ramdisk
    return out


def make_dtb(path, compatible="xiaomi,sheng", extra=""):
    dts = path + ".dts"
    with open(dts, "w") as f:
        f.write("/dts-v1/;\n/ {\n compatible = \"%s\", \"qcom,sm8550\";\n"
                " #address-cells = <2>;\n #size-cells = <2>;\n%s};\n"
                % (compatible, extra))
    subprocess.run(["dtc", "-I", "dts", "-O", "dtb", "-o", path, dts],
                   check=True, capture_output=True)
    return path


def gzip_bytes(data):
    return gzip.compress(data)
