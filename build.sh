#!/bin/sh
# Builds click.exe (needs gcc-mingw-w64-x86-64). A standard Windows build:
# normal C startup code, version info and a manifest, so antivirus tools see an
# ordinary app. The build timestamp is fixed (SOURCE_DATE_EPOCH), so every
# build of the same code is byte-identical to the released, scanned click.exe.
set -e
export SOURCE_DATE_EPOCH=1793361406 # 2026-10-30 11:56:46 UTC
x86_64-w64-mingw32-windres click.rc -O coff -o click.res
x86_64-w64-mingw32-gcc -std=c11 -O2 -s -Wall -Wextra -Wno-unused-parameter \
  -municode -mwindows -Wl,--insert-timestamp \
  click.c click.res -o click.exe -ldwmapi
rm click.res
