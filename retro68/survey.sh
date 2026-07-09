#!/bin/bash
# Quick compile-survey of every MiniVNC translation unit under Retro68/GCC.
# Not the real build (that's CMake add_application) -- just a fast way to see
# which files compile and what the first error is on the ones that don't.
set -u
RETRO68="${RETRO68:-$HOME/Retro68-build/toolchain}"
GXX="$RETRO68/bin/m68k-apple-macos-g++"
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../mac-cpp-source"
OUT="${OUT:-/tmp/minivnc-r68}"
mkdir -p "$OUT"

DEFS=(-DTARGET_API_MAC_OS8=1 -DOLDROUTINENAMES=1)
# Note: libs/ root is deliberately NOT on the include path, so `#include "MacTCP.h"`
# resolves to Retro68's universal (CFM-correct) header rather than the vintage
# bundled libs/MacTCP.h. compat.h (the only other libs/ root header) has no users.
INC=(-I"$SRC" -I"$SRC/libs/miniz-3.0.2"
     -I"$SRC/libs/Ari Halberstadt" -I"$SRC/libs/Common Libs" -I"$SRC/libs/ChromiVNC")

ok=0; fail=0
printf "%-28s %s\n" FILE RESULT
for f in $(cd "$SRC" && ls *.cpp); do
  b="${f%.cpp}"
  "$GXX" -c -fno-exceptions -fno-rtti -fpermissive "${DEFS[@]}" \
      -include "$HERE/prefix.h" "${INC[@]}" "$SRC/$f" -o "$OUT/$b.o" 2>"$OUT/$b.err"
  if [ $? -eq 0 ]; then
    printf "%-28s OK\n" "$f"; ok=$((ok+1))
  else
    e=$(grep -m1 'error:' "$OUT/$b.err" | sed 's/.*error: //' | cut -c1-58)
    printf "%-28s FAIL: %s\n" "$f" "$e"; fail=$((fail+1))
  fi
done
echo "-----"
echo "OK=$ok  FAIL=$fail  (errors in $OUT/*.err)"
