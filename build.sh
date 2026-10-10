#!/bin/bash
# Builds the game (hatrick) and the headless simulator (sim).
# ./build.sh fast  builds with -Og: quicker to compile, for quick test builds.
set -e
cd "$(dirname "$0")"
OPT=-O1   # measured faster than -O2 both to compile and per frame
if [ "$1" = fast ]; then OPT=-Og; fi
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
# the game and the sim are independent, so compile them at the same time
gcc $OPT -w hatrick.c audio.o vendor/miniaudio.o vendor/stb_image.o -o hatrick -lX11 -lm -lpthread -ldl &
game=$!
gcc $OPT -DSIM -DSC=1 -w hatrick.c -o sim
wait $game
./sim --check || true   # level file problems, if any
ls -l hatrick
