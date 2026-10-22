#!/usr/bin/env bash
# Fetch the pinned kShimC dependencies without git (tarballs via a GitHub mirror).
# Usage: GHPROXY=https://ghproxy.net scripts/fetch_deps.sh
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
K="$ROOT/vendor/kshim"
CACHE="$ROOT/build/cache"
mkdir -p "$CACHE"

MIRRORS=("${GHPROXY:-}" https://ghproxy.net https://gh-proxy.com https://ghfast.top "")

fetch_tar() { # owner/repo commit dest
  local repo="$1" sha="$2" dest="$3" url tmp ok=0
  if [ -f "$dest/.fetched" ] && [ "$(cat "$dest/.fetched")" = "$sha" ]; then
    echo "ok: $dest @ ${sha:0:12}"; return
  fi
  tmp="$CACHE/$(basename "$repo")-$sha.tar.gz"
  if [ ! -s "$tmp" ]; then
    for m in "${MIRRORS[@]}"; do
      url="${m:+$m/}https://github.com/$repo/archive/$sha.tar.gz"
      echo "get: $url"
      if curl -fsSL --noproxy '*' --connect-timeout 8 -m 600 "$url" -o "$tmp"; then ok=1; break; fi
    done
    [ "$ok" = 1 ] || { echo "error: cannot download $repo@$sha" >&2; exit 1; }
  fi
  rm -rf "$dest"; mkdir -p "$dest"
  tar -xzf "$tmp" -C "$dest" --strip-components=1
  echo "$sha" > "$dest/.fetched"
  echo "fetched: $dest @ ${sha:0:12}"
}

# CrDK is consumed through `git archive`, so it needs a real object store.
fetch_git() { # owner/repo commit dest
  local repo="$1" sha="$2" dest="$3" base
  if [ -d "$dest/.git" ] && [ "$(git -C "$dest" rev-parse HEAD 2>/dev/null)" = "$sha" ]; then
    echo "ok: $dest @ ${sha:0:12}"; return
  fi
  base="${GHPROXY:-https://ghproxy.net}"
  rm -rf "$dest"; mkdir -p "$dest"
  git -C "$dest" init -q
  git -C "$dest" remote add origin "$base/https://github.com/$repo"
  git -C "$dest" fetch -q --depth=1 origin "$sha"
  git -C "$dest" checkout -q --detach FETCH_HEAD
  echo "fetched: $dest @ ${sha:0:12}"
}

fetch_git Project-Aloha/CrDK b6c3335136b7f6b28271917de11e8f11982975fc "$K/lib/CrDK"
fetch_tar picolibc/picolibc f7edb0869e64b9d687f46961adeef7154921fe92 "$K/lib/picolibc"
fetch_tar lvgl/lvgl 7f07a129e8d77f4984fff8e623fd5be18ff42e74 "$K/third_party/lvgl"

# libfdt is a plain directory in the kShimC tree.
if [ ! -f "$K/lib/libfdt/fdt.c" ]; then
  if [ -f /tmp/opencode/kShimC-up/lib/libfdt/fdt.c ]; then
    cp -a /tmp/opencode/kShimC-up/lib/libfdt "$K/lib/libfdt"
  else
    fetch_tar sunflower2333/kShimC 82ce6eaa10c36e64cfd3f16799172d854f9a955b "$CACHE/kshimc"
    cp -a "$CACHE/kshimc/lib/libfdt" "$K/lib/libfdt"
  fi
fi
echo "pinned dependencies are in place."
