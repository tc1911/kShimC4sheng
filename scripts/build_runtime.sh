#!/usr/bin/env bash
# Build the 4 MiB kShimC runtime from config/sheng_defconfig.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

missing=()
command -v cmake >/dev/null || missing+=("cmake")
command -v ninja >/dev/null || missing+=("ninja")

# Toolchain: explicit prefix, project-local unpack, or system PATH.
if [ -n "${KSHIM_TOOLCHAIN_PREFIX:-}" ]; then
  PREFIX="$KSHIM_TOOLCHAIN_PREFIX"
elif [ -x "$ROOT/build/toolchain/bin/aarch64-linux-gnu-gcc" ]; then
  PREFIX="$ROOT/build/toolchain/bin/aarch64-linux-gnu-"
elif [ -x "$ROOT/build/toolchain/usr/bin/aarch64-linux-gnu-gcc" ]; then
  PREFIX="$ROOT/build/toolchain/usr/bin/aarch64-linux-gnu-"
elif command -v aarch64-linux-gnu-gcc >/dev/null; then
  PREFIX="$(dirname "$(command -v aarch64-linux-gnu-gcc)")/aarch64-linux-gnu-"
else
  missing+=("aarch64-linux-gnu-gcc (run scripts/fetch_toolchain.sh)")
  PREFIX=""
fi
if [ -n "$PREFIX" ]; then
  export KSHIM_TOOLCHAIN_PREFIX="$PREFIX"
  export PATH="$(dirname "${PREFIX}gcc"):$PATH"
fi
if ! command -v genconfig >/dev/null; then
  if [ -f /opt/klipper/lib/kconfiglib/genconfig.py ]; then
    mkdir -p build/bin
    printf '#!/bin/sh\nexec python3 /opt/klipper/lib/kconfiglib/genconfig.py "$@"\n' > build/bin/genconfig
    chmod +x build/bin/genconfig
    export PATH="$ROOT/build/bin:$PATH"
    export PYTHONPATH="/opt/klipper/lib/kconfiglib${PYTHONPATH:+:$PYTHONPATH}"
  else
    missing+=("genconfig (pip install kconfiglib)")
  fi
fi

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
