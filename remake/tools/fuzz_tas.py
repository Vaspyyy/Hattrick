#!/usr/bin/env python3
# Random but purposeful input for comparing the remake's port with the C game: mostly running right
# with jumps, cap throws, crouches, rolls and pounds, now and then a turn back. The same seed always
# gives the same file (the C simulator's .tas format).
#   python3 remake/tools/fuzz_tas.py SEED FRAMES > /tmp/fuzz.tas
import random
import sys

seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
frames = int(sys.argv[2]) if len(sys.argv) > 2 else 3000
rng = random.Random(seed)
moves = [  # (weight, keys, min frames, max frames)
    (30, "R", 4, 40), (14, "RJ", 4, 22), (6, "J", 2, 18), (5, "RC", 1, 3), (3, "C", 1, 3),
    (3, "UC", 1, 3), (2, "V", 1, 3), (3, "RD", 4, 20), (3, "D", 2, 10), (2, "DJ", 2, 12),
    (4, "L", 3, 25), (3, "LJ", 4, 20), (2, "-", 2, 20), (2, "RDJ", 2, 10), (1, "U", 4, 20),
]
total = sum(m[0] for m in moves)
out, left = [], frames
while left > 0:
    r = rng.uniform(0, total)
    for w, keys, lo, hi in moves:
        r -= w
        if r <= 0:
            break
    n = min(left, rng.randint(lo, hi))
    if keys.endswith("D") and rng.random() < 0.5 and out:   # a ground pound: jump, then Down in the air
        out.append("10 RJ")
        left -= 10
        n = min(left, n)
    out.append(f"{n} {keys}")
    if keys != "-" and rng.random() < 0.35:   # let go before the next move, so presses are new
        out.append("1 -")
        left -= 1
    left -= n
print("\n".join(out))
