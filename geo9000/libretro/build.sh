#!/bin/sh
set -e 
make clean && make
cp geolith_libretro.dylib ../../e9k-debugger/system
echo "Copied to geolith_libretro.dylib ../../e9k-debugger/system"
