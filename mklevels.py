# Converts levels.txt (ASCII maps, 32 rows each, separated by lines starting with '=')
# into levels.h: per level [startx, starty] then 4-byte records x, y|type<<5, w, h.
# Type 0 ends the level (goal x,y and level width), type 7 spawns an enemy (w = kind).
import sys
TILES = {"#": 1, "B": 2, "^": 3, "S": 4, "T": 5, "o": 6}
ENEMY = {"g": 1, "b": 2, "h": 3}
# Levels named "lab..." are test levels: moved to the end and only built into the simulator.
levels, labs, cur = [], [], None
for line in open("levels.txt").read().split("\n"):
    if line.startswith("="):
        cur = []
        (labs if line[1:].strip().startswith("lab") else levels).append(cur)
    elif not line.startswith(";") and cur is not None:
        cur.append(line.rstrip())
nlv = len(levels)
levels += labs
out, total = [], 0
for li, rows in enumerate(levels):
    while rows and not rows[-1].strip(): rows.pop()
    assert len(rows) <= 32, (li, len(rows))
    rows = [""] * (32 - len(rows)) + rows
    w = max(len(r) for r in rows)
    assert w <= 256
    g = [list(r.ljust(w)) for r in rows]
    data, start, goal = [], None, None
    for y in range(32):
        for x in range(w):
            c = g[y][x]
            if c == "@": start = (x, y)
            elif c == "F": goal = (x, y)
            elif c in ENEMY: data += [x, y | 7 << 5, ENEMY[c], 1]
    assert start and goal, li
    for ch, t in TILES.items():   # greedy rectangles: extend right, then down
        used = [[False] * w for _ in range(32)]
        for y in range(32):
            for x in range(w):
                if g[y][x] != ch or used[y][x]: continue
                x2 = x
                while x2 + 1 < w and g[y][x2 + 1] == ch and not used[y][x2 + 1]: x2 += 1
                y2 = y
                while y2 + 1 < 32 and all(g[y2 + 1][i] == ch and not used[y2 + 1][i] for i in range(x, x2 + 1)): y2 += 1
                for yy in range(y, y2 + 1):
                    for xx in range(x, x2 + 1): used[yy][xx] = True
                data += [x, y | t << 5, x2 - x + 1, y2 - y + 1]
    data = [start[0], start[1] - 1] + data + [goal[0], goal[1], w, 0]
    total += len(data) * (li < nlv)
    out.append("static const unsigned char L%d[]={%s};" % (li, ",".join(map(str, data))))
    if li >= nlv: out[-1] = "#ifdef SIM\n" + out[-1] + "\n#endif"
out.append("static const unsigned char *const LV[]={%s\n#ifdef SIM\n%s\n#endif\n};" % (
    "".join("L%d," % i for i in range(nlv)), "".join("L%d," % i for i in range(nlv, len(levels)))))
out.append("#define NLV %d" % nlv)
open("levels.h", "w").write("\n".join(out) + "\n")
print("levels:", nlv, "+", len(levels) - nlv, "lab, bytes:", total)
