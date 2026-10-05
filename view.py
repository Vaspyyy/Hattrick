# Draws a level from levels.txt with the hero path of a TAS run on top (needs Pillow).
# usage: python3 view.py LEVEL file.tas out.png [x0 x1]   (x range in tiles, default whole level)
import subprocess, sys
from PIL import Image, ImageDraw
lvl, tas, out = int(sys.argv[1]), sys.argv[2], sys.argv[3]
levels, labs, cur = [], [], None   # same order as mklevels.py: lab levels last
for line in open("levels.txt").read().split("\n"):
    if line.startswith("="):
        cur = []
        (labs if line[1:].strip().startswith("lab") else levels).append(cur)
    elif not line.startswith(";") and cur is not None:
        cur.append(line.rstrip())
levels += labs
rows = levels[lvl]
while rows and not rows[-1].strip(): rows.pop()
rows = [""] * (32 - len(rows)) + rows
w = max(len(r) for r in rows)
x0, x1 = (int(sys.argv[4]), int(sys.argv[5])) if len(sys.argv) > 5 else (0, w)
S = 2
COL = {"#": (168, 100, 44), "B": (208, 96, 42), "^": (224, 230, 238), "S": (144, 152, 168), "T": (232, 64, 58),
       "o": (255, 216, 74), "g": (154, 72, 208), "b": (255, 210, 60), "h": (255, 150, 60), "@": (255, 122, 28),
       "F": (46, 200, 90)}
im = Image.new("RGB", ((x1 - x0) * 8 * S, 256 * S), (74, 160, 255))
d = ImageDraw.Draw(im)
for i in range(x0, x1):
    if i % 8 == 0: d.line([((i - x0) * 8 * S, 0), ((i - x0) * 8 * S, 256 * S)], (90, 175, 255))
    if i % 8 == 0: d.text(((i - x0) * 8 * S + 2, 2), str(i), (255, 255, 255))
for y in range(0, 32, 4): d.text((2, y * 8 * S + 2), str(y), (255, 255, 255))
for y in range(32):
    for x in range(x0, min(x1, w)):
        c = rows[y][x] if x < len(rows[y]) else " "
        if c in COL:
            r = [((x - x0) * 8) * S, y * 8 * S, ((x - x0) * 8 + 7) * S, (y * 8 + 7) * S]
            if c in "ogbh@F": d.ellipse(r, COL[c])
            elif c == "^": d.polygon([(r[0], r[3]), ((r[0] + r[2]) // 2, r[1]), (r[2], r[3])], COL[c])
            else: d.rectangle(r, COL[c])
res = subprocess.run(["./sim", str(lvl), tas, "trace"], capture_output=True, text=True).stdout
STC = [(255, 255, 255), (0, 255, 255), (255, 0, 255), (255, 0, 255), (255, 0, 255), (255, 255, 0), (255, 255, 0),
       (255, 0, 0), (0, 255, 0)]
last = None
for l in res.splitlines():
    f = l.split()
    if f and f[0].isdigit():
        p = dict(kv.split("=") for kv in f[2:])
        X, Y, st = float(p["x"]) + 3, float(p["y"]) + 5.5, int(p["st"])
        q = ((X - x0 * 8) * S, Y * S)
        if last: d.line([last, q], STC[st], 1)
        if int(f[0]) % 30 == 0: d.text((q[0] + 2, q[1] - 12), f[0], (0, 0, 0))
        last = q
    elif l: print(l)
im.save(out)
