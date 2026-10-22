#!/usr/bin/env bash
# Flash the kShimC boot image to boot_a (Android slot).
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/config/sheng.env"

image="$ROOT/out/boot_a.img"; assume_yes=0; no_reboot=0
while [ $# -gt 0 ]; do
  case "$1" in
    --image) image=$2; shift 2 ;;
    -y|--yes) assume_yes=1; shift ;;
    --no-reboot) no_reboot=1; shift ;;
    -h|--help) echo "usage: $0 [--image FILE] [-y] [--no-reboot]"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 1 ;;
  esac
done
[ -f "$image" ] || { echo "missing image: $image" >&2; exit 1; }

command -v fastboot >/dev/null || { echo "fastboot not found" >&2; exit 1; }
fastboot devices | grep -q . || { echo "no fastboot device; reboot to bootloader first" >&2; exit 1; }

slot=$(fastboot getvar current-slot 2>&1 | awk -F': *' '/current-slot/{print $2; exit}')
if [ "$slot" != "a" ]; then
  echo "WARNING: current slot is '$slot', expected 'a'." >&2
  echo "The shim in boot_a boots Android slot A; set slot a first or Android may not boot." >&2
fi
size=$(stat -c %s "$image")
if [ "$size" -gt $((SHENG_BOOT_PARTITION_SIZE - 65536)) ]; then
  echo "image too large: $size bytes" >&2; exit 1
fi

if [ "$assume_yes" -ne 1 ]; then
  read -r -p "flash $image ($size bytes) to boot_a? [y/N] " ans
  case "$ans" in y|Y|yes|YES) ;; *) echo "aborted"; exit 1 ;; esac
fi

fastboot flash boot_a "$image"
if [ "$no_reboot" -eq 0 ]; then fastboot reboot; fi
