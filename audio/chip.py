# Chiptune versions of the Hatrick themes, for bg=8bit areas (gimmicks.h): the same melody,
# chords and loop length, played on NES-style channels. Writes assets/music/<theme>-8bit/ with
# the usual stems, so the reactive layers still work: lead (pulse 25%), bass (triangle),
# perc (noise drums), fast (busier noise hats), arp (pulse 12.5%), bah (a pulse stab on the
# hop beats), and a copy of music.txt. usage: python3 chip.py   (needs numpy and ffmpeg)
import os
import numpy as np
from studio import SR, ASSETS, save_ogg, hz
import songs

def pulse(f, n, duty):
    ph = (np.arange(n) * f / SR) % 1.0
    return np.where(ph < duty, 1.0, -1.0)

def triangle(f, n):   # the NES triangle: 32 steps
    ph = (np.arange(n) * f / SR) % 1.0
    return np.round((1 - 4 * np.abs(ph - 0.5)) * 7.5) / 7.5

def noise(n, short=False, seed=1):   # a 15-bit LFSR, like the NES noise channel
    r, out, period = 1, np.empty(n), 8 if short else 40
    for i in range(0, n, period):
        bit = (r ^ (r >> (6 if short else 1))) & 1
        r = (r >> 1) | (bit << 14)
        out[i:i + period] = 1.0 if r & 1 else -1.0
    return out

def place(x, s, y):
    s = int(s)
    if s >= len(x): return
    x[s:s + len(y)] += y[:len(x) - s]

def voice(notes, spb, L, wave, vol, decay=6.0):   # notes (beat, beats, pitch, vel)
    x = np.zeros(L + 4 * SR)
    for b, d, p, v in notes:
        n = max(int(d * spb * SR), 200)
        env = np.exp(-np.arange(n) / SR * decay) * 0.6 + 0.4
        env[-min(80, n):] *= np.linspace(1, 0, min(80, n))
        place(x, b * spb * SR, wave(hz(p), n) * env * vol * v)
    return x

def drums(times, spb, L, short, length, vol):
    x = np.zeros(L + 4 * SR); n = int(length * SR)
    hit = noise(n, short) * np.exp(-np.arange(n) / SR * 30) * vol
    for b in times: place(x, b * spb * SR, hit)
    return x

def fold(x, L):
    out = np.zeros(L)
    for s in range(0, len(x), L): seg = x[s:s + L]; out[:len(seg)] += seg
    out *= 0.67   # about as loud as the original themes
    return np.stack([out, out], 1).astype(np.float32)

for theme in songs.THEMES:
    t = theme(); spb = 60 / t["bpm"]; bars, ch = t["bars"], t["chords"]
    L = int(round(bars * 4 * spb * SR))
    mel = [(b, d, p, 0.9) for b, d, p in t["mel"]]
    stems = {
        "lead": voice(songs.stacc(mel, 0.85), spb, L, lambda f, n: pulse(f, n, 0.25), 0.16),
        "bass": voice(songs.bass_line(ch, bars), spb, L, triangle, 0.32, 2.0),
        "arp": voice(songs.arpeggio(ch, bars, t["key_lo"], t["key_hi"] + 5, 0.8), spb, L, lambda f, n: pulse(f, n, 0.125), 0.07, 14.0),
        "perc": drums([bar * 4 + s / 4 for bar in range(bars) for s in (0, 8)], spb, L, False, 0.12, 0.30) +
                drums([bar * 4 + s / 4 for bar in range(bars) for s in (4, 12)], spb, L, False, 0.09, 0.22) +
                drums([bar * 4 + s / 4 for bar in range(bars) for s in range(2, 16, 4)], spb, L, True, 0.03, 0.10),
        "fast": drums([bar * 4 + s / 4 for bar in range(bars) for s in range(1, 16, 2)], spb, L, True, 0.025, 0.09),
    }
    bah = np.zeros(L + 4 * SR)
    for b, ps in songs.bah_notes(ch, t["bahs"]):
        n = int(0.22 * SR)
        y = sum(pulse(hz(p), n, 0.5) for p in ps) / len(ps) * np.exp(-np.arange(n) / SR * 9) * 0.16
        place(bah, b * spb * SR, y)
    stems["bah"] = bah
    out = os.path.join(ASSETS, "music", t["name"] + "-8bit")
    for name, x in stems.items(): save_ogg(os.path.join(out, name + ".ogg"), fold(x, L))
    src = os.path.join(ASSETS, "music", t["name"], "music.txt")
    if os.path.exists(src):
        with open(src) as f, open(os.path.join(out, "music.txt"), "w") as g: g.write(f.read())
    print(t["name"] + "-8bit", f"{L / SR:.1f} s loop")
