#!/bin/bash
# Cross-build MiniVNC for classic Macs with the Retro68 toolchain.
#   ARCH=68k (default) -> 68000/020 app (runs on 68k Macs and PPC via emulation)
#   ARCH=ppc           -> native PowerPC (CFM) app, e.g. for a Power Mac G3
# Outputs build[-ppc]/MiniVNC.bin (MacBinary APPL) and MiniVNC.dsk (disk image).
set -eu
RETRO68="${RETRO68:-$HOME/Retro68-build/toolchain}"
ARCH="${ARCH:-68k}"
HERE="$(cd "$(dirname "$0")" && pwd)"

case "$ARCH" in
  68k) TOOLCHAIN="$RETRO68/m68k-apple-macos/cmake/retro68.toolchain.cmake";  BUILD="$HERE/build" ;;
  ppc) TOOLCHAIN="$RETRO68/powerpc-apple-macos/cmake/retroppc.toolchain.cmake"; BUILD="$HERE/build-ppc" ;;
  *)   echo "ARCH must be 68k or ppc"; exit 1 ;;
esac

if [ ! -f "$TOOLCHAIN" ]; then
  echo "Retro68 toolchain not found at $TOOLCHAIN"; exit 1
fi

echo ">>> Building MiniVNC for ARCH=$ARCH"
mkdir -p "$BUILD"
cd "$BUILD"
PATH="$RETRO68/bin:$PATH" cmake .. \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DCMAKE_BUILD_TYPE=Release "$@"
PATH="$RETRO68/bin:$PATH" cmake --build . -- -j"$(sysctl -n hw.ncpu)"
echo "---"
ls -la "$BUILD"/MiniVNC.bin "$BUILD"/MiniVNC.dsk 2>/dev/null || echo "(no output artifacts yet)"
