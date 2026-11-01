# The port's side of tools/ctrace.c: replays a route through the GDScript simulator and prints
# exactly the lines ctrace prints from the C game (Hatrick, the cap, the live map's checksum, every
# enemy, dweller, seed, gimmick and boss of the current area), one per frame, so the two diff.
#   godot --headless --path remake --script res://tools/xtrace.gd -- LEVEL FILE.tas
#   SIM_RESPAWN=1 keeps going after a death; SIM_BOSS=N starts at boss arena N's entrance;
#   SIM_AT=ROOM,X,Y starts on that cell.
# tools/difftrace.sh runs both and shows the first difference.
extends SceneTree

const Levels = preload("res://src/sim/levels.gd")
const Sim = preload("res://src/sim/sim.gd")


func line(sim, frame: int, b: String) -> String:
	var s := "%d %s x=%d y=%d vx=%d vy=%d st=%d gnd=%d cap=%d room=%d score=%d wall=%d moons=%d" % [
		frame, b, sim.hx, sim.hy, sim.hvx, sim.hvy, sim.st, sim.gnd, sim.cst, sim.room, sim.score, sim.wall, sim.wd.moongot]
	var sum := 0
	var m: PackedByteArray = sim.wd.rm[sim.room]
	var stride: int = sim.rs[sim.room]
	for y in sim.lh:
		for x in sim.lw:
			sum = (sum * 31 + m[y * stride + x]) & 0xffffffff
	s += " c=%d hp=%d left=%d cx=%d,%d pow=%d carry=%d map=%d" % [sim.coins, sim.hp, sim.left, sim.cxp >> 8, sim.cyp >> 8, sim.capx_pow, sim.capx_carry, sum]
	for i in sim.wd.en.size():
		var e = sim.wd.en[i]
		if e.a and e.r == sim.room:
			s += " e%d=%d:%d,%d,%d,%d,%d" % [i, e.t, e.x >> 8, e.y >> 8, e.s, e.u, e.w]
	var L: Dictionary = sim.L
	for i in L.homes.size():
		if L.homes[i].room == sim.room:
			var d = sim.wd.dw[i]
			s += " d%d=%d,%d,%d" % [i, d.phase, d.ofs, d.a]
	for p in sim.wd.sh:
		if p.a: s += " sh=%d,%d" % [p.x >> 8, p.y >> 8]
	var gw = sim.wd.gw
	for i in gw.pl.size():
		if gw.pl[i].room == sim.room: s += " p%d=%d,%d" % [i, gw.pl[i].y, gw.pl[i].tilt]
	for g in gw.ball:
		if g.a: s += " gb=%d,%d" % [g.x >> 8, g.y >> 8]
	if L.gim[0]: s += " rise=%d" % gw.rise
	if gw.flipt or gw.rot or gw.flip: s += " flip=%d,%d,%d" % [gw.flipt, gw.rot, gw.flip]
	if gw.gold: s += " gold=%d,%d" % [gw.gold, gw.goldn]
	if L.avalanche: s += " av=%d" % sim.avx
	for i in sim.bz.size():
		var z = sim.bz[i]
		s += " B%d=%d,%d,%d,%d,%d,%d,%d,%d" % [i, z.on, z.x >> 8, z.y >> 8, z.hp, z.s, z.t, z.dead, z.hurt]
	if sim.bz.size():
		for q in sim.bsh:
			if q.a: s += " S=%d:%d,%d" % [q.kind, q.x >> 8, q.y >> 8]
		for c in sim.bcl:
			if c.a: s += " C=%d,%d,%d,%d" % [c.x >> 8, c.y >> 8, c.charge, c.strike]
		s += " saw=%d,%d hat=%d,%d,%d m=%d,%d,%d,%d" % [sim.sawt, sim.sawslam, sim.hatst, sim.hatx, sim.haty, sim.mcur, sim.mofs, sim.dcur, sim.dofs]
	return s


func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() < 2:
		printerr("usage: -- LEVEL FILE.tas")
		quit(3)
		return
	var assets := ProjectSettings.globalize_path("res://").path_join("../assets").simplify_path()
	var lv := Levels.new()
	if not lv.load_dir(assets):
		printerr("no levels")
		quit(3)
		return
	var sim := Sim.new()
	sim.setup(lv.levels, lv.ncampaign, assets)
	var l := args[0].to_int()
	if l < 0 or l >= lv.levels.size():
		printerr("xtrace: no level %d (there are %d)" % [l, lv.levels.size()])
		quit(3)
		return
	sim.load_level(l)
	var bs := OS.get_environment("SIM_BOSS")
	if bs != "":
		var n := bs.to_int()
		if n >= sim.bz.size():
			printerr("xtrace: level %d has no boss %d" % [l, n])
			quit(3)
			return
		var b = sim.bz[n]
		sim.spawn(b.room, ((b.ax0 + 4) * 8) << 8, ((b.my - 3) * 8) << 8)
	var at := OS.get_environment("SIM_AT")   # SIM_AT=ROOM,X,Y: start standing on that cell (tiles)
	if at != "":
		var p := at.split(",")
		sim.spawn(p[0].to_int(), (p[1].to_int() * 8 + 1) << 8, (p[2].to_int() * 8 - 3) << 8)
	var f := FileAccess.open(args[1], FileAccess.READ)
	if f == null:
		printerr("xtrace: cannot read %s (relative paths start in remake/: try ../tas/...)" % args[1])
		quit(3)
		return
	var respawn := OS.get_environment("SIM_RESPAWN") != ""
	var frame := 0
	var out := PackedStringArray()
	for ln in f.get_as_text().split("\n"):
		var parts := ln.strip_edges().split(" ", false)
		if parts.size() < 2:
			continue
		var n := parts[0].to_int()
		var b: String = parts[1].substr(0, 31)
		var k := 0
		for c in b:
			match c:
				"L": k |= 1
				"R": k |= 2
				"U": k |= 4
				"D": k |= 8
				"J": k |= 16
				"C": k |= 32
				"V": k |= Sim.CAP2
		for i in n:
			sim.tick(k)
			sim.ev.clear()
			sim.dirty.clear()
			frame += 1
			out.append(line(sim, frame, b))
			if sim.st == Sim.DEAD and sim.stt == 0 and not respawn:
				out.append("DIED frame %d" % frame)
				_done(out, 1)
				return
			if sim.st == Sim.WIN and sim.stt == 1:
				out.append("GOAL frame %d" % frame)
				_done(out, 0)
				return
	out.append("END frame %d" % frame)
	_done(out, 2)


func _done(out: PackedStringArray, code: int) -> void:
	print("\n".join(out))
	quit(code)
