# Route finder for TAS proofs: a beam search over short input "macros" (runs, jumps of several
# lengths, long jumps, dives, cap combos, tube entries, waits) in the simulator, aiming at a list
# of waypoints. Every candidate is replayed from the start, so the result is an ordinary .tas file.
# usage: python3 tools/route.py LEVEL out.tas [prefix.tas] [beam=24] [steps=200]
#                               [wp=X,Y[,ROOM] ...]   (px; the last waypoint is the flag by default)
# It prints its progress and writes the best route so far after every step.
import os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIM = os.path.join(ROOT, "sim")
lvl, out = sys.argv[1], sys.argv[2]
prefix, beam, steps, wps = [], 24, 200, []
for a in sys.argv[3:]:
    if a.startswith("beam="): beam = int(a[5:])
    elif a.startswith("steps="): steps = int(a[6:])
    elif a.startswith("wp="):
        v = list(map(int, a[3:].split(","))); wps.append((v[0], v[1], v[2] if len(v) > 2 else 0))
    else: prefix = [l.split() for l in open(a) if l.strip() and not l.startswith("#")]

LAND = 120   # "until landing" tail
MACROS = [[("R", n)] for n in (4, 10, 24)] + [[("L", n)] for n in (4, 12)] + [[("-", n)] for n in (8, 24, 48)]
for h in (1, 3, 6, 9, 12, 16, 20, 26, 32, 40):
    MACROS += [[("RJ", h), ("R", LAND)], [("RJ", h), ("-", LAND)], [("LJ", h), ("L", LAND)], [("J", h), ("-", LAND)],
               [("RJ", h), ("L", LAND)], [("LJ", h), ("R", LAND)]]
for h in (2, 4, 6, 9, 12):   # wall jumps: away from the wall, then steer back, drift or keep going
    for d, e in (("L", "R"), ("R", "L")):
        MACROS += [[(d + "J", h), (e, LAND)], [(d + "J", h), (d, 3), (e, LAND)], [(d + "J", h), ("-", LAND)], [(d + "J", h), (d, LAND)]]
for h in (2, 6, 12, 20):   # ground pounds (through bricks), then the high ground-pound jump comes from J after landing
    MACROS += [[("J", h), ("D", LAND)], [("RJ", h), ("D", LAND)], [("LJ", h), ("D", LAND)]]
MACROS += [[("-", 6), ("J", 30), ("-", LAND)], [("-", 6), ("RJ", 30), ("R", LAND)]]
MACROS += [[("RDJ", 1), ("RD", 8), ("R", LAND)], [("RDJ", 1), ("R", LAND)], [("LDJ", 1), ("L", LAND)],
           [("RJ", 10), ("RDC", 1), ("R", LAND)], [("RJ", 18), ("RDC", 1), ("R", LAND)],
           [("RJ", 12), ("RJC", 1), ("RJ", 8), ("RDC", 1), ("R", 20), ("RJ", 40), ("R", LAND)],
           [("RJ", 12), ("RJC", 1), ("RJ", 8), ("RDC", 1), ("R", 20), ("RJ", 16), ("RDC", 1), ("R", LAND)],
           [("DJ", 1), ("D", LAND)], [("UJ", 1), ("RU", 40), ("R", LAND)],
           [("D", 3), ("-", 44)], [("RD", 3), ("-", 44)], [("R", 1), ("-", 44)], [("U", 30), ("-", 44)],
           [("RDC", 1), ("RD", 20), ("R", 10)], [("C", 1), ("-", 30)]]

def simulate(seq):
    """Replays seq; returns (status, frames, rows) with one (x, y, gnd, st, room) per frame."""
    tas = "\n".join(f"{n} {k}" for k, n in seq) + "\n"
    fn = f"/tmp/claude-1000/route_{os.getpid()}_{id(seq)}_{os.urandom(4).hex()}.tas"
    open(fn, "w").write(tas)
    res = subprocess.run([SIM, lvl, fn, "trace"], capture_output=True, text=True, env={k: v for k, v in os.environ.items() if k != "SIM_RESPAWN"})
    os.unlink(fn)
    rows, status = [], "END"
    for l in res.stdout.splitlines():
        f = l.split()
        if f and f[0].isdigit():
            p = dict(kv.split("=") for kv in f[2:] if kv.count("=") == 1)
            rows.append((float(p["x"]), float(p["y"]), int(p["gnd"]), int(p["st"]), int(p.get("room", 0)), int(p.get("wall", 0))))
        elif f and f[0] in ("GOAL", "DIED"): status = f[0]
    return status, rows

def trim(seq, base, rows):
    """Cut the macro at its first landing (after leaving the ground), or where it first touches a
    wall in the air (a wall jump can follow), so routes are made of stable points."""
    total = sum(n for _, n in seq)
    air, cut = False, total
    for i in range(base, len(rows)):
        if not rows[i][2]: air = True
        if air and rows[i][2] and rows[i][3] < 12: cut = i + 1; break
        if air and i >= base + 3 and rows[i][5] and rows[i][3] == 0 and not rows[i][2]: cut = i + 1; break
    out, left = [], cut
    for k, n in seq:
        if left <= 0: break
        out.append((k, min(n, left))); left -= n
    return out, rows[:cut]

def score(rows, wpi):
    x, y, gnd, st, room, wall = rows[-1]
    tx, ty, tr = wps[wpi]
    d = abs(tx - x) + abs(ty - y) * 1.5 + (0 if room == tr else 4000)
    return -d - len(rows) * 0.02

def flag():
    lines = open(os.path.join(ROOT, "assets", "levels.txt")).read().split("\n")
    levels, labs, cur = [], [], None
    for l in lines:
        if l.startswith("="): cur = [[]]; (labs if l[1:].strip().startswith("lab") else levels).append(cur)
        elif l.startswith("+") and cur is not None: cur.append([])
        elif not l.startswith(";") and cur is not None: cur[-1].append(l.rstrip())
    rows = (levels + labs)[int(lvl)][0]
    while rows and not rows[-1].strip(): rows.pop()
    for i, r in enumerate(rows):
        if "F" in r: return r.index("F") * 8, (max(32, len(rows)) - len(rows) + i) * 8, 0

wps.append(flag())
start = [(k, int(n)) for n, k in prefix]
beamset = [(0.0, start, 0)]
best = None
pool = ThreadPoolExecutor(os.cpu_count())
for step in range(steps):
    cands = []
    for sc, seq, wpi in beamset:
        base = sum(n for _, n in seq)
        for m in MACROS: cands.append((seq, wpi, base, seq + m))
    results = list(pool.map(lambda c: (c, simulate(c[3])), cands))
    nxt, seen = [], set()
    for (seq, wpi, base, full), (status, rows) in results:
        if status == "GOAL":
            # keep only up to the goal frame
            k = next((i for i, r in enumerate(rows) if r[3] == 14), len(rows)) + 2   # through the frame the sim reports GOAL
            route, left = [], k
            for key, n in full:
                if left <= 0: break
                route.append((key, min(n, left))); left -= n
            if not best or sum(n for _, n in route) < sum(n for _, n in best): best = route
            continue
        if len(rows) <= base: continue
        n = len(rows)
        route, rows = trim(full, base, rows)
        if status == "DIED" and len(rows) >= n: continue   # died before the macro reached a stable point
        x, y, gnd, st, room, wall = rows[-1]
        if not gnd and st != 12 and not wall: continue
        w = wpi
        while w < len(wps) - 1 and abs(wps[w][0] - x) < 12 and abs(wps[w][1] - y) < 16 and wps[w][2] == room: w += 1
        key = (room, int(x) // 6, int(y) // 8, w, gnd)
        if key in seen: continue
        seen.add(key)
        nxt.append((score(rows, w) + w * 10000, route, w))
    if best:
        print(f"GOAL in {sum(n for _, n in best)} frames")
        open(out, "w").write("".join(f"{n} {k}\n" for k, n in best))
        break
    nxt.sort(key=lambda t: -t[0])
    beamset = nxt[:beam]
    if not beamset: print("stuck: no surviving candidates"); break
    sc, route, w = beamset[0]
    st, rows = simulate(route)
    print(f"step {step}: best x={rows[-1][0]:.0f} y={rows[-1][1]:.0f} room={rows[-1][4]} wp={w}/{len(wps)} frames={len(rows)} beam={len(beamset)}", flush=True)
    open(out, "w").write("".join(f"{n} {k}\n" for k, n in route))
