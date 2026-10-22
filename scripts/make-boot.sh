#!/usr/bin/env bash
# One-shot: stock boot image + Linux kernel/DTB (+ optional initrd) -> boot_a.img
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/config/sheng.env"

usage() {
  cat <<'USAGE'
usage:
  scripts/make-boot.sh --stock-boot <stock_boot_a.img> --distro-boot <linux-boot.img> [options]
  scripts/make-boot.sh --stock-boot <stock_boot_a.img> --linux-image <Image> --dtb <dtb> [--initrd <img>] [options]

options:
  --out <file>        output image (default: out/boot_a.img)
  --runtime <file>    kShimC runtime (default: out/runtime.bin, 4 MiB)
  --default android|linux   menu default (default: android)
  --timeout-ms N      menu timeout (default: 8000)
  --bootargs STR      Linux cmdline (default: root=PARTLABEL=linux rw rootwait)
  --keep-payload      keep the extracted payload directory
  -h, --help

The runtime only changes when the menu/config changes; extract + repack do not
need a cross compiler. If the packer binary is missing it is built with cmake.
USAGE
}

stock=""; distro=""; image=""; dtb=""; initrd=""
out="$ROOT/out/boot_a.img"; runtime="${KSHIM_RUNTIME:-}"
if [ -z "$runtime" ]; then
  if [ -f "$ROOT/out/runtime.bin" ]; then runtime="$ROOT/out/runtime.bin"
  else runtime="$ROOT/prebuilt/sheng-runtime.bin"; fi
fi
default="$SHENG_DEFAULT_OS"; timeout="$SHENG_TIMEOUT_MS"; bootargs="$SHENG_BOOTARGS"
keep=0
while [ $# -gt 0 ]; do
  case "$1" in
    --stock-boot) stock=$2; shift 2 ;;
    --distro-boot) distro=$2; shift 2 ;;
    --linux-image) image=$2; shift 2 ;;
    --dtb) dtb=$2; shift 2 ;;
    --initrd) initrd=$2; shift 2 ;;
    --out|-o) out=$2; shift 2 ;;
    --runtime) runtime=$2; shift 2 ;;
    --default) default=$2; shift 2 ;;
    --timeout-ms) timeout=$2; shift 2 ;;
    --bootargs) bootargs=$2; shift 2 ;;
    --keep-payload) keep=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 1 ;;
  esac
done

[ -n "$stock" ] || { echo "missing --stock-boot" >&2; usage >&2; exit 1; }
[ -f "$stock" ] || { echo "missing file: $stock" >&2; exit 1; }
if [ -n "$distro" ]; then
  [ -z "$image$dtb$initrd" ] || { echo "use either --distro-boot or --linux-image/--dtb" >&2; exit 1; }
elif [ -n "$image" ] && [ -n "$dtb" ]; then
  :
else
  echo "missing Linux payload: --distro-boot or (--linux-image and --dtb)" >&2
  usage >&2; exit 1
fi

if [ ! -x "$ROOT/build/packer/MultiBootKernelPatcher" ]; then
  echo "== building MultiBootKernelPatcher"
  cmake -S "$ROOT/vendor/packer" -B "$ROOT/build/packer" \
        -DBUILD_SHIMS=OFF -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "$ROOT/build/packer" -j"${JOBS:-8}" >/dev/null
fi

if [ ! -f "$runtime" ]; then
  echo "missing runtime: $runtime" >&2
  echo "build it once: scripts/build_runtime.sh config/sheng_defconfig out/runtime" >&2
  echo "(or copy an existing 4 MiB kShimC.bin and pass --runtime)" >&2
  exit 1
fi
rsize=$(stat -c %s "$runtime")
[ "$rsize" -eq 4194304 ] || { echo "runtime must be exactly 4 MiB, got $rsize" >&2; exit 1; }

mkdir -p "$ROOT/out"
work=$(mktemp -d "$ROOT/out/.make-boot.XXXXXX")
cleanup() { [ "$keep" -eq 1 ] || rm -rf "$work"; }
trap cleanup EXIT

prep=(--stock-boot "$stock" --out "$work")
if [ -n "$distro" ]; then
  prep+=(--distro-boot "$distro")
else
  prep+=(--linux-image "$image" --dtb "$dtb")
  [ -n "$initrd" ] && prep+=(--initrd "$initrd")
fi
"$ROOT/scripts/prepare.sh" "${prep[@]}"
[ "$keep" -eq 1 ] && echo "payload kept in $work"

"$ROOT/scripts/build.sh" \
  --payload "$work" --runtime "$runtime" --out "$out" \
  --default "$default" --timeout-ms "$timeout" --bootargs "$bootargs"

echo
echo "boot image: $out"
echo "next: fastboot flash boot_a $out   (or scripts/flash.sh --image $out)"
