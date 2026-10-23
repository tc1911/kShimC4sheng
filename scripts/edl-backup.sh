#!/usr/bin/env bash
# 9008/EDL read-only backup using the Xiaomi SM8550 loader.
# Dumps every GPT partition except userdata, plus GPT, to $OUT.
#
# Usage:
#   scripts/edl-backup.sh [OUTDIR] [--loader FILE] [--reboot] [--reset]
#
# The device must already be in EDL (Qualcomm HS-USB QDLoader 9008,
# USB 05c6:9008). This script never writes to the device.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
OUT="$ROOT/out/backup/edl"
LOADER="/home/tc191/小米8G2引导-铠侠勿用.melf"
DO_REBOOT=0
DO_RESET=0

while [ $# -gt 0 ]; do
  case "$1" in
    --loader) LOADER=$2; shift 2 ;;
    --reboot) DO_REBOOT=1; shift ;;
    --reset) DO_RESET=1; shift ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    -*) echo "unknown option: $1" >&2; exit 1 ;;
    *) OUT=$1; shift ;;
  esac
done

EDL="$ROOT/scripts/edl-local"
[ -x "$EDL" ] || { echo "missing $EDL (run from the project tree)" >&2; exit 1; }
[ -f "$LOADER" ] || { echo "loader not found: $LOADER" >&2; exit 1; }
mkdir -p "$OUT"

in_edl() { lsusb 2>/dev/null | grep -qi '05c6:9008'; }

if ! in_edl; then
  if [ "$DO_REBOOT" = 1 ]; then
    echo "rebooting to EDL..."
    adb reboot edl
    for _ in $(seq 1 30); do
      sleep 1
      in_edl && break
    done
  fi
  if ! in_edl; then
    echo "no EDL device (05c6:9008). Put the tablet in 9008 mode first:" >&2
    echo "  adb reboot edl        # with root" >&2
    echo "or run this script with --reboot" >&2
    exit 1
  fi
fi

echo "loader: $LOADER"
echo "output: $OUT"

# Read-only probe: this also identifies the UFS vendor.
"$EDL" --loader="$LOADER" --memory=ufs printgpt | tee "$OUT/gpt.txt"
if grep -qi 'kioxia' "$OUT/gpt.txt"; then
  echo "ABORT: this loader is marked unsafe for Kioxia UFS, and the device reports Kioxia." >&2
  exit 1
fi

avail=$(df -B1 --output=avail "$OUT" | tail -1 | tr -d ' ')
if [ "$avail" -lt $((16 * 1024 * 1024 * 1024)) ]; then
  echo "not enough space: need at least 16 GiB, have $avail bytes" >&2
  exit 1
fi

# rl reads all GPT partitions. --skip=userdata keeps the 232 GiB encrypted
# userdata out of the backup; --genxml writes rawprogram XML for reference.
"$EDL" --loader="$LOADER" --memory=ufs rl "$OUT" --skip=userdata --genxml

if find "$OUT" -type f -name 'userdata.bin' | grep -q .; then
  echo "ERROR: userdata was dumped; stopping before hashing." >&2
  exit 1
fi

find "$OUT" -type f \( -name '*.bin' -o -name '*.xml' \) -print0 \
  | sort -z | xargs -0 sha256sum > "$OUT/SHA256SUMS"
find "$OUT" -type f -name '*.bin' -printf '%f %s\n' | sort > "$OUT/SIZES.txt"

cat > "$OUT/README.txt" <<'TXT'
EDL backup for Xiaomi Pad 6S Pro (sheng), excluding userdata.

Files are per-partition .bin dumps plus gpt_main0.bin/gpt_backup0.bin.
Restore one partition (read/write requires a working loader and EDL access):
  edl --loader=LOADER --memory=ufs w <partition> <partition>.bin
Restore all non-GPT partitions in this directory:
  edl --loader=LOADER --memory=ufs wl .
userdata is not included; restore /sdcard and app data separately.
TXT

echo "done: $OUT"
du -sh "$OUT"
wc -l "$OUT/SHA256SUMS"

if [ "$DO_RESET" = 1 ]; then
  "$EDL" --loader="$LOADER" --memory=ufs reset || true
fi
