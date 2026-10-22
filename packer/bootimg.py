#!/usr/bin/env python3
"""Android boot image v0-v4 parser/writer (stdlib only).

Only what kShimC4sheng needs: read the stock boot image, keep its header
geometry, replace the kernel blob with the kShimC packed blob, write a new
image with the same header version.
"""

import struct

MAGIC = b"ANDROID!"
ARM64_MAGIC = b"ARM\x64"
GZIP_MAGIC = b"\x1f\x8b"
QC_ABOOT_MAGIC = b"UNCOMPRESSED_IMG"
LZ4_MAGIC = b"\x02\x21\x4c\x18"


class BootImageError(Exception):
    pass


def u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def u64(buf, off):
    return struct.unpack_from("<Q", buf, off)[0]


def put32(buf, off, value):
    struct.pack_into("<I", buf, off, value)


def put64(buf, off, value):
    struct.pack_into("<Q", buf, off, value)


def align_up(value, alignment):
    return (value + alignment - 1) // alignment * alignment


def _cstr(buf):
    return bytes(buf).split(b"\x00", 1)[0]


def assert_raw_arm64_image(kernel, label="kernel"):
    """Reject gzip/QC-ABOOT payloads: kShimC needs a raw ARM64 Image."""
    if len(kernel) < 0x40:
        raise BootImageError("%s too small: %d" % (label, len(kernel)))
    if kernel[:2] == GZIP_MAGIC:
        raise BootImageError("%s is gzip-compressed; decompress to a raw Image" % label)
    if kernel[:4] == LZ4_MAGIC:
        raise BootImageError("%s is lz4-compressed; decompress with `lz4 -d`" % label)
    if kernel[:len(QC_ABOOT_MAGIC)] == QC_ABOOT_MAGIC:
        raise BootImageError("%s is a QC ABOOT image, not an ARM64 Image" % label)
    if kernel[0x38:0x3C] != ARM64_MAGIC:
        raise BootImageError(
            "%s has no ARM64 magic at 0x38 (got %r)" % (label, kernel[0x38:0x3C]))


def parse(data):
    if len(data) < 1632:
        raise BootImageError("boot image truncated: %d bytes" % len(data))
    if data[:8] != MAGIC:
        raise BootImageError("bad boot magic: %r" % data[:8])

    version = u32(data, 40)
    info = {"version": version, "raw": bytes(data), "size": len(data)}

    if version in (0, 1, 2):
        page = u32(data, 36)
        if page <= 0 or page > 65536 or page & (page - 1):
            raise BootImageError("bad page size: %d" % page)
        kernel_size = u32(data, 8)
        header_size = u32(data, 1644) if version >= 1 else 1632
        if version >= 1 and header_size == 0:
            header_size = 1632
        if page < header_size:
            raise BootImageError("page size %d smaller than header %d" % (page, header_size))
        kernel_off = page
        if kernel_off + kernel_size > len(data):
            raise BootImageError("kernel extends past image")
        ramdisk_size = u32(data, 16)
        ramdisk_off = align_up(kernel_off + kernel_size, page) if ramdisk_size else 0
        if ramdisk_size and ramdisk_off + ramdisk_size > len(data):
            raise BootImageError("ramdisk extends past image")
        dtb_size = u32(data, 1648) if version >= 2 else 0
        dtb_addr = u64(data, 1652) if version >= 2 else 0
        info.update({
            "page_size": page,
            "header_size": header_size,
            "kernel_size": kernel_size,
            "kernel_offset": kernel_off,
            "kernel": bytes(data[kernel_off:kernel_off + kernel_size]),
            "ramdisk_size": ramdisk_size,
            "ramdisk_offset": ramdisk_off,
            "ramdisk": bytes(data[ramdisk_off:ramdisk_off + ramdisk_size]),
            "dtb_size": dtb_size,
            "dtb_addr": dtb_addr,
            "os_version": u32(data, 44),
            "cmdline": _cstr(data[64:576]) + _cstr(data[608:1632]),
            "name": _cstr(data[48:64]),
        })
        return info

    if version in (3, 4):
        page = 4096
        kernel_size = u32(data, 8)
        header_size = u32(data, 20)
        expected = 1584 if version == 4 else 1580
        if header_size != expected:
            raise BootImageError(
                "header_size %d != %d for v%d" % (header_size, expected, version))
        kernel_off = page
        if kernel_off + kernel_size > len(data):
            raise BootImageError("kernel extends past image")
        ramdisk_size = u32(data, 12)
        ramdisk_off = align_up(kernel_off + kernel_size, page) if ramdisk_size else 0
        if ramdisk_size and ramdisk_off + ramdisk_size > len(data):
            raise BootImageError("ramdisk extends past image")
        info.update({
            "page_size": page,
            "header_size": header_size,
            "kernel_size": kernel_size,
            "kernel_offset": kernel_off,
            "kernel": bytes(data[kernel_off:kernel_off + kernel_size]),
            "ramdisk_size": ramdisk_size,
            "ramdisk_offset": ramdisk_off,
            "ramdisk": bytes(data[ramdisk_off:ramdisk_off + ramdisk_size]),
            "dtb_size": 0,
            "dtb_addr": 0,
            "os_version": u32(data, 16),
            "cmdline": _cstr(data[44:1580]),
            "name": b"",
            "signature_size": u32(data, 1580) if version == 4 else 0,
        })
        return info

    raise BootImageError("unsupported header version %d" % version)


def build(orig, kernel, pad_to=None):
    """Write a boot image with the original header geometry and a new kernel."""
    version = orig["version"]
    page = orig["page_size"]
    header = bytearray(orig["raw"][:page])

    if version in (0, 1, 2):
        if orig["ramdisk_size"]:
            raise BootImageError(
                "stock v%d boot has a %d-byte ramdisk; this tool does not carry it"
                % (version, orig["ramdisk_size"]))
        put32(header, 8, len(kernel))
        put32(header, 16, 0)          # ramdisk
        put32(header, 24, 0)          # second
        if version >= 2:
            put32(header, 1648, 0)    # dtb_size
            put64(header, 1652, 0)    # dtb_addr
    elif version in (3, 4):
        put32(header, 8, len(kernel))
        put32(header, 12, 0)          # ramdisk = 0
        put32(header, 20, 1584 if version == 4 else 1580)
        if version == 4:
            put32(header, 1580, 0)    # signature_size
    else:
        raise BootImageError("unsupported header version %d" % version)

    out = bytearray(header)
    out += kernel
    out += b"\x00" * (align_up(len(out), page) - len(out))

    if pad_to is not None:
        if len(out) > pad_to:
            raise BootImageError(
                "image %d bytes exceeds pad target %d" % (len(out), pad_to))
        out += b"\x00" * (pad_to - len(out))

    return bytes(out)
