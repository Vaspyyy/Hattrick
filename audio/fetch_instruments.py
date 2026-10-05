# Downloads the freely licensed instruments the audio is rendered from (not kept in git):
#   GeneralUser GS SoundFont (S. Christian Collins, GeneralUser GS License v2.0), 32 MB
#   VSCO 2 Community Edition subset (Versilian Studios, CC0), 80 WAV files, 45 MB
# into assets/instruments/, with their license files in assets/licenses/.
import json, os, re, urllib.parse, urllib.request
from concurrent.futures import ThreadPoolExecutor

ASSETS = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "assets")

def listing(repo, path):
    url = f"https://api.github.com/repos/{repo}/contents/{urllib.parse.quote(path)}"
    return json.load(urllib.request.urlopen(url, timeout=30))

def jobs():
    gu, vs = "mrbumpy409/GeneralUser-GS", "sgossner/VSCO-2-CE"
    for x in listing(gu, ""):
        if x["name"] == "GeneralUser-GS.sf2": yield x, "instruments/GeneralUser-GS.sf2"
    for x in listing(gu, "documentation"):
        if x["name"] == "LICENSE.txt": yield x, "licenses/GeneralUser-GS_LICENSE.txt"
    for x in listing(vs, ""):
        if x["name"] == "LICENSE": yield x, "licenses/VSCO-2-CE_LICENSE.txt"
        if x["name"] == "Readme.txt": yield x, "licenses/VSCO-2-CE_Readme.txt"
    for folder in ("Marimba", "Glock", "Xylo"):
        for x in listing(vs, "Percussion/" + folder):
            if x["type"] == "file": yield x, f"instruments/vsco/{folder}/{x['name']}"
    keep = re.compile(r"^(Claves1_Hit|Conga-HitN|Conga-Tap1|Cowbell1_Hit|Guiro-Hit|Tamb1-Hit|Tamb1-Shake|LogDrumHi|"
                      r"LogDrumLo|Triangle3-Hit_|Sleighbells|Snare2-HitSN_v5|BDrumNewhit_v5|BellTree)")
    for x in listing(vs, "Percussion"):
        if x["type"] == "file" and keep.match(x["name"]): yield x, f"instruments/vsco/Perc/{x['name']}"

def get(job):
    x, rel = job
    dst = os.path.join(ASSETS, rel)
    if os.path.exists(dst) and os.path.getsize(dst) == x["size"]: return rel, "ok"
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    data = urllib.request.urlopen(x["download_url"], timeout=120).read()
    if len(data) != x["size"] or (rel.endswith((".wav", ".sf2")) and data[:4] != b"RIFF"):
        return rel, "BAD DOWNLOAD"
    open(dst, "wb").write(data)
    return rel, "downloaded"

if __name__ == "__main__":
    with ThreadPoolExecutor(8) as ex: res = list(ex.map(get, list(jobs())))
    for rel, s in res:
        if s != "ok": print(s, rel)
    print(len(res), "files in place")
