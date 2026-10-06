"""Writes the game's built-in art as PNG templates in the layouts of assets/gfx/ (see MODDING.md).
usage: python3 tools/export_gfx.py [outdir]   (default: gfx-template/; needs ./build.sh and Pillow)
Copy the files you change into assets/gfx/; the ones you leave out keep the built-in art.
"""
import os, subprocess, sys, tempfile
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "gfx-template")
os.makedirs(out, exist_ok=True)
with tempfile.TemporaryDirectory() as tmp:
    listing = subprocess.run([os.path.join(ROOT, "sim"), "--export-gfx", tmp], capture_output=True, text=True, check=True).stdout
    for line in listing.split("\n"):
        if not line.strip(): continue
        name, w, h = line.split(); w, h = int(w), int(h)
        im = Image.frombytes("RGBA", (w, h), open(os.path.join(tmp, name + ".rgba"), "rb").read())
        im.save(os.path.join(out, name + ".png"))
        print(f"{name}.png  {w}x{h}")
print("templates in", out)
