# Prints everything src/sim/levels.gd reads from assets/levels.txt + worlds.txt, in the format of
# `ctrace dump` (tools/ctrace.c), so the two readers can be diffed:
#   godot --headless --path remake --script res://tools/levels_dump.gd > gd.txt; ctrace dump > c.txt
extends SceneTree

const Levels := preload("res://src/sim/levels.gd")


func _init() -> void:
	var lv := Levels.new()
	lv.load_dir("../assets")
	for p in lv.problems:
		printerr("levels: ", p)
	var out := PackedStringArray()
	out.append("play=%d nsec=%d ncamp=%d" % [lv.play, lv.nsec, lv.ncampaign])
	for i in lv.levels.size():
		var L: Dictionary = lv.levels[i]
		var g: Array = L.gim
		var gr: Array = L.gimroom
		out.append("%d %s music=%s time=%d card=%d lab=%d boss=%d mini=%d theme=%d av=%d before=%s gim=%d,%d,%d,%d,%d gr=%d,%d,%d,%d" % [i, L.name, L.music, L.time, L.card, L.lab, L.boss, L.mini, L.theme, L.avalanche, L.before, g[0], g[1], g[2], g[3], g[4], gr[0], gr[1], gr[2], gr[3]])
		var c: Dictionary = L.cl
		out.append(" cl=%d,%d,%d,%d,%d,%d,%d,%d,%d,%s" % [c.star, c.sg, c.gr, c.gx, c.gy, c.pc, c.pr, c.px, c.py, c.secretof])
		for r in L.rooms.size():
			var R: Dictionary = L.rooms[r]
			var s := 0
			for k in R.grid:
				s = (s * 31 + k) & 0xFFFFFFFF
			out.append(" room%d=%d,%d,%d,%d" % [r, R.w, R.h, R.cave, s])
		for k in L.tubes.size():
			var t: Dictionary = L.tubes[k]
			out.append(" t%d=%d,%d,%d,%d,%d,%d" % [k, t.room, t.x, t.y, t.dir, t.id, t.link])
		for k in L.bars.size():
			var b: Dictionary = L.bars[k]
			out.append(" b%d=%d,%d,%d,%d,%d,%d" % [k, b.room, b.x, b.y, b.len, b.speed, b.a0])
		for k in L.homes.size():
			var hm: Dictionary = L.homes[k]
			out.append(" h%d=%d,%d,%d" % [k, hm.room, hm.tube, hm.spit])
		for k in L.moons.size():
			var m: Dictionary = L.moons[k]
			out.append(" m%d=%d,%d,%d" % [k, m.room, m.x, m.y])
	print("\n".join(out))
	quit(0)
