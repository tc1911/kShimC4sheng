#!/usr/bin/env python3
"""kShimC kernel manifest parser (mirrors vendor/packer/inc/manifest.h)."""

import struct

MAGIC = 0x464D504B
ARM64_MAGIC = b"ARM\x64"
HEADER_SIZE = 60
ENTRY_SIZE = 56

TYPES = {1: "Linux", 2: "FreeExec", 3: "Shim", 4: "Blob", 5: "DTB", 6: "Manifest"}
FLAGS = {1: "BASE_IMAGE", 2: "COPY", 4: "EXECUTABLE", 8: "HAS_DTB", 16: "QC_ABOOT"}


class ManifestError(Exception):
    pass


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def u64(b, o):
    return struct.unpack_from("<Q", b, o)[0]


def entry_flags(value):
    return [name for bit, name in FLAGS.items() if value & bit]


def parse(raw):
    if raw[0x38:0x3C] != ARM64_MAGIC:
        raise ManifestError("packed blob has no ARM64 magic at 0x38")
    image_size = u64(raw, 16)
    manifest_off = u64(raw, 32)
    manifest_size = u64(raw, 40)
    shim_off = u64(raw, 48)
    if manifest_off + manifest_size > len(raw):
        raise ManifestError("manifest outside packed blob")
    if manifest_off < HEADER_SIZE or manifest_off + manifest_size < HEADER_SIZE:
        raise ManifestError("bad manifest offset/size")

    m = raw[manifest_off:manifest_off + manifest_size]
    magic = u32(m, 0)
    if magic != MAGIC:
        raise ManifestError("bad manifest magic 0x%08x" % magic)
    major, minor = struct.unpack_from("<HH", m, 4)
    count = u32(m, 8)
    default = u32(m, 12)
    timeout = u32(m, 16)
    base = {
        "instructions": u64(m, 20),
        "image_size": u64(m, 28),
        "manifest_offset": u64(m, 36),
        "manifest_size": u64(m, 44),
        "shim_offset": u64(m, 52),
    }
    if count > 50:
        raise ManifestError("entry count %d too large" % count)

    entries = []
    pos = HEADER_SIZE
    for _ in range(count):
        if pos + ENTRY_SIZE > len(m):
            raise ManifestError("truncated manifest entry")
        name_len = u32(m, pos + 52)
        name = m[pos + 56:pos + 56 + name_len].split(b"\x00", 1)[0].decode("utf-8", "replace")
        entries.append({
            "offset": u64(m, pos + 0),
            "size": u64(m, pos + 8),
            "type": u32(m, pos + 16),
            "type_name": TYPES.get(u32(m, pos + 16), "?"),
            "flags": u32(m, pos + 20),
            "flag_names": entry_flags(u32(m, pos + 20)),
            "load_address": u64(m, pos + 24),
            "entry_offset": u32(m, pos + 32),
            "dtb_entry": u32(m, pos + 36),
            "alignment": u32(m, pos + 40),
            "copy_size_max": u64(m, pos + 44),
            "name": name,
        })
        pos += ENTRY_SIZE + name_len
        pos += (-pos) % 4
    return {
        "image_size": image_size,
        "manifest_offset": manifest_off,
        "manifest_size": manifest_size,
        "shim_offset": shim_off,
        "version": "%d.%d" % (major, minor),
        "default_entry": default,
        "timeout_ms": timeout,
        "base_image": base,
        "entries": entries,
    }
