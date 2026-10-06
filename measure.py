# Runs input scripts in the simulator on the flat lab level and reports every airborne arc.
import subprocess,sys
LAB=str(sum(l.startswith("=") and not l[1:].strip().startswith("lab") for l in open("assets/levels.txt")))  # lab levels come last
def run(tas):
    open("/tmp/claude-1000/m.tas","w").write("\n".join(tas)+"\n")
    out=subprocess.run(["./sim",LAB,"/tmp/claude-1000/m.tas","trace"],capture_output=True,text=True).stdout
    rows=[]
    for l in out.splitlines():
        f=l.split()
        if not f or not f[0].isdigit(): continue
        p=dict(kv.split("=") for kv in f[2:])
        rows.append((float(p["x"]),float(p["y"]),int(p["gnd"])))
    return rows
def arcs(name,tas):
    r=run(tas); res=[]; i=1
    while i<len(r):
        if r[i-1][2] and not r[i][2]:
            j=i
            while j<len(r) and not r[j][2]: j+=1
            x0,y0=r[i-1][0],r[i-1][1]; top=min(p[1] for p in r[i:j+1])
            if j<len(r): res.append(f"h{(y0-top)/8:.1f} d{(r[j][0]-x0)/8:+.1f} t{j-i+1}")
            i=j
        i+=1
    print(f"{name:22s}", " | ".join(res), "  (tiles, frames)")
R=["60 R"]
arcs("running jump",R+["30 RJ","20 R"])
arcs("single>double>triple",R+["20 RJ","13 R","30 RJ","10 R","45 RJ","30 R"])
arcs("long jump x2",R+["1 RDJ","27 RD","5 RDJ","40 R"])
arcs("backflip",["10 -","1 DJ","80 D"])
arcs("side flip",R+["4 L","30 LJ","60 L"])
arcs("dive from ground",R+["1 RDC","60 R"])
arcs("GP jump",["10 -","17 J","1 D","24 -","40 J","30 -"])
arcs("jump+cap stall",R+["10 RJ","1 RJC","30 RJ","30 R"])
arcs("jump,cap,dive,capjump",R+["12 RJ","1 RJC","8 RJ","1 RDC","20 R","40 RJ","30 R"])
arcs("wall test: jump only",R+["40 RJ"])
arcs("dive in air at apex",R+["18 RJ","1 RDC","40 R"])
arcs("cap hover hold, no bounce",["10 -","1 C","60 C"])
arcs("jump,cap,dive,cj,dive",R+["12 RJ","1 RJC","8 RJ","1 RDC","20 R","16 RJ","1 RDC","60 R"])
arcs("jump,cap,dive,cj,dive2",R+["12 RJ","1 RJC","8 RJ","1 RDC","20 R","22 RJ","1 RDC","60 R"])
arcs("long jump,cap,dive,cj",R+["1 RDJ","14 RD","1 RC","3 R","1 RDC","20 R","20 RJ","1 RDC","60 R"])
