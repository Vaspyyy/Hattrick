# Hatrick audio studio: renders original music and sound effects from freely licensed
# instruments (VSCO 2 CE samples, CC0; GeneralUser GS SoundFont, see assets/licenses/).
# Notes are (beat, beats, midi pitch, velocity 0..1, instrument). Needs numpy, scipy,
# fluidsynth and ffmpeg.
import glob, math, os, re, subprocess, tempfile, wave
from functools import lru_cache
import numpy as np
from scipy.signal import fftconvolve, lfilter, resample_poly

SR = 48000
HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(os.path.dirname(HERE), "assets")
SF2 = os.path.join(ASSETS, "instruments", "GeneralUser-GS.sf2")
VSCO = os.path.join(ASSETS, "instruments", "vsco")
RNG = np.random.default_rng(7)

NAMES = {"C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3, "E": 4, "F": 5, "F#": 6, "Gb": 6,
         "G": 7, "G#": 8, "Ab": 8, "A": 9, "A#": 10, "Bb": 10, "B": 11}

def midi(name):   # "C#5" -> 73 (C4 = 60)
    m = re.fullmatch(r"([A-G][#b]?)(-?\d)", name)
    return 12 * (int(m.group(2)) + 1) + NAMES[m.group(1)]

def hz(p): return 440.0 * 2 ** ((p - 69) / 12)

# ---------- sample loading ----------
@lru_cache(None)
def load(path):   # -> float32 stereo at SR
    w = wave.open(path)
    ch, sw, sr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    raw = w.readframes(n)
    if sw == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3).astype(np.int32)
        x = b[:, 0] | b[:, 1] << 8 | b[:, 2] << 16
        x = np.where(x >= 1 << 23, x - (1 << 24), x) / float(1 << 23)
    elif sw == 2: x = np.frombuffer(raw, np.int16) / 32768.0
    elif sw == 4: x = np.frombuffer(raw, np.int32) / float(1 << 31)
    else: raise ValueError(path)
    x = x.reshape(-1, ch).astype(np.float32)
    if ch == 1: x = np.repeat(x, 2, axis=1)
    if sr != SR:
        g = math.gcd(SR, sr)
        x = resample_poly(x, SR // g, sr // g, axis=0).astype(np.float32)
    return x

def pitched(x, ratio):   # play back faster by ratio (pitch up), linear interpolation
    if abs(ratio - 1) < 1e-4: return x
    n = int(len(x) / ratio)
    pos = np.arange(n) * ratio
    i = pos.astype(np.int64); f = (pos - i)[:, None].astype(np.float32)
    j = np.minimum(i + 1, len(x) - 1)
    return x[i] * (1 - f) + x[j] * f

def fade(x, start, length):   # fade out from sample `start` over `length` samples, cut after
    x = x[:start + length].copy()
    if len(x) > start: x[start:] *= np.linspace(1, 0, len(x) - start, dtype=np.float32)[:, None]
    return x

# ---------- sampled instruments ----------
class Pitched:
    """Multi-sampled pitched instrument: nearest sample, resampled to the pitch."""
    def __init__(self, folder, gain=1.0, ring=0.6, release=0.08, mute=1.6):
        self.zones = []
        for f in sorted(glob.glob(os.path.join(VSCO, folder, "*.wav"))):
            m = re.search(r"_([A-G][#b]?\d)[_.]", os.path.basename(f))
            if m: self.zones.append((midi(m.group(1)), f))
        self.gain, self.ring, self.release, self.mute = gain, ring, release, mute

    def note(self, p, dur, vel):
        root, f = min(self.zones, key=lambda z: (abs(z[0] - p), z[0] > p))
        y = pitched(load(f), 2 ** ((p - root) / 12))
        hold = int(min(self.ring, max(dur * self.mute, 0.07)) * SR)   # mallet notes are damped, "staccato"
        return fade(y, hold, int(self.release * SR)) * (self.gain * (0.25 + 0.75 * vel))

class OneShot:
    """Unpitched hits with round-robin and velocity layers from VSCO percussion files."""
    def __init__(self, pattern, gain=1.0, length=1.2):
        self.files = sorted(glob.glob(os.path.join(VSCO, "Perc", pattern)))
        assert self.files, pattern
        self.gain, self.length, self.i = gain, length, 0

    def note(self, p, dur, vel):
        layers = sorted(set(re.sub(r"_rr\d", "", f) for f in self.files))
        layer = layers[min(len(layers) - 1, int(vel * len(layers)))]
        rr = [f for f in self.files if re.sub(r"_rr\d", "", f) == layer]
        self.i += 1
        y = load(rr[self.i % len(rr)])
        tune = 2 ** ((p - 60) / 12) if p else 1.0   # pitch 0 = as recorded
        return fade(pitched(y, tune), int(self.length * SR), int(0.05 * SR)) * self.gain

# ---------- SoundFont parts through fluidsynth ----------
def write_midi(path, bpm, parts, end_beat):
    """parts: [(channel, program, [(beat, beats, pitch, vel)])], format 1, 480 ticks per beat."""
    T = 480
    def vlq(n):
        out = [n & 0x7f]; n >>= 7
        while n: out.append(0x80 | n & 0x7f); n >>= 7
        return bytes(reversed(out))
    def chunk(evs):
        evs.sort(key=lambda e: (e[0], e[1]))
        data, t = b"", 0
        for tick, _, msg in evs: data += vlq(tick - t) + msg; t = tick
        data += vlq(0) + b"\xff\x2f\x00"
        return b"MTrk" + len(data).to_bytes(4, "big") + data
    tempo = int(60e6 / bpm)
    tracks = [chunk([(0, 0, b"\xff\x51\x03" + tempo.to_bytes(3, "big")), (int(end_beat * T), 0, b"\xff\x01\x03end")])]
    for ch, prog, notes in parts:
        evs = [(0, 0, bytes([0xC0 | ch, prog]))] if ch != 9 else []
        for b, d, p, v in notes:
            on = int(round(b * T)); off = max(on + 1, int(round((b + d) * T)))
            evs.append((on, 2, bytes([0x90 | ch, p, max(1, min(127, int(v * 127)))])))
            evs.append((off, 1, bytes([0x80 | ch, p, 0])))
        evs.append((int(end_beat * T), 3, bytes([0xB0 | ch, 110, 0])))   # keeps the render going for tails
        tracks.append(chunk(evs))
    open(path, "wb").write(b"MThd" + (6).to_bytes(4, "big") + (1).to_bytes(2, "big") +
                           len(tracks).to_bytes(2, "big") + T.to_bytes(2, "big") + b"".join(tracks))

def fluid(parts, bpm, end_beat, reverb=0.25):
    with tempfile.TemporaryDirectory() as d:
        m, w = os.path.join(d, "p.mid"), os.path.join(d, "p.wav")
        write_midi(m, bpm, parts, end_beat)
        subprocess.run(["fluidsynth", "-ni", "-q", "-r", str(SR), "-g", "0.6",
                        "-o", "synth.chorus.active=0", "-o", "synth.reverb.active=1",
                        "-o", f"synth.reverb.level={reverb}", "-o", "synth.reverb.room-size=0.3",
                        "-F", w, SF2, m], check=True, capture_output=True)
        return load.__wrapped__(w)

# ---------- synthesis helpers ----------
def t_axis(sec): return np.arange(int(sec * SR)) / SR

def stereo(x, pan=0.0):   # mono -> stereo, pan -1..1
    l, r = math.cos((pan + 1) * math.pi / 4), math.sin((pan + 1) * math.pi / 4)
    return np.stack([x * l * 1.414, x * r * 1.414], axis=1).astype(np.float32)

def resonate(x, freqs, bws, block=128):
    """Band-pass resonator whose centre/bandwidth follow per-sample arrays (time-varying formant)."""
    y = np.zeros_like(x); zi = np.zeros(2)
    for s in range(0, len(x), block):
        f, bw = float(freqs[min(s, len(freqs) - 1)]), float(bws[min(s, len(bws) - 1)])
        w0 = 2 * math.pi * f / SR; alpha = math.sin(w0) * math.sinh(math.log(2) / 2 * (bw / f) * w0 / math.sin(w0))
        b = np.array([alpha, 0, -alpha]) / (1 + alpha); a = np.array([1, -2 * math.cos(w0) / (1 + alpha), (1 - alpha) / (1 + alpha)])
        y[s:s + block], zi = lfilter(b, a, x[s:s + block], zi=zi)
    return y

def room(x, size=0.5, wet=0.18, seed=1):   # small stereo room from decaying noise
    r = np.random.default_rng(seed)
    n = int(size * SR); env = np.exp(-np.arange(n) / (size * SR / 6.9))
    ir = np.stack([r.standard_normal(n) * env, r.standard_normal(n) * env], 1) * 0.03
    out = np.zeros((len(x) + n - 1, 2), np.float32)
    for c in range(2): out[:, c] = fftconvolve(x[:, c], ir[:, c])
    out[:len(x)] = out[:len(x)] * wet + x * (1 - wet * 0.3)
    return out[:len(x) + int(size * SR * 0.6)]

def bah(pitches, length=0.34, seed=0, low=False):
    """Short sung "bah!": glottal-ish harmonic source through moving formants, a few voices."""
    r = np.random.default_rng(seed)
    out = np.zeros((int((length + 0.5) * SR), 2), np.float32)
    F = [(820, 1180, 2650, 3500), (720, 1100, 2450, 3300)][low]      # /a/ formants, brighter or lower voice
    for vi, p in enumerate(pitches):
        f0 = hz(p) * 2 ** (r.uniform(-8, 8) / 1200)
        t = t_axis(length + 0.05)
        f = f0 * (1 - 0.05 * np.exp(-t / 0.035)) * (1 + 0.006 * np.sin(2 * math.pi * 5.6 * t + r.uniform(0, 6)))
        ph = np.cumsum(f) / SR
        src = np.zeros_like(t)
        for k in range(1, int(5200 / f0) + 1): src += np.sin(2 * math.pi * k * ph) / k ** 1.1
        src += r.standard_normal(len(t)) * 0.02
        y = np.zeros_like(t)
        for fi, (ft, start, bw, g) in enumerate(zip(F, (260, 750, 2200, 3200), (90, 110, 160, 220), (1.0, 0.55, 0.22, 0.1))):
            path = ft + (start - ft) * np.exp(-t / 0.022)          # the "b": formants open up
            y += resonate(src, path, np.full_like(t, bw)) * g
        env = np.clip((t - 0.008) / 0.012, 0, 1) * np.where(t < length * 0.4, 1, np.exp(-(t - length * 0.4) / (length * 0.22)))
        y *= env
        y /= np.max(np.abs(y)) + 1e-9
        d = int((vi * 0.006 + r.uniform(0, 0.006)) * SR)
        s = stereo(y.astype(np.float32), (vi - (len(pitches) - 1) / 2) * 0.5)
        out[d:d + len(s)] += s
    return room(out, 0.45, 0.2, seed)

# ---------- mixing, loops, files ----------
def render(notes, insts, length, offset=0.0):
    """Mix sampled notes into a buffer `length` samples long (plus tails). insts: name -> instrument."""
    buf = np.zeros((length + 6 * SR, 2), np.float32)
    for sec, dur, p, v, name in notes:
        y = insts[name].note(p, dur, v)
        s = int(round((sec + offset) * SR))
        n = min(len(y), len(buf) - s)
        if n > 0 and s >= 0: buf[s:s + n] += y[:n]
    return buf

def fold(x, length):   # seamless loop: everything past the end wraps onto the start
    out = np.zeros((length, 2), np.float32)
    for s in range(0, len(x), length):
        seg = x[s:s + length]; out[:len(seg)] += seg
    return out

def save_ogg(path, x, q=6):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    x = np.clip(x, -1, 1).astype(np.float32)
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "f32le", "-ar", str(SR), "-ac", "2", "-i", "-",
                    "-c:a", "libvorbis", "-q:a", str(q), path], input=x.tobytes(), check=True)

def db(g): return 10 ** (g / 20)

def limit(x, ceiling=0.95):   # gentle soft clip for previews
    return (np.tanh(x / ceiling) * ceiling).astype(np.float32)
