# Replays .tas files on the real game through xdotool keydown/keyup (e.g. under Xvfb).
# usage: DISPLAY=:99 python3 play.py [-shots DIR] tas/1.tas [tas/2.tas ...]
# -shots saves a screenshot after each level's last input (needs python-xlib and Pillow).
# Each file starts with an R press (restart: the level and its enemy timers reset), so the
# run lines up with ./sim. One xdotool process reads commands from stdin and every input
# change is sent at its absolute frame time, so timing does not drift over a level. Before
# the first level the screen is watched for redraws to find the game's frame phase, and
# inputs are then sent mid-frame, well away from the moment the game polls the keyboard.
import cmath, math, os, re, subprocess, sys, time
KEYS = {"L": "Left", "R": "Right", "U": "Up", "D": "Down", "J": "z", "C": "x"}
FRAME = 1 / 60

def goal_frame(path):   # stop sending input once ./sim says the flag is reached
    lvl = int(re.findall(r"\d+", os.path.basename(path))[0]) - 1
    out = subprocess.run(["./sim", str(lvl), path], capture_output=True, text=True).stdout
    m = re.search(r"GOAL frame (\d+)", out)
    return int(m.group(1)) + 30 if m else 1 << 30   # a little slack: input is ignored after the goal

def wait_until(t):
    while (d := t - time.perf_counter()) > 0.002: time.sleep(d - 0.002)
    while time.perf_counter() < t: pass

def frame_anchor(secs=1.5):   # when the game polls input (its frames are exact 60 Hz); inputs go half a frame later
    try:
        from Xlib import X, display
        root = display.Display().screen().root
        g = root.get_geometry()
        prev, hits, end = None, [], time.perf_counter() + secs
        while time.perf_counter() < end:
            img = root.get_image(0, 0, g.width, g.height, X.ZPixmap, 0xffffffff).data
            t = time.perf_counter()
            if prev is not None and img != prev: hits.append(t)
            prev = img
        if len(hits) < 5: return None
        ang = sum(cmath.exp(2j * math.pi * (t % FRAME) / FRAME) for t in hits)   # circular mean
        return (cmath.phase(ang) / (2 * math.pi) % 1) * FRAME   # the keyboard poll flushes the previous image, so it happens about here
    except Exception as e:
        print("no frame sync:", e)
        return None

def run(xdo, path, anchor):
    stop, events, held, frames = goal_frame(path), [], set(), 0
    for line in open(path):
        if not line.strip() or frames >= stop: continue
        n, k = line.split()
        want = {KEYS[c] for c in k if c in KEYS}
        up, down = sorted(held - want), sorted(want - held)   # --delay 0: xdotool waits 12 ms per key otherwise
        events.append((frames, ["keyup --delay 0 " + " ".join(up)] * bool(up) + ["keydown --delay 0 " + " ".join(down)] * bool(down)))
        held, frames = want, min(frames + int(n), stop)
    events.append((frames, ["keyup --delay 0 " + " ".join(sorted(held))] * bool(held)))
    t0 = time.perf_counter() + 0.05
    if anchor is not None: t0 += (anchor + FRAME / 2 - t0) % FRAME   # mid-frame
    wait_until(t0)
    xdo.stdin.write("keydown --delay 0 r\n"); xdo.stdin.flush()
    for f, cmds in events:
        wait_until(t0 + (f + 1) * FRAME)
        xdo.stdin.write("".join(c + "\n" for c in cmds + (["keyup --delay 0 r"] if f == 0 else []))); xdo.stdin.flush()
    print(f"{path}: {frames} frames in {time.perf_counter() - t0:.2f}s (ideal {(frames + 1) * FRAME:.2f}s)")

def shot(path):   # screenshot taken while the goal animation plays, not during the run
    from Xlib import X, display
    from PIL import Image
    root = display.Display().screen().root
    g = root.get_geometry()
    img = root.get_image(0, 0, g.width, g.height, X.ZPixmap, 0xffffffff)
    Image.frombytes("RGB", (g.width, g.height), img.data, "raw", "BGRX").save(path)

args, shots = sys.argv[1:], None
if args[:1] == ["-shots"]: shots, args = args[1], args[2:]
anchor = frame_anchor()
print("frame phase:", "unknown" if anchor is None else f"{anchor * 1000:.1f} ms")
xdo = subprocess.Popen(["xdotool", "-"], stdin=subprocess.PIPE, text=True)
for p in args:
    run(xdo, p, anchor)
    time.sleep(0.3)
    if shots: shot(os.path.join(shots, os.path.basename(p) + ".png"))
    time.sleep(1.7)   # goal animation, then the next level loads
xdo.stdin.close(); xdo.wait()
