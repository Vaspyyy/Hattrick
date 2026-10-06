#!/bin/bash
# Builds the game (hatrick) and the headless simulator (sim).
set -e
cd "$(dirname "$0")"
python3 gen.py >/dev/null
python3 mklevels.py
# miniaudio is large: compile it once and reuse the object until vendor/ changes
if [ ! -f vendor/miniaudio.o ] || [ vendor/miniaudio.h -nt vendor/miniaudio.o ] || [ vendor/miniaudio.c -nt vendor/miniaudio.o ]; then
  gcc -O2 -w -c vendor/miniaudio.c -o vendor/miniaudio.o
fi
if [ ! -f vendor/stb_image.o ] || [ vendor/stb_image.h -nt vendor/stb_image.o ] || [ vendor/stb_image.c -nt vendor/stb_image.o ]; then
  gcc -O2 -w -c vendor/stb_image.c -o vendor/stb_image.o
fi
gcc -O2 -Wall -Wno-unused-function -Wno-parentheses -Wno-sign-compare -Wno-char-subscripts -c audio.c -o audio.o
gcc -O2 -w hatrick.c audio.o vendor/miniaudio.o vendor/stb_image.o -o hatrick -lX11 -lm -lpthread -ldl
gcc -O1 -DSIM -DSC=1 -w hatrick.c -o sim
./sim --check || true   # level file problems, if any
ls -l hatrick
