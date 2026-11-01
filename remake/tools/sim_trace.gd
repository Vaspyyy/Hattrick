# Replays a recorded route (tas/*.tas, the C simulator's format) through the GDScript port and
# prints the same per-frame trace as `./sim LEVEL FILE trace`, so the two can be diffed:
#   godot --headless --path remake --script res://tools/sim_trace.gd -- LEVEL FILE.tas [trace]
# Without "trace" it prints only the outcome line (GOAL / DIED / END), like the C simulator.
extends SceneTree

const Levels = preload("res://src/sim/levels.gd")
const Sim = preload("res://src/sim/sim.gd")


func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() < 2:
		printerr("usage: -- LEVEL FILE.tas [trace]")
		quit(3)
		return
	var assets := ProjectSettings.globalize_path("res://").path_join("../assets").simplify_path()
	var lv := Levels.new()
	if not lv.load_dir(assets):
		printerr("no levels")
		quit(3)
		return
	var sim := Sim.new()
	sim.setup(lv.levels, lv.ncampaign)
	sim.load_level(args[0].to_int())
	var trace := args.size() > 2 and args[2] == "trace"
	var f := FileAccess.open(args[1], FileAccess.READ)
	var frame := 0
	var words := f.get_as_text().split("\n")
	var respawn := OS.get_environment("SIM_RESPAWN") != ""
	for line in words:
		var parts := line.strip_edges().split(" ", false)
		if parts.size() < 2:
			continue
		var n := parts[0].to_int()
		var b: String = parts[1]
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
			if trace:
				var s := "%d %s x=%.1f y=%.1f vx=%d vy=%d st=%d gnd=%d cap=%d room=%d score=%d wall=%d moons=%d" % [
					frame, b, sim.hx / 256.0, sim.hy / 256.0, sim.hvx, sim.hvy, sim.st, sim.gnd, sim.cst,
					sim.room, sim.score, sim.wall, sim.wd.moongot]
				for e in sim.wd.en:
					if e.a and absi(e.x - sim.hx) < (160 << 8):
						s += " e=%d,%d" % [e.x >> 8, e.y >> 8]
				print(s)
			if sim.st == Sim.DEAD and sim.stt == 0 and not respawn:
				print("DIED frame %d at x=%d y=%d (tile %d,%d)" % [frame, sim.hx >> 8, sim.hy >> 8, sim.hx >> 11, sim.hy >> 11])
				quit(1)
				return
			if sim.st == Sim.WIN and sim.stt == 1:
				var moons := 0
				for j in 3:
					moons += (sim.wd.moongot >> j) & 1
				print("GOAL frame %d (%.2fs) coins %d score %d deaths %d moons %d/%d" % [frame, frame / 60.0, sim.coins, sim.score, sim.deaths, moons, sim.L.moons.size()])
				quit(0)
				return
	print("END frame %d at x=%d y=%d (tile %d,%d) st=%d" % [frame, sim.hx >> 8, sim.hy >> 8, sim.hx >> 11, sim.hy >> 11, sim.st])
	quit(2)
