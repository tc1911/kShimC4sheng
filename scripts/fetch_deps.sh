#!/usr/bin/env bash
# Fetch the pinned kShimC dependencies (network required).
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
K="$ROOT/vendor/kshim"

fetch() { # path url commit
  local path="$1" url="$2" commit="$3"
  if [ -d "$path/.git" ] && [ "$(git -C "$path" rev-parse HEAD 2>/dev/null)" = "$commit" ]; then
    echo "ok: $path @ $commit"; return
  fi
  rm -rf "$path"; mkdir -p "$path"
  git init -q "$path"
  git -C "$path" remote add origin "$url"
  git -C "$path" fetch -q --depth=1 origin "$commit"
  git -C "$path" checkout -q --detach FETCH_HEAD
  echo "fetched: $path @ $commit"
}

fetch "$K/lib/CrDK"      https://github.com/Project-Aloha/CrDK b6c3335136b7f6b28271917de11e8f11982975fc
fetch "$K/lib/picolibc"  https://github.com/picolibc/picolibc f7edb0869e64b9d687f46961adeef7154921fe92
fetch "$K/third_party/lvgl" https://github.com/lvgl/lvgl 7f07a129e8d77f4984fff8e623fd5be18ff42e74

# libfdt is vendored inside the kShimC repo; sparse-checkout just that path.
if [ ! -f "$K/lib/libfdt/fdt.c" ]; then
  tmp=$(mktemp -d)
  git clone -q --filter=blob:none --no-checkout https://github.com/sunflower2333/kShimC "$tmp"
  git -C "$tmp" sparse-checkout set lib/libfdt
  git -C "$tmp" checkout -q 82ce6eaa10c36e64cfd3f16799172d854f9a955b
  rm -rf "$K/lib/libfdt"; cp -a "$tmp/lib/libfdt" "$K/lib/libfdt"
  rm -rf "$tmp"
  echo "copied: $K/lib/libfdt @ 82ce6eaa"
fi

printf 'pinned dependencies are in place.\n'
printf 'next: python3 -m venv build/venv && build/venv/bin/pip install kconfiglib\n'
printf '      export PATH="$PWD/build/venv/bin:$PATH"\n'
