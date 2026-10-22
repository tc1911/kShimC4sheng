#!/usr/bin/env bash
# Build the 4 MiB kShimC runtime from config/sheng_defconfig.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

missing=()
command -v cmake >/dev/null || missing+=("cmake")
command -v ninja >/dev/null || missing+=("ninja")
command -v aarch64-linux-gnu-gcc >/dev/null || missing+=("aarch64-linux-gnu-gcc")
command -v genconfig >/dev/null || missing+=("genconfig (pip install kconfiglib)")

for d in lib/CrDK lib/picolibc third_party/lvgl lib/libfdt; do
  [ -e "vendor/kshim/$d" ] || missing+=("vendor/kshim/$d (run scripts/fetch_deps.sh)")
done

if [ ${#missing[@]} -gt 0 ]; then
  printf 'missing dependency: %s\n' "${missing[@]}" >&2
  echo "see docs/bringup-plan.md (runtime toolchain)" >&2
  exit 1
fi

python3 vendor/kshim/tools/build_runtime.py \
  --config config/sheng_defconfig \
  --out out/runtime \
  --jobs "${JOBS:-8}"

install -m 0644 out/runtime/kShimC.bin out/runtime.bin
ls -l out/runtime.bin
sha256sum out/runtime.bin
