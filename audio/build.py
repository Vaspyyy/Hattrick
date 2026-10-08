# Renders all Hatrick audio into ../assets: music stems (music/<theme>/), jingles, sound effects
# (sfx/) and listening previews (preview/). usage: python3 build.py [music] [sfx] [preview]
import os, sys
import numpy as np
from studio import SR, ASSETS, save_ogg, limit
import songs, sfx

what = set(sys.argv[1:]) or {"music", "sfx", "preview"}
music_dir, sfx_dir, prev_dir = (os.path.join(ASSETS, d) for d in ("music", "sfx", "preview"))
guide = ["Hatrick audio previews", "======================", ""]

def stamp(sec): return f"{int(sec // 60)}:{sec % 60:04.1f}"

def song_preview(stems, meta):
    """Loop twice: base mix, then the reactive layers fade in (fast percussion, then the arpeggio)."""
    L = meta["samples"]; base = sum(stems[k] for k in ("lead", "bass", "perc", "bells", "bah"))
    out = np.concatenate([base, base]).astype(np.float32)
    ramp = lambda start, sec: np.clip((np.arange(2 * L) - start) / (sec * SR), 0, 1)[:, None]
    out += np.concatenate([stems["fast"], stems["fast"]]) * ramp(L, 1.5)
    out += np.concatenate([stems["arp"], stems["arp"]]) * ramp(L + L // 2, 1.0)
    out[-3 * SR:] *= np.linspace(1, 0, 3 * SR)[:, None]
    return limit(out), [(0, "base mix: lead, bass, percussion, bells, bah"), (L / SR, "+ fast percussion layer (running / chaining moves)"),
                        (1.5 * L / SR, "+ bell arpeggio layer (cap-jump chains)")]

def extras_preview(stems, meta):
    """Half a loop each: base mix + danger strings, base mix + secret celesta, then the bonus room remix."""
    L = meta["samples"]; H = L // 2; base = sum(stems[k] for k in ("lead", "bass", "perc", "bells", "bah"))
    ramp = np.clip(np.arange(H) / (1.0 * SR), 0, 1)[:, None]
    parts = [base[:H] + stems["danger"][:H] * ramp, base[H:2 * H] + stems["secret"][H:2 * H] * ramp, stems["mallet"][:H]]
    out = np.concatenate(parts).astype(np.float32)
    out[-2 * SR:] *= np.linspace(1, 0, 2 * SR)[:, None]
    return limit(out), [(0, "+ danger layer (timer under 100, or a boss near)"), (H / SR, "+ secret layer (near a moon coin not found yet)"),
                        (2 * H / SR, "bonus room remix (marimba and glockenspiel only)")]

if "music" in what or "preview" in what:
    for t in songs.THEMES:
        th = t(); out = os.path.join(music_dir, th["name"])
        stems, meta = songs.build(th, out)
        print(f"{th['name']:12s} {meta['bpm']} BPM, {meta['beats']} beats, {meta['samples'] / SR:.1f} s loop, {len(meta['bah_beats'])} bahs")
        if "preview" in what:
            x, marks = song_preview(stems, meta)
            save_ogg(os.path.join(prev_dir, f"music_{th['name']}.ogg"), x)
            guide.append(f"music_{th['name']}.ogg  \"{th['title']}\" ({meta['bpm']} BPM, {len(x) / SR:.0f} s)")
            guide += [f"  {stamp(s)}  {m}" for s, m in marks]
            x, marks = extras_preview(stems, meta)
            save_ogg(os.path.join(prev_dir, f"music_{th['name']}_extras.ogg"), x)
            guide.append(f"music_{th['name']}_extras.ogg  reactive layers and the bonus room remix ({len(x) / SR:.0f} s)")
            guide += [f"  {stamp(s)}  {m}" for s, m in marks]
    for kind in ("clear", "death"):
        x = songs.jingle(kind)
        save_ogg(os.path.join(music_dir, f"jingle_{kind}.ogg"), x)
        if "preview" in what:
            save_ogg(os.path.join(prev_dir, f"jingle_{kind}.ogg"), x)
            guide.append(f"jingle_{kind}.ogg  ({len(x) / SR:.1f} s)")

if "sfx" in what or "preview" in what:
    fx = sfx.build_all(sfx_dir)
    print("sfx:", ", ".join(fx))
    if "preview" in what:
        reel, t, marks = [], 0.0, []
        for name, x in fx.items():
            marks.append(f"  {stamp(t)}  {name}"); reel.append(x); gap = np.zeros((int(0.45 * SR), 2), np.float32)
            reel.append(gap); t += len(x) / SR + 0.45
        save_ogg(os.path.join(prev_dir, "sfx_reel.ogg"), np.concatenate(reel))
        guide += ["", f"sfx_reel.ogg  (all effects in a row; single files are in assets/sfx/)"] + marks

if "preview" in what:
    open(os.path.join(prev_dir, "README.txt"), "w").write("\n".join(guide) + "\n")
    print("\n".join(guide))
