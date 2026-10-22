#!/usr/bin/env python3
"""QEMU-only acceptance: genuine Linux consumes an external cpio initramfs.

Builds nothing except two tiny freestanding ARM64 /init fixtures. Drives the
existing packer over the shared fixtures, checks the produced manifest ABI,
boots the packed image under qemu-system-aarch64 and requires a real Linux
kernel to run the selected external initramfs through the runtime handoff.

Runs on the Linux remote Docker (qemu-system-aarch64, aarch64-linux-gnu-gcc,
cpio, gzip, fdtput, fdtget). No hardware, no publish, no cleanup of shared
processes. Scope is QEMU only.
"""
import argparse
import hashlib
import json
import os
import shutil
import stat
import struct
import subprocess
from pathlib import Path

from qemu_menu import run_with_label

# Manifest ABI constants (src/include/manifest.h).
MANIFEST_MAGIC = 0x464D504B
MF_LINUX, MF_FREE_EXEC, MF_SHIM, MF_BLOB, MF_DTB, MF_MANIFEST = 1, 2, 3, 4, 5, 6
FLAG_BASE_IMAGE = 1 << 0
FLAG_HAS_DTB = 1 << 3

# Fixture markers and bootargs. The per-archive markers are unique and are the
# only proof that Linux actually executed the fixture /init.
MARKER_A = "KSHIM_EXTERNAL_INITRD_A_PASS"
MARKER_B = "KSHIM_EXTERNAL_INITRD_B_PASS"
BASE_MARKER = "PAYLOAD_A_PASS"
BOOTARGS = "console=ttyAMA0 earlycon rdinit=/init panic=-1 loglevel=6"

# The exact pack section order. Manifest reference indices are derived from it.
PACK_ORDER = [
    "Image-Base",
    "Image-Linux-A",
    "Image-Linux-B",
    "Image-DT-A",
    "Image-DT-B",
    "Image-Blob-A",
    "Image-Blob-B",
]
BLOB_INDEX = {name: i for i, name in enumerate(PACK_ORDER)}

QEMU_TIMEOUT = 90


def run(cmd, log=None, timeout=None, check=False, cwd=None):
    cmd = [str(c) for c in cmd]
    print("+", " ".join(cmd), flush=True)
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=timeout, cwd=cwd)
    except subprocess.TimeoutExpired as e:
        if log:
            Path(log).write_bytes(e.stdout or b"")
        raise
    data = p.stdout or b""
    if log:
        Path(log).write_bytes(data)
    if check and p.returncode != 0:
        raise SystemExit("command failed (%d): %s" % (p.returncode, " ".join(cmd)))
    return p.returncode, data


def qemu_run(cmd, log, input_bytes, selection_label=None):
    """Run QEMU with pipes and an explicit timeout. Timeout => logged FAIL."""
    cmd = [str(c) for c in cmd]
    print("+", " ".join(cmd), flush=True)
    if selection_label is not None:
        try:
            p = run_with_label(cmd, selection_label, QEMU_TIMEOUT)
            data = p.stdout or b""
            Path(log).write_bytes(data)
            return p.returncode, data.decode("utf-8", "replace"), False, None
        except subprocess.TimeoutExpired as e:
            data = e.stdout or b""
            Path(log).write_bytes(data)
            return None, data.decode("utf-8", "replace"), True, "TimeoutExpired"
        except OSError as e:
            Path(log).write_text("spawn error: %s\n" % e)
            return None, "", False, "spawn error: %s" % e
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           input=input_bytes, timeout=QEMU_TIMEOUT)
        data = p.stdout or b""
        Path(log).write_bytes(data)
        return p.returncode, data.decode("utf-8", "replace"), False, None
    except subprocess.TimeoutExpired as e:
        data = e.stdout or b""
        Path(log).write_bytes(data)
        return None, data.decode("utf-8", "replace"), True, "TimeoutExpired"
    except OSError as e:
        Path(log).write_text("spawn error: %s\n" % e)
        return None, "", False, "spawn error: %s" % e


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def build_init(src, elf, marker, log):
    run(["aarch64-linux-gnu-gcc", "-nostdlib", "-static", "-ffreestanding",
         "-fno-stack-protector", "-fno-pie", "-no-pie",
         "-Wl,--build-id=none", "-Wl,-e,_start",
         '-DEXTERNAL_INITRD_MARKER="%s"' % marker,
         str(src), "-o", str(elf)], log=log, check=True)
    return elf.read_bytes()


def make_archive(stage, init_bytes, cpio_out, gz_out, log):
    if stage.exists():
        shutil.rmtree(stage)
    (stage / "dev").mkdir(parents=True)
    (stage / "init").write_bytes(init_bytes)
    os.chmod(str(stage / "init"), 0o755)
    node = stage / "dev" / "console"
    if node.exists():
        node.unlink()
    os.mknod(str(node), stat.S_IFCHR | 0o600, os.makedev(5, 1))

    names = b".\ndev\ndev/console\ninit\n"
    with open(cpio_out, "wb") as out, open(log, "wb") as err:
        subprocess.run(["cpio", "--quiet", "-o", "-H", "newc", "--owner=0:0"],
                       cwd=str(stage), input=names, stdout=out, stderr=err,
                       check=True)
    with open(gz_out, "wb") as out, open(log, "ab") as err:
        subprocess.run(["gzip", "-n", "-c", str(cpio_out)],
                       stdout=out, stderr=err, check=True)


def write_pack_config(path, runtime, base, padded, dt_a, dt_b, blobs, out_bin):
    path.write_text("\n".join([
        "[Pack]",
        "Shim=" + str(runtime),
        "Default=Image-Linux-A",
        "Timeout=1500",
        "Output=" + str(out_bin),
        "[Image-Base]",
        "Name=Base",
        "Path=" + str(base),
        "Type=Linux",
        "BaseImage=true",
        "[Image-Linux-A]",
        "Name=Linux A",
        "Path=" + str(padded),
        "Type=Linux",
        "BaseImage=false",
        "Align=21",
        "DeviceTreeBlob=Image-DT-A",
        "[Image-Linux-B]",
        "Name=Linux B",
        "PayloadFrom=Image-Linux-A",
        "Type=Linux",
        "BaseImage=false",
        "DeviceTreeBlob=Image-DT-B",
        "[Image-DT-A]",
        "Name=DT A",
        "Path=" + str(dt_a),
        "Type=DTB",
        "BaseImage=false",
        "[Image-DT-B]",
        "Name=DT B",
        "Path=" + str(dt_b),
        "Type=DTB",
        "BaseImage=false",
        "[Image-Blob-A]",
        "Name=Blob A",
        "Path=" + str(blobs[0]),
        "Type=Blob",
        "BaseImage=false",
        "Align=16",
        "[Image-Blob-B]",
        "Name=Blob B",
        "Path=" + str(blobs[1]),
        "Type=Blob",
        "BaseImage=false",
        "Align=16",
        "[Manifest]",
        "Type=Manifest",
        "",
    ]))


def parse_manifest(data):
    manifest_offset = struct.unpack_from("<Q", data, 0x20)[0]
    magic = struct.unpack_from("<I", data, manifest_offset)[0]
    entry_count = struct.unpack_from("<I", data, manifest_offset + 8)[0]
    cursor = manifest_offset + 60
    entries = []
    for _ in range(entry_count):
        offset, size, etype, flags = struct.unpack_from("<QQII", data, cursor)
        dtref = struct.unpack_from("<I", data, cursor + 36)[0]
        name_len = struct.unpack_from("<I", data, cursor + 52)[0]
        name = data[cursor + 56:cursor + 56 + name_len].decode("utf-8", "replace")
        entries.append({"offset": offset, "size": size, "type": etype,
                        "flags": flags, "dtref": dtref, "name": name})
        cursor += (56 + name_len + 3) & ~3
    return magic, entries


def qemu_base():
    return ["qemu-system-aarch64", "-machine", "virt,gic-version=3",
            "-accel", "tcg", "-cpu", "cortex-a76", "-smp", "1", "-m", "1024M",
            "-display", "none", "-monitor", "none", "-serial", "stdio",
            "-no-reboot"]


def main():
    ap = argparse.ArgumentParser(description="External initramfs QEMU acceptance")
    ap.add_argument("--runtime", required=True, type=Path)
    ap.add_argument("--kernel", required=True, type=Path)
    ap.add_argument("--packer", required=True, type=Path)
    ap.add_argument("--base", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()

    src_c = Path(__file__).resolve().parent / "external_initrd_init.c"
    runtime = args.runtime.resolve()
    kernel = args.kernel.resolve()
    packer = args.packer.resolve()
    base = args.base.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)

    for p in (runtime, kernel, packer, base, src_c):
        if not p.is_file():
            raise SystemExit("missing input: %s" % p)
    if runtime.stat().st_size != 4 * 1024 * 1024:
        raise SystemExit("runtime must be exactly 4 MiB")

    # ARM64 Image header: magic at 56, little-endian image_size at 16.
    with open(kernel, "rb") as f:
        head = f.read(64)
    if len(head) < 64 or head[56:60] != b"ARM\x64":
        raise SystemExit("kernel is not an ARM64 Image (bad magic)")
    image_size = struct.unpack_from("<Q", head, 16)[0]
    if image_size == 0:
        raise SystemExit("kernel Image header reports zero size")

    kernel_bytes = kernel.read_bytes()
    target = max(image_size, len(kernel_bytes))
    padded = out / "padded-Image"
    padded.write_bytes(kernel_bytes + b"\x00" * (target - len(kernel_bytes)))

    # Standalone freestanding /init fixtures, one marker per archive.
    init_a = build_init(src_c, out / "init-a.elf", MARKER_A, out / "init-a-build.log")
    init_b = build_init(src_c, out / "init-b.elf", MARKER_B, out / "init-b-build.log")
    archive_a = out / "blob-a.cpio.gz"
    archive_b = out / "blob-b.cpio.gz"
    make_archive(out / "stage-a", init_a, out / "blob-a.cpio", archive_a,
                 out / "blob-a-cpio.log")
    make_archive(out / "stage-b", init_b, out / "blob-b.cpio", archive_b,
                 out / "blob-b-cpio.log")

    # Baseline DTB from QEMU, same CPU/RAM as the runs.
    qemu_dtb = out / "qemu.dtb"
    run(["qemu-system-aarch64", "-machine",
         "virt,gic-version=3,dumpdtb=" + str(qemu_dtb),
         "-cpu", "cortex-a76", "-smp", "1", "-m", "1024M",
         "-display", "none", "-monitor", "none", "-serial", "none"],
        log=out / "qemu-dumpdtb.log", timeout=30, check=True)
    _rc, reg = run(["fdtget", "-t", "x", str(qemu_dtb), "/memory@40000000", "reg"])
    if reg.decode().strip() != "0 40000000 0 40000000":
        raise SystemExit("unexpected QEMU root RAM node: %r" % reg.decode().strip())

    # DT-A/DT-B: drop the RAM node so the runtime handoff backfills it, and
    # point chosen at the matching blob manifest index.
    dt_a = out / "dt-a.dtb"
    dt_b = out / "dt-b.dtb"
    blob_a_index = BLOB_INDEX["Image-Blob-A"]
    blob_b_index = BLOB_INDEX["Image-Blob-B"]
    for dt, idx in ((dt_a, blob_a_index), (dt_b, blob_b_index)):
        shutil.copyfile(qemu_dtb, dt)
        run(["fdtput", "-r", str(dt), "/memory@40000000"], check=True)
        run(["fdtput", "-t", "s", str(dt), "/chosen", "bootargs", BOOTARGS], check=True)
        run(["fdtput", str(dt), "/chosen", "kshim,inherit-runtime-memory"], check=True)
        run(["fdtput", "-t", "x", str(dt), "/chosen", "kshim,initrd-entry",
             "%x" % idx], check=True)

    # Structural-only difference: a valid DT-B with an out-of-range blob ref.
    dt_b_bad = out / "dt-b-bad.dtb"
    shutil.copyfile(dt_b, dt_b_bad)
    run(["fdtput", "-t", "x", str(dt_b_bad), "/chosen", "kshim,initrd-entry",
         "ffffffff"], check=True)

    main_bin = (out / "external-initrd.bin").resolve()
    bad_bin = (out / "external-initrd-badref.bin").resolve()
    cfg = out / "pack.cfg"
    bad_cfg = out / "pack-badref.cfg"
    write_pack_config(cfg, runtime, base, padded, dt_a, dt_b,
                      (archive_a, archive_b), main_bin)
    write_pack_config(bad_cfg, runtime, base, padded, dt_a, dt_b_bad,
                      (archive_a, archive_b), bad_bin)
    run([str(packer), str(cfg)], log=out / "pack.log", check=True, cwd=str(out))
    run([str(packer), str(bad_cfg)], log=out / "pack-badref.log", check=True,
        cwd=str(out))
    if not main_bin.is_file() or not bad_bin.is_file():
        raise SystemExit("packer did not produce the expected images")

    archive_a_bytes = archive_a.read_bytes()
    archive_b_bytes = archive_b.read_bytes()
    archive_a_sha = sha256(archive_a)
    archive_b_sha = sha256(archive_b)
    if archive_a_sha == archive_b_sha:
        raise SystemExit("archive A and B must have distinct hashes")

    # Narrow ABI check on the controlled fixture.
    image_bytes = main_bin.read_bytes()
    magic, entries = parse_manifest(image_bytes)
    if magic != MANIFEST_MAGIC:
        raise SystemExit("bad manifest magic: %#x" % magic)
    if len(entries) < len(PACK_ORDER):
        raise SystemExit("manifest has %d entries, expected >= %d"
                         % (len(entries), len(PACK_ORDER)))
    expected_types = [MF_LINUX, MF_LINUX, MF_LINUX, MF_DTB, MF_DTB, MF_BLOB, MF_BLOB]
    got_types = [e["type"] for e in entries[:len(PACK_ORDER)]]
    if got_types != expected_types:
        raise SystemExit("manifest type order mismatch: %r" % got_types)
    for extra in entries[len(PACK_ORDER):]:
        if extra["type"] not in (MF_SHIM, MF_MANIFEST):
            raise SystemExit("unexpected trailing manifest entry type %d"
                             % extra["type"])
    if not (entries[0]["flags"] & FLAG_BASE_IMAGE):
        raise SystemExit("base entry is not flagged BaseImage")
    if entries[1]["flags"] & FLAG_BASE_IMAGE or entries[2]["flags"] & FLAG_BASE_IMAGE:
        raise SystemExit("Linux-A/B must not be BaseImage")
    if entries[1]["dtref"] != 3 or entries[2]["dtref"] != 4:
        raise SystemExit("Linux DT refs mismatch: %d, %d"
                         % (entries[1]["dtref"], entries[2]["dtref"]))
    if not (entries[1]["flags"] & FLAG_HAS_DTB) or not (entries[2]["flags"] & FLAG_HAS_DTB):
        raise SystemExit("Linux entries missing device-tree flag")
    if entries[1]["offset"] != entries[2]["offset"] or entries[1]["size"] != entries[2]["size"]:
        raise SystemExit("Linux-A/B must share offset and size")
    for idx, blob in ((5, archive_a_bytes), (6, archive_b_bytes)):
        if entries[idx]["flags"] != 0:
            raise SystemExit("blob %d flags must be 0" % idx)
        if entries[idx]["size"] != len(blob):
            raise SystemExit("blob %d size mismatch" % idx)
        start = entries[idx]["offset"]
        if image_bytes[start:start + len(blob)] != blob:
            raise SystemExit("blob %d content mismatch" % idx)

    # The DTs stored in the manifest must reference the right blob index.
    for idx, expect in ((3, blob_a_index), (4, blob_b_index)):
        blob_dt = out / ("verify-dt-%d.dtb" % idx)
        start, size = entries[idx]["offset"], entries[idx]["size"]
        blob_dt.write_bytes(image_bytes[start:start + size])
        _rc, val = run(["fdtget", "-t", "x", str(blob_dt), "/chosen",
                        "kshim,initrd-entry"], check=True)
        if int(val.decode().strip(), 16) != expect:
            raise SystemExit("DT %d initrd-entry mismatch" % idx)
        _rc, ba = run(["fdtget", "-t", "s", str(blob_dt), "/chosen", "bootargs"],
                      check=True)
        if ba.decode().strip() != BOOTARGS:
            raise SystemExit("DT %d bootargs mismatch" % idx)

    main_sha = sha256(main_bin)
    bad_sha = sha256(bad_bin)
    if main_sha == bad_sha:
        raise SystemExit("bad-reference pack must differ from the main pack")

    runtime_sha = sha256(runtime)
    kernel_sha = sha256(kernel)
    packer_sha = sha256(packer)
    base_sha = sha256(base)

    results = {}
    failed = False

    def record(case, ok, rc, timed_out, cmd, log, err, pack_sha):
        nonlocal failed
        results[case] = {
            "status": "PASS" if ok else "FAIL",
            "returncode": rc,
            "timeout": timed_out,
            "command": " ".join(str(c) for c in cmd),
            "log": str(log),
            "error": err,
            "pack_sha256": pack_sha,
        }
        if not ok:
            failed = True
        print(("PASS" if ok else "FAIL"), case, flush=True)

    # 1. direct-no-initrd: original kernel, no initrd, must panic for missing init.
    log = out / "case1-direct-no-initrd.log"
    cmd = qemu_base() + ["-kernel", str(kernel), "-append", BOOTARGS]
    rc, text, timed_out, err = qemu_run(cmd, log, b"")
    ok = (not timed_out and rc == 0 and err is None
          and "Linux version" in text
          and "Kernel panic" in text
          and any(s in text for s in ("No working init found",
                                      "Unable to mount root fs",
                                      "Failed to execute /init"))
          and MARKER_A not in text and MARKER_B not in text)
    record("direct-no-initrd", ok, rc, timed_out, cmd, log, err, None)

    # 2. selected-a: pick Linux A by label from the full-settings UNKNOWN-role
    #    fixture, whose alternate rows are visible but not automatic. This case
    #    verifies the archive-A real /init handoff, not the default policy.
    log = out / "case2-selected-a.log"
    cmd = qemu_base() + ["-kernel", str(main_bin)]
    rc, text, timed_out, err = qemu_run(cmd, log, b"", selection_label="Linux A")
    ok = (not timed_out and rc == 0 and err is None
          and "Linux version" in text
          and MARKER_A in text and MARKER_B not in text
          and BASE_MARKER not in text
          and "Power down" in text)
    record("selected-a", ok, rc, timed_out, cmd, log, err, main_sha)

    # 3. selected-b: same pack/hash, choose B via the menu.
    log = out / "case3-selected-b.log"
    cmd = qemu_base() + ["-kernel", str(main_bin)]
    rc, text, timed_out, err = qemu_run(cmd, log, b"", selection_label="Linux B")
    ok = (not timed_out and rc == 0 and err is None
          and "Linux version" in text
          and MARKER_B in text and MARKER_A not in text
          and BASE_MARKER not in text
          and "Power down" in text)
    record("selected-b", ok, rc, timed_out, cmd, log, err, main_sha)

    # 4. invalid-b-reference: runtime rejects the out-of-range blob ref and
    #    falls back to the base image.
    log = out / "case4-invalid-b-reference.log"
    cmd = qemu_base() + ["-kernel", str(bad_bin)]
    rc, text, timed_out, err = qemu_run(cmd, log, b"", selection_label="Linux B")
    ok = (not timed_out and rc == 0 and err is None
          and "Memory handoff failed; booting base image" in text
          and BASE_MARKER in text
          and MARKER_A not in text and MARKER_B not in text
          and "Manifest not found" not in text)
    record("invalid-b-reference", ok, rc, timed_out, cmd, log, err, bad_sha)

    (out / "validation.json").write_text(json.dumps({
        "scope": "QEMU only; no TB323FU hardware or device validation",
        "inputs": {
            "runtime_sha256": runtime_sha,
            "kernel_sha256": kernel_sha,
            "packer_sha256": packer_sha,
            "base_sha256": base_sha,
        },
        "generated": {
            "padded_image_sha256": sha256(padded),
            "archive_a_sha256": archive_a_sha,
            "archive_b_sha256": archive_b_sha,
            "main_pack_sha256": main_sha,
            "bad_pack_sha256": bad_sha,
        },
        "pack_order": PACK_ORDER,
        "blob_reference_indices": {"Image-Blob-A": blob_a_index,
                                   "Image-Blob-B": blob_b_index},
        "cases": results,
    }, indent=2) + "\n")

    if failed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
