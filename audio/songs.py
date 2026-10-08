# Original Hatrick music, written as melody text + chord symbols; bass, percussion, bells,
# arpeggios and "bah" hits are generated from the chords in a bouncy Latin-pop style.
# Melody text: "pitch:length" in sixteenth notes, bars separated by "|", "r" is a rest.
import json, os
import numpy as np
from studio import *

# ---------- notation ----------
def line(text, bar0=0):
    notes, beat = [], bar0 * 4.0
    for i, bar in enumerate(text.split("|")):
        start = beat
        for tok in bar.split():
            n, d = tok.split(":"); d = int(d) / 4
            if n != "r": notes.append((beat, d, midi(n)))
            beat += d
        assert abs(beat - start - 4) < 1e-9, f"bar {bar0 + i + 1} has {(beat - start) * 4} sixteenths"
    return notes

QUAL = {"": (0, 4, 7), "m": (0, 3, 7), "7": (0, 4, 7, 10), "maj7": (0, 4, 7, 11), "m7": (0, 3, 7, 10),
        "sus": (0, 5, 7), "dim": (0, 3, 6)}

def chord(sym):   # "F#m7" -> (root pitch class, intervals)
    root = sym[:2] if len(sym) > 1 and sym[1] in "#b" else sym[:1]
    return NAMES[root], QUAL[sym[len(root):]]

def chord_at(chords, beat):   # chords: one entry per bar, "C" or "Dm7 G7" (two halves)
    bar = chords[int(beat // 4) % len(chords)].split()
    return chord(bar[0] if beat % 4 < 2 or len(bar) == 1 else bar[1])

def tones(c, lo, hi):   # chord tones between pitches lo and hi
    pc, iv = c
    return [p for p in range(lo, hi + 1) if (p - pc) % 12 in [i % 12 for i in iv]]

def near(c, center):   # chord root nearest a pitch
    pc, _ = c
    return min((p for p in range(center - 12, center + 12) if p % 12 == pc), key=lambda p: abs(p - center))

# ---------- pattern generators (16 steps per bar) ----------
def bass_line(chords, bars, center=43, pattern=((0, 3, "R"), (3, 1, "R"), (6, 2, "5"), (8, 3, "R"), (11, 1, "R"), (14, 2, "5"))):
    out = []
    for bar in range(bars):
        for step, ln, deg in pattern:
            b = bar * 4 + step / 4
            c = chord_at(chords, b); r = near(c, center)
            p = r + {"R": 0, "5": 7 if 7 in c[1] else 6, "8": 12, "3": c[1][1]}[deg]
            out.append((b, ln / 4 * 0.8, p, 0.85 if step in (0, 8) else 0.7))
    return out

def hits(bars, steps, pitch, vel, every=1, phase=0):   # repeated drum hits at given steps
    return [(bar * 4 + s / 4, 0.25, pitch, vel(s) if callable(vel) else vel)
            for bar in range(bars) if bar % every == phase for s in steps]

def clave(bars, three_two=True, son=True):   # 2-bar clave pattern
    a, b = ((0, 6, 12) if son else (0, 6, 13)), (4, 8)
    out = []
    for bar in range(bars):
        for s in (a if (bar % 2 == 0) == three_two else b): out.append((bar * 4 + s / 4, 0.25, 0, 0.7))
    return out

def arpeggio(chords, bars, lo, hi, vel=0.5, shape=(0, 1, 2, 3, 2, 1, 2, 3)):
    out = []
    for bar in range(bars):
        for s in range(16):
            b = bar * 4 + s / 4
            t = tones(chord_at(chords, b), lo, hi)
            out.append((b, 0.25, t[shape[s % len(shape)] % len(t)], vel * (1.15 if s % 4 == 0 else 1)))
    return out

def dings(chords, bars, lo, hi, every=1, vel=0.5):   # bell chord tones on the downbeat
    out = []
    for bar in range(0, bars, every):
        t = tones(chord_at(chords, bar * 4), lo, hi)
        out += [(bar * 4, 1.5, p, vel) for p in t[-2:]]
    return out

def bah_notes(chords, spots, lo=55, hi=70):   # (bar, step) -> group "bah" voiced on the coming chord
    out = []
    for bar, step in spots:
        b = bar * 4 + step / 4
        c = chord_at(chords, b + 0.5 if step >= 12 else b)
        out.append((b, tones(c, lo, hi)[-3:]))
    return out

def octave(notes, k=12, vel=None): return [(b, d, p + k, v if vel is None else vel) for b, d, p, v in notes]
def with_vel(notes, v): return [(b, d, p, v) for b, d, p in notes]
def stacc(notes, f): return [(b, d * f, p, v) for b, d, p, v in notes]

# Hatrick's original tune (from the first version of the game), 16 bars of sixteenths:
# 0 holds the previous note, 1 is a rest, anything else is a MIDI note.
OLD_TUNE = [
    67,0,0,72,0,0,76,0, 79,0,76,0,72,0,74,0,   76,0,0,72,0,0,69,0, 72,0,0,0,1,0,71,72,
    77,0,0,76,0,0,74,0, 72,0,69,0,72,0,77,0,   79,0,0,0,77,0,76,0, 74,0,0,0,1,0,67,0,
    72,0,0,76,0,0,79,0, 84,0,83,0,79,0,76,0,   81,0,0,79,0,0,76,0, 72,0,0,0,74,0,76,0,
    77,0,74,0,77,0,81,0, 79,0,77,0,74,0,71,0,  72,0,0,67,0,0,72,0, 72,0,1,0,0,0,0,0,
    81,0,79,0,77,0,0,76, 0,0,77,0,81,0,0,0,    83,0,81,0,79,0,0,77, 0,0,79,0,83,0,0,0,
    84,0,0,83,0,0,79,0, 76,0,0,0,79,0,83,0,    84,0,0,0,81,0,0,0, 76,0,0,0,1,0,0,0,
    77,0,81,0,86,0,84,0, 81,0,77,0,74,0,77,0,  79,0,83,0,86,0,84,0, 83,0,79,0,74,0,71,0,
    72,0,76,0,79,0,84,0, 88,0,0,0,84,0,0,0,    86,0,0,0,83,0,0,0, 79,0,74,0,71,0,74,0,
]
def old_tune():
    out, cur = [], None
    for i, m in enumerate(OLD_TUNE + [1]):
        if m:
            if cur: out.append((cur[0] / 4, (i - cur[0]) / 4, cur[1]))
            cur = (i, m) if m > 1 else None
    return out

# ---------- themes ----------
def overworld():
    chords = ["C", "Am", "F", "G", "C", "Am", "Dm7 G7", "C",
              "F", "G", "Em", "Am", "Dm", "G", "C", "G",
              "Fmaj7", "Em7", "Dm7", "Cmaj7", "Fmaj7", "Em7 A7", "Dm7", "G7"]
    mel = old_tune() + line(
        "A5:2 r:1 A5:1 C6:2 A5:2 G5:2 F5:2 r:4 | G5:2 r:1 G5:1 B5:2 G5:2 E5:4 r:4 |"
        "F5:2 r:1 F5:1 A5:2 F5:2 D5:2 E5:2 F5:2 r:2 | E5:6 r:2 B4:2 C5:2 D5:2 E5:2 |"
        "A5:2 r:1 A5:1 C6:2 E6:2 D6:2 C6:2 A5:4 | G5:2 B5:2 E6:4 C#6:2 A5:2 G5:4 |"
        "F5:2 A5:2 D6:2 C6:2 A5:2 F5:2 E5:2 D5:2 | G5:4 r:2 B4:2 D5:2 F5:2 G5:2 r:2", 16)
    bahs = [(b, 14) for b in (1, 3, 5, 9, 11, 13, 17, 19, 21)] + [(7, 0), (15, 0), (23, 8)]
    return dict(name="overworld", title="Hilltop Bounce", bpm=104, bars=24, chords=chords, mel=mel, bahs=bahs,
                lead=("organ", "marimba"), bass=32, arp="glock", bells="glock", kit="pop", key_lo=72, key_hi=88,
                motif=(14, 84, "major"))

def underground():
    chords = ["Am", "Am", "Dm", "E7", "Am", "F", "Dm E7", "Am",
              "F", "G", "Em", "Am", "Dm", "E7", "Am", "E7"]
    mel = line(
        "A4:2 r:2 C5:2 r:2 E5:2 D5:2 C5:2 r:2 | B4:2 C5:2 A4:4 r:4 E4:2 G#4:2 |"
        "A4:2 r:2 D5:2 r:2 F5:2 E5:2 D5:2 r:2 | C5:2 D5:2 B4:4 r:4 G#4:2 B4:2 |"
        "E5:2 r:1 E5:1 A5:2 G#5:2 A5:2 E5:2 C5:4 | F5:2 r:1 F5:1 A5:2 G5:2 F5:2 E5:2 D5:4 |"
        "D5:2 F5:2 A5:4 G#5:2 E5:2 B4:4 | A4:6 r:6 E4:2 A4:2 |"
        "C5:2 r:2 F5:2 r:2 A5:2 G5:2 F5:2 r:2 | D5:2 r:2 G5:2 r:2 B5:2 A5:2 G5:2 r:2 |"
        "E5:2 G5:2 B5:4 A5:2 G5:2 E5:4 | C6:2 B5:2 A5:4 r:4 E5:2 C5:2 |"
        "D5:2 F5:2 A5:2 D6:2 C6:2 A5:2 F5:4 | G#5:2 B5:2 D6:2 B5:2 G#5:2 E5:2 D5:4 |"
        "C5:2 E5:2 A5:4 E5:2 C5:2 A4:4 | B4:2 r:2 E5:2 r:2 G#5:2 r:2 B5:4")
    bahs = [(b, 12) for b in (3, 7, 11, 15)] + [(b, 14) for b in (1, 5, 9, 13)]
    return dict(name="underground", title="Brick Cellar", bpm=100, bars=16, chords=chords, mel=mel, bahs=bahs,
                lead=("pizz", "xylo"), bass=32, arp="xylo", bells="glock", kit="cellar", key_lo=69, key_hi=84, low_bah=True,
                motif=(14, 81, "minor"))

def athletic():
    chords = ["F", "C", "Dm", "Bb", "F", "Gm7 C7", "F", "C7",
              "Bb", "C", "Am", "Dm", "Gm", "C7", "F", "C7"]
    mel = line(
        "C5:2 F5:2 A5:3 G5:1 F5:2 C6:4 r:2 | G5:2 E5:2 C5:3 D5:1 E5:2 G5:4 r:2 |"
        "F5:2 A5:2 D6:3 C6:1 A5:2 F5:4 r:2 | D6:2 C6:2 Bb5:2 A5:2 G5:4 F5:2 G5:2 |"
        "A5:2 r:1 A5:1 C6:2 A5:2 F5:2 A5:2 C6:4 | Bb5:2 A5:2 G5:2 F5:2 E5:2 G5:2 C6:4 |"
        "A5:3 G5:1 F5:2 C5:2 F5:4 r:4 | E5:2 F5:2 G5:2 Bb5:2 A5:2 G5:2 E5:2 C5:2 |"
        "D5:2 F5:2 Bb5:4 A5:2 Bb5:2 D6:4 | C6:2 Bb5:2 A5:2 G5:2 E5:4 C5:4 |"
        "E5:2 A5:2 C6:4 B5:2 A5:2 E5:4 | F5:2 A5:2 D6:4 C6:2 A5:2 F5:4 |"
        "G5:2 Bb5:2 D6:3 C6:1 Bb5:2 G5:2 D5:4 | E5:2 G5:2 Bb5:2 C6:2 D6:2 C6:2 Bb5:4 |"
        "A5:2 C6:2 F6:4 E6:2 C6:2 A5:4 | G5:2 r:2 E5:2 r:2 C5:2 D5:2 E5:4")
    bahs = [(b, 14) for b in (1, 3, 5, 7, 9, 11, 13)] + [(15, 4), (15, 8)]
    return dict(name="athletic", title="Cloud Hop", bpm=110, bars=16, chords=chords, mel=mel, bahs=bahs,
                lead=("organ", "marimba"), bass=32, arp="glock", bells="glock", kit="pop", key_lo=72, key_hi=89, son=False,
                motif=(14, 89, "major"))

def finale():
    chords = ["Dm", "Dm", "Bb", "A7", "Dm", "Gm", "Bb A7", "Dm",
              "Gm", "Dm", "Eb", "A7", "Gm", "Bb", "A7", "A7"]
    mel = line(
        "D5:2 r:1 D5:1 F5:2 A5:2 G5:2 F5:2 E5:4 | F5:2 r:1 F5:1 E5:2 D5:2 C#5:4 A4:4 |"
        "D5:2 r:1 D5:1 F5:2 Bb5:2 A5:2 G5:2 F5:4 | E5:2 F5:2 G5:2 A5:2 C#6:4 r:4 |"
        "D6:2 r:1 D6:1 C6:2 A5:2 Bb5:2 A5:2 F5:4 | G5:2 r:1 G5:1 Bb5:2 D6:2 C6:2 Bb5:2 G5:4 |"
        "F5:2 G5:2 A5:2 Bb5:2 A5:2 G5:2 E5:2 C#5:2 | D5:6 r:2 A4:2 D5:2 F5:2 A5:2 |"
        "Bb5:3 A5:1 G5:2 D5:2 G5:4 r:4 | A5:3 G5:1 F5:2 D5:2 A4:4 r:4 |"
        "G5:2 Bb5:2 Eb6:4 D6:2 C6:2 Bb5:4 | A5:2 C#6:2 E6:4 D6:2 C#6:2 A5:4 |"
        "D6:2 r:1 D6:1 Bb5:2 G5:2 D6:2 Bb5:2 G5:4 | F6:2 r:1 F6:1 D6:2 Bb5:2 F6:2 D6:2 Bb5:4 |"
        "E6:2 C#6:2 A5:2 G5:2 E5:2 C#5:2 A4:4 | A4:2 r:2 C#5:2 r:2 E5:2 r:2 G5:2 A5:2")
    bahs = [(b, 14) for b in (1, 3, 5, 9, 11, 13)] + [(7, 0), (15, 0), (15, 8)]
    return dict(name="finale", title="Hat Trick", bpm=106, bars=16, chords=chords, mel=mel, bahs=bahs,
                lead=("organ", "xylo"), bass=33, arp="xylo", bells="glock", kit="finale", key_lo=69, key_hi=88, low_bah=True)

THEMES = [overworld, underground, athletic, finale]

# The Hat Trick leitmotif: the finale's opening bar, quoted in every other theme on the bells at
# half speed (two bars from `bar`), in that theme's key and mode. (scale degree, sixteenths); None rests.
MOTIF = [(1, 4), (None, 2), (1, 2), (3, 4), (5, 4), (4, 4), (3, 4), (2, 8)]
DEGREE = {"major": {1: 0, 2: 2, 3: 4, 4: 5, 5: 7}, "minor": {1: 0, 2: 2, 3: 3, 4: 5, 5: 7}}

def motif_notes(t, vel=0.62):
    if "motif" not in t: return []
    bar, tonic, mode = t["motif"]
    out, beat = [], bar * 4.0
    for deg, d in MOTIF:
        if deg: out.append((beat, d / 4 * 0.9, tonic + DEGREE[mode][deg], vel))
        beat += d / 4
    return out

# ---------- arrangement -> stems ----------
GM = {"organ": 17, "pizz": 45, "timp": 47}
VSCO_INST = {}
def insts():
    if not VSCO_INST:
        VSCO_INST.update(
            marimba=Pitched("Marimba", gain=0.9, ring=0.45, mute=1.3), xylo=Pitched("Xylo", gain=0.75, ring=0.35, mute=1.2),
            glock=Pitched("Glock", gain=0.7, ring=1.1, mute=3.0),
            claves=OneShot("Claves1_Hit*", 0.55, 0.4), conga=OneShot("Conga-HitN*", 0.8, 0.5), slap=OneShot("Conga-Tap1*", 0.8, 0.4),
            cowbell=OneShot("Cowbell1_Hit*", 0.35, 0.4), tamb=OneShot("Tamb1-Hit*", 0.4, 0.4), shake=OneShot("Tamb1-Shake*", 0.35, 0.5),
            guiro=OneShot("Guiro-Hit*", 0.45, 0.6), loghi=OneShot("LogDrumHi*", 0.8, 0.5), loglo=OneShot("LogDrumLo*", 0.9, 0.6),
            triangle=OneShot("Triangle3-Hit_*", 0.3, 1.2), sleigh=OneShot("Sleighbells*", 0.4, 0.8), belltree=OneShot("BellTree*", 0.4, 2.0))
    return VSCO_INST

def arrange(t):
    """Returns stems: name -> (fluidsynth parts, sampled notes [(beat, beats, pitch, vel, inst)])."""
    bars, ch = t["bars"], t["chords"]
    mel = [(b, d, p, 0.82 + (0.1 if b % 1 == 0 else 0)) for b, d, p in t["mel"]]
    lead_fluid, lead_sample = t["lead"]
    stems = {}
    stems["lead"] = ([(0, GM[lead_fluid], stacc(mel, 0.55 if lead_fluid == "organ" else 0.9))],
                     [(b, d, p, v, lead_sample) for b, d, p, v in mel])
    stems["bass"] = ([(1, t["bass"], bass_line(ch, bars) if t["kit"] != "cellar" else
                       bass_line(ch, bars, 40, ((0, 2, "R"), (6, 2, "8"), (10, 2, "5"), (14, 2, "R"))))], [])
    # rhythm section: General MIDI kit (kick 36, clap 39, closed hat 42) plus sampled hand percussion
    if t["kit"] == "cellar":
        kit = hits(bars, (4, 12), 39, 0.55)
        perc = [(b, d, 0, v, "loglo") for b, d, _, v in hits(bars, (0, 10), 0, 0.8)] + \
               [(b, d, 0, v, "loghi") for b, d, _, v in hits(bars, (4, 7, 12, 14), 0, lambda s: 0.7 if s in (4, 12) else 0.5)]
    else:
        kit = hits(bars, (0, 8), 36, 0.75) + hits(bars, (4, 12), 39, 0.8) + hits(bars, range(0, 16, 2), 42, lambda s: 0.42 if s % 4 else 0.3)
        perc = [(b, d, 0, 0.35, "guiro") for b, d, _, v in hits(bars, (0,), 0, 1, every=4, phase=3)]
    perc += [(b, d, 0, v, "claves") for b, d, _, v in clave(bars, son=t.get("son", True))]
    fluid_perc = [(9, 0, kit)]
    if t["kit"] == "finale":
        fluid_perc.append((2, GM["timp"], [(bar * 4, 1.0, near(chord_at(ch, bar * 4), 41), 0.8) for bar in range(bars)]))
    stems["perc"] = (fluid_perc, perc)
    bells = dings(ch, bars, t["key_lo"] + 12, t["key_hi"] + 12, 1, 0.45) + motif_notes(t)
    stems["bells"] = ([], [(b, d, p, v, t["bells"]) for b, d, p, v in bells])
    # reactive layers: "fast" (running / chaining moves) and "arp" (cap-jump chains)
    fast = [(b, d, 0, v, "conga") for b, d, _, v in hits(bars, (6, 7), 0, 0.75)] + \
           [(b, d, -5, v, "conga") for b, d, _, v in hits(bars, (14, 15), 0, 0.8)] + \
           [(b, d, 0, v, "slap") for b, d, _, v in hits(bars, (4, 12), 0, 0.8)] + \
           [(b, d, 0, v, "cowbell") for b, d, _, v in hits(bars, (0, 4, 8, 12), 0, lambda s: 0.9 if s == 0 else 0.6)] + \
           [(b, d, 0, v, "shake") for b, d, _, v in hits(bars, (2, 6, 10, 14), 0, 0.5)]
    stems["fast"] = ([], fast)
    stems["arp"] = ([], [(b, d, p, v, t["arp"]) for b, d, p, v in arpeggio(ch, bars, t["key_lo"], t["key_hi"] + 5, 0.42)])
    stems["bah"] = ([], [])   # synthesized separately
    # "danger" (time running low, a boss near): low tremolo strings on the chord roots and fifths,
    # over a staccato contrabass pulse on every beat
    danger = []
    for bar in range(bars):
        for half in (0, 2):
            c = chord_at(ch, bar * 4 + half); r = near(c, 45)
            danger += [(bar * 4 + half, 1.9, r, 0.72), (bar * 4 + half, 1.9, r + (7 if 7 in c[1] else 6), 0.6)]
    pulse = [(bar * 4 + q, 0.3, near(chord_at(ch, bar * 4 + q), 33), 0.8 if q % 2 == 0 else 0.62) for bar in range(bars) for q in range(4)]
    stems["danger"] = ([(10, 44, danger), (11, 43, pulse)], [])
    # "secret" (near a moon coin not found yet): high celesta twinkles on the chord, a bell tree now and then
    twinkle = []
    for bar in range(bars):
        for s, k in ((0, 0), (3, 1), (6, 2), (10, 3), (13, 2)):
            tn = tones(chord_at(ch, bar * 4 + s / 4), t["key_lo"] + 12, t["key_hi"] + 14)
            twinkle.append((bar * 4 + s / 4, 0.5, tn[min(k, len(tn) - 1)], 0.5 if s else 0.62))
    stems["secret"] = ([(12, 8, twinkle)], [(bar * 4, 2, 0, 0.5, "belltree") for bar in range(0, bars, 4)])
    # "mallet": the bonus room remix, the whole theme on marimba and glockenspiel alone
    mallet = [(b, d, p, v * 0.95, "marimba") for b, d, p, v in mel] + \
             [(b, d, p, v, "glock") for b, d, p, v in bells] + \
             [(b, d, p + 12, v * 0.8, "marimba") for b, d, p, v in bass_line(ch, bars)]
    stems["mallet"] = ([], mallet)
    return stems

LEVELS = {"lead": -15, "bass": -17, "perc": -20, "bells": -27, "fast": -23, "arp": -26, "bah": -19,
          "danger": -21, "secret": -25, "mallet": -15}
SOLO = ("mallet",)   # stems that play on their own (the bonus room remix), so they are not mixed with the rest

def rms_db(x):
    a = np.abs(x).max(axis=1); act = x[a > 1e-3]
    return 20 * np.log10(np.sqrt(np.mean(act ** 2)) + 1e-12) if len(act) else -120

def build(t, outdir):
    """Renders a theme's loop stems to outdir/<stem>.ogg; returns the stems and loop metadata."""
    spb = 60 / t["bpm"]; L = int(round(t["bars"] * 4 * spb * SR)); end = t["bars"] * 4 + 8
    stems = {}
    for name, (fparts, snotes) in arrange(t).items():
        x = np.zeros((L + 8 * SR, 2), np.float32)
        if fparts:
            y = fluid(fparts, t["bpm"], end); x[:min(len(x), len(y))] += y[:len(x)]
        if snotes:
            y = render([(b * spb, d * spb, p, v, i) for b, d, p, v, i in snotes], insts(), L); x[:len(y)] += y[:len(x)]
        if name == "bah":
            for b, ps in bah_notes(t["chords"], t["bahs"]):
                y = bah(ps, 0.3, seed=int(b * 7), low=t.get("low_bah", False)); s = int(b * spb * SR)
                x[s:s + len(y)] += y[:len(x) - s]
        stems[name] = fold(x, L)
    for name, x in stems.items():   # level each stem to its target loudness
        stems[name] = x * db(LEVELS[name] - rms_db(x))
    peak = np.abs(sum(x for n, x in stems.items() if n not in SOLO)).max()
    if peak > 0.9:
        for name in stems:
            if name not in SOLO: stems[name] *= 0.9 / peak
    for name in SOLO:
        peak = np.abs(stems[name]).max()
        if peak > 0.9: stems[name] *= 0.9 / peak
    for name, x in stems.items(): save_ogg(os.path.join(outdir, name + ".ogg"), x)
    meta = dict(title=t["title"], bpm=t["bpm"], beats=t["bars"] * 4, samples=L, rate=SR, stems=list(stems),
                bah_beats=sorted(b for b, _ in bah_notes(t["chords"], t["bahs"])))
    with open(os.path.join(outdir, "music.txt"), "w") as f:   # read by the game at runtime (MODDING.md)
        f.write(f'# {t["title"]}: {meta["beats"]} beats, {L} samples at {SR} Hz\n')
        f.write(f'bpm {t["bpm"]:g}\nbah {" ".join(f"{b:g}" for b in meta["bah_beats"])}\n')
    # the full arrangement as one General MIDI file (sampled parts mapped to GM programs)
    gmap = {"marimba": 12, "xylo": 13, "glock": 9}
    parts = []
    for name, (fparts, snotes) in arrange(t).items():
        if name in SOLO: continue   # the bonus room remix repeats the other parts
        parts += fparts
        byinst = {}
        for b, d, p, v, i in snotes:
            if i in gmap: byinst.setdefault(i, []).append((b, d, p, v))
        parts += [(3 + k, gmap[i], n) for k, (i, n) in enumerate(byinst.items())]
    write_midi(os.path.join(outdir, t["name"] + ".mid"), t["bpm"], parts, t["bars"] * 4)
    return stems, meta

# ---------- jingles (play once) ----------
def jingle(kind):
    if kind == "clear":   # course clear: a short two-bar fanfare, a pickup run into a held high C and a sung "bah!"
        bpm, chords = 150, ["C G", "F C"]
        mel = line("C5:2 E5:2 G5:2 C6:2 r:1 G5:1 C6:2 E6:4 | D6:2 C6:2 A5:2 B5:2 C6:8")
        sampled = [(b, d, p, 0.9, "marimba") for b, d, p in mel] + \
                  [(4 + i * 0.125, 0.5, p, 0.6, "glock") for i, p in enumerate((84, 88, 91, 96, 100, 103))] + \
                  [(6, 1, 0, 0.6, "belltree")]
        parts = [(0, 17, stacc(with_vel(mel, 0.85), 0.6)),
                 (1, 32, [(0, 0.5, 36, 0.9), (1, 0.5, 43, 0.8), (2, 0.5, 36, 0.8), (3, 0.5, 43, 0.8), (4, 0.5, 41, 0.9), (5, 0.5, 43, 0.9), (6, 1.5, 36, 1.0)]),
                 (9, 0, [(b, 0.25, 39, 0.8) for b in (1, 3, 5)] + [(6, 0.5, 49, 0.8), (6, 0.25, 36, 0.9)])]
        bahs = [(6.0, [60, 64, 67])]
        length = 3.8
    else:                 # death: a short deflating descent
        bpm, chords = 100, ["C", "C"]
        mel = line("E5:2 r:1 E5:1 D#5:2 r:2 D5:2 r:2 C#5:4 | C5:2 G4:2 C4:4 r:8")
        sampled = [(b, d, p, 0.85, "marimba") for b, d, p in mel] + [(4, 1, 48, 0.7, "marimba")]
        parts = [(0, 17, stacc(octave(with_vel(mel, 0.7), -12), 0.5)),
                 (1, 32, [(4, 1.0, 36, 0.8)])]
        bahs = [(4.5, [55, 60])]
        length = 4.0
    spb = 60 / bpm; L = int(length * SR)
    x = np.zeros((L + 4 * SR, 2), np.float32)
    y = fluid(parts, bpm, length / spb); x[:min(len(x), len(y))] += y[:len(x)]
    y = render([(b * spb, d * spb, p, v, i) for b, d, p, v, i in sampled], insts(), L); x[:len(y)] += y[:len(x)]
    for b, ps in bahs:
        y = bah(ps, 0.45 if kind == "clear" else 0.5, seed=3, low=kind != "clear"); s = int(b * spb * SR)
        x[s:s + len(y)] += y * (0.5 if kind == "clear" else 0.35)
    x = x[:L]; x[-int(0.3 * SR):] *= np.linspace(1, 0, int(0.3 * SR))[:, None]
    return x * (0.8 / np.abs(x).max())
