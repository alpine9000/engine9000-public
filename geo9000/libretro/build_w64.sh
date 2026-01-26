#!/bin/sh
set -e
make clean
CC=x86_64-w64-mingw32-gcc platform=win64 make
cp geolith_libretro.dll ../../e9k-debugger/system
echo "copied to geolith_libretro.dll ../../e9k-debugger/system"
