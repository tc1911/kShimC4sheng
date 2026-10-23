#!/usr/bin/env bash
# Back up every GPT partition except userdata to the PC.
#
# Works from Android with root (KernelSU/Magisk) or from TWRP.
# userdata is intentionally excluded: back it up at file level instead.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
OUT="$ROOT/out/backup/partitions"
SKIP_SUPER=0
while [ $# -gt 0 ]; do
  case "$1" in
    --skip-super) SKIP_SUPER=1; shift ;;
    --out) OUT=$2; shift 2 ;;
    -h|--help) echo "usage: $0 [--skip-super] [--out DIR]"; exit 0 ;;
    *) OUT=$1; shift ;;
  esac
done
mkdir -p "$OUT"

command -v adb >/dev/null || { echo "adb not found" >&2; exit 1; }
adb wait-for-device

if [ "$(adb shell id -u 2>/dev/null | tr -d '\r')" = "0" ]; then
  ROOTMODE=direct
elif adb shell 'su -c id -u' 2>/dev/null | tr -d '\r' | grep -qx 0; then
  ROOTMODE=su
else
  echo "need root: boot TWRP or grant su" >&2
  exit 1
fi

run() {
  if [ "$ROOTMODE" = direct ]; then
    adb exec-out sh -c "$1"
  else
    adb exec-out su -c "$1"
  fi
}

names=()
sizes=()
total=0
for name in $(adb shell ls /dev/block/by-name | tr -d '\r' | sort); do
  case "$name" in
    userdata|sd*) continue ;;
    super) [ "$SKIP_SUPER" -eq 1 ] && continue ;;
  esac
  size=$(run "blockdev --getsize64 /dev/block/by-name/$name" | tr -d '\r')
  case "$size" in ''|*[!0-9]*) continue ;; esac
  [ "$size" -gt 0 ] || continue
  names+=("$name")
  sizes+=("$size")
  total=$((total + size))
done
[ "${#names[@]}" -gt 0 ] || { echo "no partitions found" >&2; exit 1; }

avail=$(df -B1 --output=avail "$OUT" | tail -1 | tr -d ' ')
if [ "$avail" -lt $((total + 1073741824)) ]; then
  echo "not enough space: need $total bytes + 1 GiB margin, have $avail" >&2
  exit 1
fi
echo "root mode: $ROOTMODE"
echo "partitions: ${#names[@]}, total: $total bytes (skip_super=$SKIP_SUPER)"
echo "output: $OUT"

: > "$OUT/SHA256SUMS"
: > "$OUT/SIZES.txt"
for i in "${!names[@]}"; do
  name=${names[$i]}
  size=${sizes[$i]}
  echo "== $name ($size bytes)"
  if command -v pv >/dev/null 2>&1; then
    run "dd if=/dev/block/by-name/$name bs=1M 2>/dev/null" | pv -s "$size" > "$OUT/$name.img"
  else
    run "dd if=/dev/block/by-name/$name bs=1M 2>/dev/null" > "$OUT/$name.img"
  fi
  got=$(stat -c %s "$OUT/$name.img")
  if [ "$got" != "$size" ]; then
    echo "size mismatch for $name: got $got, want $size" >&2
    exit 1
  fi
  echo "$name $size" >> "$OUT/SIZES.txt"
  sha256sum "$OUT/$name.img" >> "$OUT/SHA256SUMS"
done

sectors=$(run "blockdev --getsz /dev/block/sda" | tr -d '\r')
echo "== GPT (disk $sectors sectors)"
run "dd if=/dev/block/sda bs=512 count=34 2>/dev/null" > "$OUT/gpt-primary.bin"
run "dd if=/dev/block/sda bs=512 skip=$((sectors - 33)) count=33 2>/dev/null" > "$OUT/gpt-backup.bin"
sha256sum "$OUT/gpt-primary.bin" "$OUT/gpt-backup.bin" >> "$OUT/SHA256SUMS"

cat > "$OUT/README.txt" <<'TXT'
Partition backup for Xiaomi Pad 6S Pro (sheng), excluding userdata.

Restore a normal partition:
  fastboot flash <name> <name>.img
Restore super (raw dump, not sparse):
  fastboot flash super super.img
GPT restore is only for a corrupted partition table; do it from TWRP:
  dd if=gpt-primary.bin of=/dev/block/sda bs=512 count=34 conv=notrunc
  dd if=gpt-backup.bin  of=/dev/block/sda bs=512 seek=$((total_sectors-33)) conv=notrunc
(userdata is not included: restore /sdcard and app data separately.)
TXT

echo "done: $OUT"
wc -l "$OUT/SHA256SUMS"
