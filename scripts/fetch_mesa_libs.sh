#!/usr/bin/env bash
# Puts the Mesa Zink/Kopper libraries into app/src/main/jniLibs/arm64-v8a/.
# The plugin needs (names must match the manifest "renderer" field):
#   libglxshim.so  libEGL_mesa.so  libzink_dri.so  (+ any libs they depend on)
#
# Set MESA_LIBS_URL to a .zip/.tar.gz of an arm64-v8a Mesa Android build whose license you
# have checked (Mesa itself is MIT; check the packager's terms). Nothing is downloaded
# from a hard-coded third-party location on purpose.
set -euo pipefail

: "${MESA_LIBS_URL:?Set MESA_LIBS_URL to your arm64 Mesa Zink/Kopper build archive}"
dest="$(dirname "$0")/../app/src/main/jniLibs/arm64-v8a"
work="$(mktemp -d)"
mkdir -p "$dest"

curl -fL "$MESA_LIBS_URL" -o "$work/mesa.bin"
case "$MESA_LIBS_URL" in
  *.zip)            unzip -q "$work/mesa.bin" -d "$work/x" ;;
  *.tar.gz|*.tgz)   mkdir "$work/x" && tar -xzf "$work/mesa.bin" -C "$work/x" ;;
  *) echo "unsupported archive type" >&2; exit 1 ;;
esac

find "$work/x" -name '*.so' -exec cp {} "$dest/" \;

for f in libglxshim.so libEGL_mesa.so libzink_dri.so; do
  [ -f "$dest/$f" ] || { echo "missing $f in archive" >&2; exit 1; }
done
echo "Mesa libs ready in $dest"
