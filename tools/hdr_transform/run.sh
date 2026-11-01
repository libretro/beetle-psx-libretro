#!/bin/sh
# Builds and runs transform_test as strict C89 against the shipping
# rhi/hdr_transform.c and rhi/hdr_aces2.c, then under AddressSanitizer +
# UBSan (skipped where the toolchain lacks them). Also checks that every
# curve constant in the CPU code appears in the shader the GPUs run, so a
# constant edited on one side only cannot ship.
# CC=x86_64-w64-mingw32-gcc RUN=wine runs it as a Windows build.
# With glslangValidator on the path (or GLSLANG= pointing at it), it also
# compiles every GL and GLES source the HDR display path builds at runtime -
# the generated analog, bloom and post shaders and the spliced output
# fragment - since a GLES context is not something CI can create.
set -e
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
SRC="tools/hdr_transform/transform_test.c rhi/hdr_transform.c rhi/hdr_aces2.c"
OUT=tools/hdr_transform/transform_test
FLAGS="-std=c89 -pedantic -Wno-long-long -Wno-variadic-macros -O2 -g -Wall -Werror -I. -Irhi -Ilibretro-common/include"

missing=0
for k in $(sed -n '/^\/\* ---- curves/,/^\/\* ---- Filmic Log/p' rhi/hdr_transform.c |
      grep -o '[0-9]\.[0-9]\{6,\}' | sort -u); do
   if ! grep -q "$k" rhi/shaders_vulkan/hdr_transform.h; then
      echo "constant $k is in rhi/hdr_transform.c but not in the shader" >&2
      missing=1
   fi
done
[ "$missing" = 0 ] || exit 1

$CC $FLAGS -o "$OUT" $SRC -lm
if [ -n "${RUN+x}" ]; then
   [ -f "$OUT.exe" ] && EXE="$OUT.exe" || EXE="$OUT"
   $RUN "$EXE"
else
   "./$OUT"
fi
if [ -z "${RUN+x}" ] && $CC $FLAGS -fsanitize=address,undefined \
      -fno-sanitize-recover=undefined -o "${OUT}_asan" $SRC -lm 2>/dev/null; then
   ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 "./${OUT}_asan"
fi
rm -f "$OUT" "$OUT.exe" "${OUT}_asan" "${OUT}_asan.exe"

GLSLANG="${GLSLANG:-$(command -v glslangValidator || true)}"
if [ -n "$GLSLANG" ] && [ -z "${RUN+x}" ]; then
   D=$(mktemp -d)
   for es in "" "-DHAVE_OPENGLES3"; do
      cc -std=c99 -Wall -Werror -Wno-unused-variable -I. -Irhi/shaders_gl $es \
         -o "$D/gl_shaders" tools/hdr_transform/gl_shaders.c
      "$D/gl_shaders" "$D"
   done
   bad=0
   for f in "$D"/*.vert "$D"/*.frag; do
      if ! "$GLSLANG" "$f" > "$D/log" 2>&1; then
         echo "GL shader $(basename "$f") does not compile:" >&2
         cat "$D/log" >&2
         bad=1
      fi
   done
   rm -rf "$D"
   [ "$bad" = 0 ] || exit 1
   echo "hdr_transform: GL and GLES shaders compile"
fi
