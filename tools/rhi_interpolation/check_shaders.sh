#!/bin/sh
set -eu

root=${1:-../..}

line_expression()
{
   sed 's/[[:space:]]//g' "$1" |
      sed -n -e 's/^n=\(.*\);$/\1/p' -e 's/^intn=\(.*\);$/\1/p'
}

expected=$(line_expression "$root/tools/rhi_interpolation/interpolation.c")
if [ -z "$expected" ]; then
   echo "Missing native line tie expression in the C test" >&2
   exit 1
fi

for shader in rhi/shaders_gl/command_fragment.glsl.h rhi/shaders_vulkan/primitive.h; do
   actual=$(line_expression "$root/$shader")
   if [ "$actual" != "$expected" ]; then
      echo "$shader: native line tie expression differs from the C test" >&2
      exit 1
   fi
done

echo "Native line tie expressions match both shaders"
