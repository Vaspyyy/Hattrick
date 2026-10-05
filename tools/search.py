# Random/parallel TAS search over the simulator, used to prove and stress-test levels.
# usage: python3 tools/search.py LEVEL base.tas "template" a=lo:hi[:step] ... [n=N] [x=lo:hi] [y=Y]
#        [touch=x0:x1:y] [show=K]
# The template is appended to base.tas ("-" for none) with {a}, {b}, ... filled from the ranges
# ("\n" separates lines; lines with 0 frames are dropped). A run succeeds on GOAL, or when it ends
# alive with x in range (and y equal, standing), or with touch: when it stands in that x range at y
# at any frame. Prints how many sampled combinations succeed and the first few.
import itertools, os, random, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
lvl, base, tpl = sys.argv[1], sys.argv[2], sys.argv[3].replace("\\n", "\n")
rng, n, xr, yy, show, touch = {}, 3000, None, None, 10, None
for a in sys.argv[4:]:
    k, v = a.split("=")
    if k == "n": n = int(v)
    elif k == "x": xr = tuple(map(int, v.split(":")))
    elif k == "y": yy = int(v)
    elif k == "show": show = int(v)
    elif k == "touch": touch = tuple(map(int, v.split(":")))
    else:
        p = list(map(int, v.split(":"))); rng[k] = range(p[0], p[1] + 1, p[2] if len(p) > 2 else 1)
B = open(base).read() if base != "-" else ""
combos = list(itertools.product(*rng.values()))
combos = combos if len(combos) <= n else random.sample(combos, n)

def run(item):
    i, combo = item
    d = dict(zip(rng, combo))
    t = "\n".join(l for l in (B + tpl.format(**d) + "\n").split("\n") if not re.match(r"^\s*0 ", l))
    fn = f"/tmp/hatrick_search_{os.getpid()}_{i}.tas"
    open(fn, "w").write(t)
    out = subprocess.run([os.path.join(ROOT, "sim"), lvl, fn] + (["trace"] if touch else []),
                         capture_output=True, text=True).stdout.strip().split("\n")
    os.unlink(fn)
    o = out[-1]; f = o.split()
    if touch:
        for l in out:
            if " gnd=1 " in l:
                p = dict(kv.split("=") for kv in l.split()[2:] if kv.count("=") == 1)
                if touch[0] <= float(p["x"]) <= touch[1] and int(float(p["y"])) == touch[2]: return True, d, l[:70]
        return False, d, o
    if f[0] == "GOAL": return True, d, o
    if f[0] == "END":
        x, y = int(f[4][2:]), int(f[5][2:])
        return (xr is None or xr[0] <= x <= xr[1]) and (yy is None or y == yy), d, o
    return False, d, o

with ThreadPoolExecutor(os.cpu_count()) as ex: res = list(ex.map(run, enumerate(combos)))
good = sorted((r for r in res if r[0]), key=lambda r: r[2])
print(f"{len(good)}/{len(res)} ok")
for _, d, o in good[:show]: print(d, o)
