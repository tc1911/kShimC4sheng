#!/usr/bin/env bash
# Extract the stock Android kernel and the Linux payload from boot images.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/config/sheng.env"

usage() {
  cat <<'USAGE'
usage: scripts/prepare.sh --stock-boot <boot_a.img> --distro-boot <linux-boot.img> --out <dir>
       scripts/prepare.sh --stock-boot <boot_a.img> --linux-image <Image> --dtb <dtb> [--initrd <img>] --out <dir>

--stock-boot   full boot_a dump or stock boot image
--distro-boot  community Linux boot image (Image.gz + DTB [+ initrd])
--linux-image  raw ARM64 Image (alternative to --distro-boot)
--dtb          device tree blob (alternative to --distro-boot)
--initrd       optional initrd (alternative to --distro-boot)
USAGE
}

stock=""; distro=""; image=""; dtb=""; initrd=""; out=""
while [ $# -gt 0 ]; do
  case "$1" in
    --stock-boot) stock=$2; shift 2 ;;
    --distro-boot) distro=$2; shift 2 ;;
    --linux-image) image=$2; shift 2 ;;
    --dtb) dtb=$2; shift 2 ;;
    --initrd) initrd=$2; shift 2 ;;
    --out) out=$2; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 1 ;;
  esac
done

[ -n "$stock" ] && [ -n "$out" ] || { usage >&2; exit 1; }
[ -n "$distro" ] || [ -n "$image" ] || { echo "need --distro-boot or --linux-image" >&2; exit 1; }
mkdir -p "$out"

python3 "$ROOT/packer/extract.py" --stock --boot "$stock" --out "$out" --prefix "$SHENG_STOCK_PREFIX"

if [ -n "$distro" ]; then
  python3 "$ROOT/packer/extract.py" --boot "$distro" --out "$out" --prefix "$SHENG_LINUX_PREFIX"
else
  cp "$image" "$out/$SHENG_LINUX_PREFIX-Image"
  cp "$dtb" "$out/$SHENG_LINUX_PREFIX.dtb"
  if [ -n "$initrd" ]; then cp "$initrd" "$out/$SHENG_LINUX_PREFIX-initrd.img"; fi
fi

echo
echo "payload ready in $out:"
ls -l "$out"
