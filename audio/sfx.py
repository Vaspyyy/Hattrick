# Hatrick sound effects, built from the VSCO 2 CE samples (CC0) plus simple synthesis.
import numpy as np
from studio import *
from songs import insts

def sample(name, pitch=60, vel=0.8, dur=0.3):
    return insts()[name].note(pitch, dur, vel)

def bend(x, r0, r1, sec):   # play x with its rate gliding from r0 to r1 over sec (pitch bend)
    n = len(x); rate = np.interp(np.arange(n), [0, sec * SR], [r0, r1])
    pos = np.cumsum(rate); pos = pos[pos < n - 1]
    i = pos.astype(np.int64); f = (pos - i)[:, None].astype(np.float32)
    return x[i] * (1 - f) + x[i + 1] * f

def sweep(f0, f1, sec, curve=1.0):   # sine glide, curve > 1 bends late
    t = t_axis(sec); f = f0 + (f1 - f0) * (t / sec) ** curve
    return np.sin(2 * np.pi * np.cumsum(f) / SR).astype(np.float32)

def env(sec, attack=0.003, decay=0.1):
    t = t_axis(sec)
    return (np.clip(t / attack, 0, 1) * np.exp(-t / decay)).astype(np.float32)

def noise(sec, seed=0): return np.random.default_rng(seed).standard_normal(int(sec * SR)).astype(np.float32)

def band(x, f0, f1, bw):   # noise through a band-pass gliding from f0 to f1
    n = len(x); return resonate(x, np.geomspace(f0, f1, n), np.full(n, bw)).astype(np.float32)

def mix(*parts):   # (offset seconds, stereo array, gain)
    n = max(int(o * SR) + len(x) for o, x, g in parts)
    out = np.zeros((n, 2), np.float32)
    for o, x, g in parts:
        s = int(o * SR); out[s:s + len(x)] += x * g
    return out

def norm(x, peak): return (x * (peak / (np.abs(x).max() + 1e-9))).astype(np.float32)

def fadeout(x, sec=0.03):
    n = int(sec * SR); x = x.copy(); x[-n:] *= np.linspace(1, 0, n)[:, None]; return x

# ---------- the effects ----------
def coin():
    return mix((0, sample("glock", 91, 0.9, 0.6), 1.0), (0, sample("glock", 96, 0.7, 0.6), 0.7),
               (0.045, sample("glock", 100, 0.8, 0.5), 0.8), (0.01, sample("triangle", 0, 0.5), 0.35))

def jump(level=0):
    wood = bend(sample("loghi", 64 + level * 4, 0.9, 0.25), 0.85, 1.5 + level * 0.2, 0.12)
    body = stereo(sweep(330 * 1.12 ** level, 760 * 1.12 ** level, 0.14, 0.7) * env(0.14, 0.002, 0.05))
    parts = [(0, wood, 1.0), (0, body, 0.35)]
    if level == 2:   # triple jump: a little glockenspiel sparkle on top
        parts += [(0.05 + i * 0.045, sample("glock", p, 0.6, 0.3), 0.5) for i, p in enumerate((84, 88, 91, 96))]
    return fadeout(mix(*parts))

def stomp():
    thump = stereo(sweep(120, 42, 0.22, 0.5) * env(0.22, 0.001, 0.07))
    squish = stereo(band(noise(0.12, 3), 900, 300, 400) * env(0.12, 0.001, 0.03))
    x = mix((0, sample("conga", 60, 1.0), 0.8), (0, thump, 1.0), (0, squish, 0.5), (0.0, sample("slap", 62, 1.0), 0.6))
    return np.tanh(norm(x, 1.4)).astype(np.float32)   # a little saturation for punch

def cap_throw():
    t = t_axis(0.38)
    flutter = 0.55 + 0.45 * np.sin(2 * np.pi * 21 * t)                       # the spinning cap
    w = band(noise(0.38, 5), 600, 2800, 900) * flutter * env(0.38, 0.03, 0.14)
    return fadeout(mix((0, stereo(w, -0.3), 1.0), (0.02, stereo(sweep(700, 1500, 0.2) * env(0.2, 0.005, 0.06), 0.3), 0.12)))

def cap_catch():
    fwip = band(noise(0.08, 7), 2600, 900, 700) * env(0.08, 0.004, 0.02)
    return mix((0, stereo(fwip), 0.6), (0.03, sample("claves", 64, 0.9), 0.8), (0.035, sample("glock", 91, 0.6, 0.2), 0.45))

def cap_bounce():
    boing = stereo(sweep(240, 900, 0.16, 0.6) * env(0.16, 0.002, 0.07))
    return fadeout(mix((0, boing, 0.5), (0, bend(sample("marimba", 72, 0.9, 0.3), 0.9, 1.35, 0.1), 1.0),
                       (0.04, sample("glock", 88, 0.6, 0.3), 0.4)))

def dive():
    w = band(noise(0.32, 9), 350, 1400, 500) * env(0.32, 0.02, 0.1)
    return fadeout(mix((0, stereo(w), 1.0), (0, bend(sample("loglo", 60, 0.7, 0.2), 1.1, 0.8, 0.15), 0.5)))

def gp_spin():
    t = t_axis(0.26)
    w = band(noise(0.26, 11), 900, 2400, 700) * (0.5 + 0.5 * np.sin(2 * np.pi * 34 * t)) * env(0.26, 0.04, 0.12)
    return fadeout(mix((0, stereo(w), 1.0)))

def gp_land():
    boom = stereo(sweep(78, 34, 0.45, 0.6) * env(0.45, 0.001, 0.16))
    crunch = stereo(band(noise(0.2, 13), 1800, 500, 900) * env(0.2, 0.001, 0.05))
    x = mix((0, boom, 1.0), (0, sample("loglo", 52, 1.0, 0.4), 0.9), (0, crunch, 0.45), (0, sample("conga", 48, 1.0), 0.5))
    return np.tanh(norm(room(x, 0.5, 0.15), 1.3)).astype(np.float32)

def brick():
    r = np.random.default_rng(17)
    parts = [(0, stereo(band(noise(0.25, 19), 2600, 700, 1200) * env(0.25, 0.001, 0.05)), 0.8),
             (0, stereo(sweep(140, 60, 0.12) * env(0.12, 0.001, 0.04)), 0.6)]
    for i in range(6):   # falling debris clacks
        parts.append((0.02 + i * 0.035 + r.uniform(0, 0.02), sample("claves" if i % 2 else "loghi", 60 + r.integers(-5, 8), 0.6), 0.45 - i * 0.05))
    return fadeout(mix(*parts))

def spring():
    t = t_axis(0.6)
    f = 230 * (1 + 0.35 * np.exp(-t / 0.08)) * (1 + 0.12 * np.sin(2 * np.pi * 16 * t) * np.exp(-t / 0.25)) * (1 + 0.6 * t)
    y = np.sin(2 * np.pi * np.cumsum(f) / SR + 1.6 * np.sin(2 * np.pi * np.cumsum(f * 2) / SR) * np.exp(-t / 0.2))
    return fadeout(mix((0, stereo((y * env(0.6, 0.002, 0.18)).astype(np.float32)), 0.7), (0, sample("marimba", 79, 0.8, 0.2), 0.5)))

def wall_jump():
    return fadeout(mix((0, sample("loglo", 62, 0.9, 0.2), 0.9), (0.01, bend(sample("loghi", 67, 0.8, 0.2), 0.9, 1.4, 0.1), 0.7),
                       (0.02, stereo(band(noise(0.15, 23), 800, 2000, 600) * env(0.15, 0.01, 0.05)), 0.4)))

def land():
    return fadeout(mix((0, stereo(band(noise(0.1, 29), 500, 250, 300) * env(0.1, 0.001, 0.025)), 0.8),
                       (0, stereo(sweep(130, 70, 0.08) * env(0.08, 0.001, 0.03)), 0.5)))

def skid():
    t = t_axis(0.3)
    s = band(noise(0.3, 31), 1400, 900, 600) * (0.6 + 0.4 * np.sign(np.sin(2 * np.pi * 40 * t))) * env(0.3, 0.01, 0.12)
    return fadeout(mix((0, stereo(s), 1.0)))

def roll():   # a tumbling, woody rumble
    t = t_axis(0.32)
    r = band(noise(0.32, 37), 260, 520, 260) * (0.55 + 0.45 * np.sin(2 * np.pi * 17 * t)) * env(0.32, 0.01, 0.12)
    return fadeout(mix((0, stereo(r), 1.0), (0, sample("loglo", 55, 0.8, 0.2), 0.6), (0.09, sample("loglo", 52, 0.6, 0.2), 0.4)))

def spin():   # twirl: rising fluttering whoosh with a tiny bell on top
    t = t_axis(0.3)
    w = band(noise(0.3, 41), 700, 2600, 800) * (0.5 + 0.5 * np.sin(2 * np.pi * 38 * t)) * env(0.3, 0.03, 0.11)
    return fadeout(mix((0, stereo(w), 1.0), (0.04, sample("glock", 96, 0.5, 0.25), 0.35)))

def longjump():   # low woody push-off plus a long airy swoosh
    w = band(noise(0.4, 43), 450, 1900, 700) * env(0.4, 0.02, 0.15)
    return fadeout(mix((0, bend(sample("loglo", 60, 0.9, 0.25), 0.9, 1.25, 0.12), 0.8), (0.01, stereo(w), 0.8)))

def flip():   # backflip / side flip: springy wood, a spin swish and two sparkles
    t = t_axis(0.35)
    w = band(noise(0.35, 47), 900, 2200, 700) * (0.5 + 0.5 * np.sin(2 * np.pi * 26 * t)) * env(0.35, 0.03, 0.12)
    return fadeout(mix((0, jump(1), 0.9), (0.03, stereo(w), 0.6), (0.12, sample("glock", 91, 0.5, 0.2), 0.35),
                       (0.17, sample("glock", 96, 0.5, 0.2), 0.3)))

def ledge():   # grabbing a ledge: a soft knock and a cloth rustle
    rustle = band(noise(0.09, 53), 1500, 700, 600) * env(0.09, 0.003, 0.025)
    return fadeout(mix((0, sample("claves", 55, 0.5), 0.6), (0, stereo(rustle), 0.5)))

def menu_move(): return fadeout(mix((0, sample("marimba", 79, 0.7, 0.12), 1.0)))
def menu_ok():
    return fadeout(mix((0, sample("marimba", 72, 0.9, 0.12), 0.9), (0.07, sample("marimba", 79, 0.9, 0.15), 0.9),
                       (0.07, sample("glock", 96, 0.7, 0.4), 0.5)))
def menu_back(): return fadeout(mix((0, sample("marimba", 79, 0.8, 0.12), 0.9), (0.07, sample("marimba", 72, 0.8, 0.15), 0.9)))
def pause():
    return fadeout(mix(*[(i * 0.03, sample("marimba", p, 0.8, 0.3), 0.7) for i, p in enumerate((72, 76, 79))],
                       (0.09, sample("glock", 91, 0.5, 0.5), 0.4)))

def checkpoint():   # a rising four-note chime with a shimmer: "saved"
    notes = [(i * 0.06, sample("glock", p, 0.75, 0.6), 0.75 - i * 0.05) for i, p in enumerate((84, 88, 91, 96))]
    return fadeout(mix(*notes, (0, sample("marimba", 60, 0.8, 0.4), 0.6), (0.18, sample("belltree", 0, 0.5), 0.35)), 0.15)

def tube():   # a hollow brass "whoomp" falling into the tube, with a wobbly resonance
    t = t_axis(0.42)
    f = 260 * np.exp(-t / 0.18) + 95
    body = np.sin(2 * np.pi * np.cumsum(f) / SR) * (1 + 0.25 * np.sin(2 * np.pi * 11 * t))
    air = band(noise(0.42, 59), 1600, 400, 500)
    return fadeout(mix((0, stereo((body * env(0.42, 0.01, 0.14)).astype(np.float32)), 0.8),
                       (0, stereo(air * env(0.42, 0.02, 0.1)), 0.35), (0, sample("loglo", 50, 0.7, 0.3), 0.5)))

def crumble():   # a dry rattle of little knocks and grit
    r = np.random.default_rng(61)
    parts = [(0, stereo(band(noise(0.4, 67), 2200, 900, 900) * env(0.4, 0.01, 0.12) * (0.6 + 0.4 * np.sign(np.sin(2 * np.pi * 30 * t_axis(0.4))))), 0.5)]
    for i in range(7):
        parts.append((i * 0.045 + r.uniform(0, 0.015), sample("claves" if i % 2 else "loghi", 66 + r.integers(-4, 5), 0.35), 0.35 - i * 0.03))
    return fadeout(mix(*parts))

def reveal():   # a hidden block found: a bright marimba-and-bell sparkle upward
    notes = [(i * 0.05, sample("marimba", p, 0.85, 0.25), 0.8) for i, p in enumerate((72, 79, 84, 91))]
    return fadeout(mix(*notes, (0.15, sample("glock", 96, 0.7, 0.6), 0.6), (0.17, sample("glock", 103, 0.6, 0.5), 0.45),
                       (0.1, sample("belltree", 0, 0.6), 0.4)), 0.2)

def spit():   # "ptoo": a lip pop and a short airy puff
    pop = stereo(sweep(700, 220, 0.07, 0.5) * env(0.07, 0.001, 0.02))
    puff = stereo(band(noise(0.16, 71), 2400, 1200, 900) * env(0.16, 0.005, 0.05))
    return fadeout(mix((0, pop, 0.7), (0.01, puff, 0.5), (0, sample("slap", 70, 0.6), 0.5)))

def emerge():   # a soft wet "shlup" of something sliding out of a tube
    t = t_axis(0.2)
    w = band(noise(0.2, 73), 300, 1200, 300) * (0.6 + 0.4 * np.sin(2 * np.pi * 22 * t)) * env(0.2, 0.03, 0.07)
    return fadeout(mix((0, stereo(w), 1.0), (0.02, bend(sample("loglo", 64, 0.5, 0.15), 0.8, 1.3, 0.1), 0.35)))

def tick():   # one tick of the score tally: tiny and bright
    return fadeout(mix((0, sample("glock", 100, 0.5, 0.08), 0.6), (0, sample("claves", 76, 0.4), 0.3))[:int(0.09 * SR)], 0.04)

def bonus():   # the tally is done: two bell notes and a sparkle
    return fadeout(mix((0, sample("glock", 91, 0.8, 0.5), 0.7), (0.09, sample("glock", 96, 0.8, 0.6), 0.75),
                       (0.09, sample("marimba", 72, 0.8, 0.3), 0.5), (0.12, sample("triangle", 0, 0.5), 0.3)), 0.2)

# name -> (function, loudness relative to the others)
SFX = {"coin": (coin, 0.55), "jump": (jump, 0.6), "jump2": (lambda: jump(1), 0.62), "jump3": (lambda: jump(2), 0.65),
       "stomp": (stomp, 0.8), "cap_throw": (cap_throw, 0.5), "cap_catch": (cap_catch, 0.5), "cap_bounce": (cap_bounce, 0.6),
       "dive": (dive, 0.55), "gp_spin": (gp_spin, 0.45), "gp_land": (gp_land, 0.85), "brick": (brick, 0.7),
       "spring": (spring, 0.6), "wall_jump": (wall_jump, 0.6), "land": (land, 0.35), "skid": (skid, 0.4),
       "roll": (roll, 0.5), "spin": (spin, 0.45), "longjump": (longjump, 0.6), "flip": (flip, 0.62), "ledge": (ledge, 0.4),
       "menu_move": (menu_move, 0.4), "menu_ok": (menu_ok, 0.5), "menu_back": (menu_back, 0.45), "pause": (pause, 0.45),
       "checkpoint": (checkpoint, 0.55), "tube": (tube, 0.55), "crumble": (crumble, 0.4), "reveal": (reveal, 0.6),
       "spit": (spit, 0.45), "emerge": (emerge, 0.3), "tick": (tick, 0.3), "bonus": (bonus, 0.5)}

def build_all(outdir, only=None):
    out = {}
    for name, (fn, peak) in SFX.items():
        if only and name not in only: continue
        x = norm(fn(), peak); out[name] = x
        save_ogg(f"{outdir}/{name}.ogg", x)
    return out
