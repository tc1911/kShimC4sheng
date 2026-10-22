#!/usr/bin/env bash
# Install a local aarch64 cross toolchain into build/toolchain (Arch/CachyOS, no root).
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
DEST="$ROOT/build/toolchain"
if [ -x "$DEST/usr/bin/aarch64-linux-gnu-gcc" ]; then
  echo "ok: $DEST/usr/bin/aarch64-linux-gnu-gcc"; exit 0
fi
command -v pacman >/dev/null || {
  echo "not an Arch system; install aarch64-linux-gnu-gcc with your package manager" >&2; exit 1; }
mkdir -p "$DEST" "$ROOT/build/cache"
pacman -Sp aarch64-linux-gnu-gcc 2>/dev/null | grep '^http' | sort -u | while read -r u; do
  f="$ROOT/build/cache/$(basename "$u")"
  [ -s "$f" ] || curl -sSL --noproxy '*' -m 600 "$u" -o "$f"
  bsdtar -xf "$f" -C "$DEST" --exclude='.PKGINFO' --exclude='.BUILDINFO' \
    --exclude='.MTREE' --exclude='.INSTALL'
done
"$DEST/usr/bin/aarch64-linux-gnu-gcc" --version | head -1
