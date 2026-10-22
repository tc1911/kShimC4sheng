#!/usr/bin/env bash
# Build out/boot_a.img from an extracted payload directory.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/config/sheng.env"

usage() {
  cat <<'USAGE'
usage: scripts/build.sh --payload <dir> --runtime <kShimC.bin> --out <boot_a.img> [options]

payload dir must contain:
  stock-boot.img       stock Android boot image (v4)
  linux-Image          Linux kernel (raw ARM64 Image)
  linux.dtb            device tree blob
  linux-initrd.img     optional initrd

options:
  --default android|linux   menu default (default: android)
  --timeout-ms N            menu timeout (default: 8000)
  --bootargs STR            Linux cmdline (default: config/sheng.env)
  --partition-size N        boot partition size (default: 201326592)
  --keep-work               keep out/work
USAGE
}

payload=""; runtime=""; out=""; default="$SHENG_DEFAULT_OS"; timeout="$SHENG_TIMEOUT_MS"
bootargs="$SHENG_BOOTARGS"; part="$SHENG_BOOT_PARTITION_SIZE"; keep=()
while [ $# -gt 0 ]; do
  case "$1" in
    --payload) payload=$2; shift 2 ;;
    --runtime) runtime=$2; shift 2 ;;
    --out) out=$2; shift 2 ;;
    --default) default=$2; shift 2 ;;
    --timeout-ms) timeout=$2; shift 2 ;;
    --bootargs) bootargs=$2; shift 2 ;;
    --partition-size) part=$2; shift 2 ;;
    --keep-work) keep=(--keep-work); shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 1 ;;
  esac
done
[ -n "$payload" ] && [ -n "$runtime" ] && [ -n "$out" ] || { usage >&2; exit 1; }

for f in "$payload/stock-boot.img" "$payload/linux-Image" "$payload/linux.dtb" "$runtime"; do
  [ -f "$f" ] || { echo "missing: $f" >&2; exit 1; }
done
initrd=()
if [ -f "$payload/linux-initrd.img" ]; then initrd=(--initrd "$payload/linux-initrd.img"); fi

python3 "$ROOT/packer/make_boot.py" \
  --stock-boot "$payload/stock-boot.img" \
  --linux-image "$payload/linux-Image" \
  --dtb "$payload/linux.dtb" \
  "${initrd[@]}" \
  --runtime "$runtime" \
  --out "$out" \
  --default "$default" \
  --timeout-ms "$timeout" \
  --bootargs "$bootargs" \
  --partition-size "$part" \
  "${keep[@]}"
