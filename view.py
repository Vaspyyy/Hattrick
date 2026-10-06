# Draws a level from assets/levels.txt with the hero path of a TAS run on top (needs Pillow).
# usage: python3 view.py LEVEL file.tas out.png [x0 x1] [room=N]   (x range in tiles; room=N: a bonus room)
import subprocess, sys
from PIL import Image, ImageDraw
lvl, tas, out = int(sys.argv[1]), sys.argv[2], sys.argv[3]
args = [a for a in sys.argv[4:] if "=" not in a]
area = int(next((a[5:] for a in sys.argv[4:] if a.startswith("room=")), 0))   # room=N draws bonus room N
levels, labs, cur = [], [], None   # same order as the game: lab levels last; each level is a list of areas
for line in open("assets/levels.txt").read().split("\n"):
    if line.startswith("="):
        cur = [[]]
        (labs if line[1:].strip().startswith("lab") else levels).append(cur)
    elif line.startswith("+") and cur is not None: cur.append([])
    elif not line.startswith(";") and cur is not None:
        cur[-1].append(line.rstrip())
levels += labs
rows = levels[lvl][area]
while rows and not rows[-1].strip(): rows.pop()
rows = [""] * (32 - len(rows)) + rows
w = max(len(r) for r in rows)
x0, x1 = (int(args[0]), int(args[1])) if len(args) > 1 else (0, w)
S = 2
COL = {"#": (168, 100, 44), "B": (208, 96, 42), "^": (224, 230, 238), "S": (144, 152, 168), "T": (232, 64, 58),
       "o": (255, 216, 74), "g": (154, 72, 208), "b": (255, 210, 60), "h": (255, 150, 60), "@": (255, 122, 28),
       "F": (46, 200, 90), "/": (139, 224, 90), "\\": (139, 224, 90), "|": (200, 144, 46), "-": (200, 144, 46),
       "M": (240, 200, 96), "C": (226, 181, 106), "?": (94, 154, 166), "*": (200, 80, 28), "%": (200, 80, 28),
       "~": (255, 160, 40), ":": (255, 160, 40), "!": (255, 160, 40), "K": (47, 208, 180), "n": (47, 143, 154),
       "m": (224, 88, 106)}
for c in "0123456789": COL[c] = (240, 200, 96)
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
            if c in "ogbh@FKnm~:!": d.ellipse(r, COL[c])
            elif c == "?": d.rectangle(r, None, COL[c])
            elif c == "^": d.polygon([(r[0], r[3]), ((r[0] + r[2]) // 2, r[1]), (r[2], r[3])], COL[c])
            elif c in "/\\": d.polygon([(r[0], r[3]), (r[2], r[3]), (r[2] if c == "/" else r[0], r[1])], COL[c])
            else: d.rectangle(r, COL[c])
res = subprocess.run(["./sim", str(lvl), tas, "trace"], capture_output=True, text=True).stdout
STC = [(255, 255, 255), (0, 255, 255), (255, 0, 255), (255, 0, 255), (255, 0, 255), (255, 255, 0), (255, 255, 0),
       (255, 160, 0), (160, 160, 255), (100, 240, 200), (240, 200, 120), (200, 240, 120), (120, 120, 120), (255, 0, 0), (0, 255, 0)]
last = None
for l in res.splitlines():
    f = l.split()
    if f and f[0].isdigit():
        p = dict(kv.split("=") for kv in f[2:])
        X, Y, st = float(p["x"]) + 3, float(p["y"]) + 5.5, int(p["st"])
        if int(p.get("room", 0)) != area: last = None; continue
        q = ((X - x0 * 8) * S, Y * S)
        if last: d.line([last, q], STC[st], 1)
        if int(f[0]) % 30 == 0: d.text((q[0] + 2, q[1] - 12), f[0], (0, 0, 0))
        last = q
    elif l: print(l)
im.save(out)
