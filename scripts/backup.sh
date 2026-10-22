#!/usr/bin/env bash
# Back up the partitions needed to roll back the kShimC experiment.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
out="$ROOT/out/backup"; parts="boot_a boot_b dtbo_a dtbo_b init_boot_a vendor_boot_a"
while [ $# -gt 0 ]; do
  case "$1" in
    --out) out=$2; shift 2 ;;
    --parts) parts=$2; shift 2 ;;
    -h|--help) echo "usage: $0 [--out DIR] [--parts 'boot_a dtbo_a ...']"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 1 ;;
  esac
done
mkdir -p "$out"
command -v adb >/dev/null || { echo "adb not found" >&2; exit 1; }
adb get-state >/dev/null 2>&1 || { echo "no adb device (boot Android or TWRP first)" >&2; exit 1; }

for p in $parts; do
  echo "== $p"
  # Works in TWRP and in rooted Android (su -c). Adjust if your shell lacks su.
  adb shell "su -c 'dd if=/dev/block/by-name/$p of=/tmp/$p.img bs=4M'" 2>/dev/null \
    || adb shell "dd if=/dev/block/by-name/$p of=/tmp/$p.img bs=4M"
  adb pull "/tmp/$p.img" "$out/$p.img"
  adb shell "rm -f /tmp/$p.img"
  sha256sum "$out/$p.img"
done
echo "backup written to $out"
