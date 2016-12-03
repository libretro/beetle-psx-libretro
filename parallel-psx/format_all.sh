#!/bin/bash

for file in vulkan/*.{cpp,hpp} vulkan/wsi/*.{cpp,hpp} *.cpp renderer/*.{cpp,hpp} atlas/*.{cpp,hpp}
do
    echo "Formatting file: $file ..."
    clang-format -style=file -i $file
done
