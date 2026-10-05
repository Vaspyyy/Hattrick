#!/bin/bash
# Builds the tiny game (hatrick) and the headless simulator (sim).
set -e
cd "$(dirname "$0")"
python3 gen.py >/dev/null
python3 mklevels.py
CF="-m32 -O2 -fomit-frame-pointer -fno-guess-branch-probability -fno-tree-loop-distribute-patterns -s -nostartfiles -nostdlib -fno-pic -no-pie -fno-plt -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-stack-protector -fno-ident -fcf-protection=none"
LF="-Wl,-T,tiny.ld -Wl,--build-id=none -Wl,-z,norelro -Wl,--hash-style=gnu -Wl,--spare-dynamic-tags=0 -Wl,--no-warn-rwx-segments"
gcc $CF $LF hatrick.c -o hatrick -L/usr/lib32 -lX11
python3 sstrip.py hatrick
gcc -O1 -DSIM -DSC=1 -w hatrick.c -o sim
ls -l hatrick
