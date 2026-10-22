#!/usr/bin/env bash
# One-shot boot the packed image without writing boot_a (`fastboot boot`).
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/config/sheng.env"

image="$ROOT/out/boot_a.img"; assume_yes=0; force=0
while [ $# -gt 0 ]; do
  case "$1" in
    --image) image=$2; shift 2 ;;
    -y|--yes) assume_yes=1; shift ;;
    --force) force=1; shift ;;
    -h|--help) echo "usage: $0 [--image FILE] [-y] [--force]"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 1 ;;
  esac
done
[ -f "$image" ] || { echo "missing image: $image" >&2; exit 1; }
command -v fastboot >/dev/null || { echo "fastboot not found" >&2; exit 1; }
fastboot devices | grep -q . || { echo "no fastboot device; reboot to bootloader first" >&2; exit 1; }

size=$(stat -c %s "$image")
if [ "$size" -gt "$SHENG_BOOT_PARTITION_SIZE" ]; then
  echo "image too large: $size bytes" >&2; exit 1
fi
slot=$(fastboot getvar current-slot 2>&1 | awk -F': *' '/current-slot/{print $2; exit}')
echo "image: $image ($size bytes), current slot: ${slot:-unknown}"

if [ "$slot" != "a" ] && [ "$force" -ne 1 ]; then
  echo "WARNING: current slot is '${slot:-unknown}', not 'a'." >&2
  echo "  - Android entry: ABL loads init_boot_${slot}/vendor_boot_${slot}/dtbo_${slot};" >&2
  echo "    without stock slot-A images the Android entry will not boot correctly." >&2
  echo "  - Linux entry: uses the embedded DTB + initrd, so it can still be tested." >&2
  echo "  This is a one-shot boot; a failure/reboot returns to the flashed boot." >&2
fi

if [ "$assume_yes" -ne 1 ]; then
  read -r -p "fastboot boot $image (one-shot, does not write flash)? [y/N] " ans
  case "$ans" in y|Y|yes|YES) ;; *) echo "aborted"; exit 1 ;; esac
fi

exec fastboot boot "$image"
