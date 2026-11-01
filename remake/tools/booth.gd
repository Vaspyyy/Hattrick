# Photo booth: poses Hatrick in each state and saves a sheet of screenshots, to check the model
# and the animation without playing. Needs a display (xvfb-run works):
#   godot --path remake --script res://tools/booth.gd -- OUT_DIR
extends SceneTree

const Hatrick := preload("res://src/view/hatrick.gd")
const Lighting := preload("res://src/view/lighting.gd")
const Sim := preload("res://src/sim/sim.gd")

var out := "/tmp"


func fake(over: Dictionary) -> Dictionary:
	var s := {
		"st": Sim.NORM, "face": 1, "gnd": 1, "hvx": 0, "hvy": 0, "cst": 0, "spin": 0, "spinlen": 1,
		"spind": 1, "twirl": 0, "duck": 0, "skid": 0, "jn": 0, "mv_flut": 0, "capx_flut": 0,
		"mv_swing": 0, "mv_wslide": 0, "mv_swim": 0, "mv_ang": 0, "capx_inv": 0, "fr": 0, "stt": 0,
		"wphase": 0, "wt": 0, "tubefrom": 0, "tubeto": 0, "L": {"tubes": []},
	}
	s.merge(over, true)
	return s


func _init() -> void:
	var a := OS.get_cmdline_user_args()
	if a.size() > 0:
		out = a[0]
	DirAccess.make_dir_recursive_absolute(out)
	call_deferred("_run")


func _run() -> void:
	var root3 := Node3D.new()
	get_root().add_child(root3)
	var lights := Lighting.new()
	root3.add_child(lights)
	var floor := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(12, 6)
	floor.mesh = pm
	var fm := StandardMaterial3D.new()
	fm.albedo_color = Color(0.45, 0.75, 0.35)
	floor.material_override = fm
	root3.add_child(floor)
	var cam := Camera3D.new()
	cam.fov = 30
	root3.add_child(cam)
	cam.look_at_from_position(Vector3(0, 1.3, 5.2), Vector3(0, 0.62, 0), Vector3.UP)
	var h := Hatrick.new()
	root3.add_child(h)
	get_root().size = Vector2i(640, 640)
	var poses := [
		["idle", {}, 1.5],
		["walk", {"hvx": 150}, 0.37],
		["run", {"hvx": 400}, 0.31],
		["skid", {"hvx": -300, "skid": 6}, 0.4],
		["crouch", {"duck": 4}, 0.5],
		["jump", {"gnd": 0, "hvy": -700}, 0.4],
		["double", {"gnd": 0, "hvy": -700, "jn": 1}, 0.4],
		["fall", {"gnd": 0, "hvy": 900}, 0.5],
		["triple", {"gnd": 0, "hvy": -900, "spin": 22, "spinlen": 44, "spind": 1, "jn": 2}, 0.3],
		["longjump", {"gnd": 0, "st": Sim.LONGJ, "hvx": 700, "hvy": -300}, 0.4],
		["dive", {"gnd": 0, "st": Sim.DIVE, "hvx": 760, "hvy": 200}, 0.4],
		["slide", {"st": Sim.SLIDE, "hvx": 500, "duck": 5}, 0.4],
		["roll", {"st": Sim.ROLL, "hvx": 571, "duck": 5}, 0.21],
		["gpslam", {"gnd": 0, "st": Sim.GPSLAM, "hvy": 1400, "duck": 4}, 0.4],
		["spinjump", {"gnd": 0, "st": Sim.SPINJ, "hvy": -300}, 0.33],
		["wallslide", {"gnd": 0, "hvy": 200, "mv_wslide": 1}, 0.4],
		["hang", {"st": Sim.HANG}, 0.4],
		["throw", {"_throw": true}, 0.12],
		["win", {"st": Sim.WIN, "wphase": 2, "wt": 20}, 0.8],
		["dead", {"st": Sim.DEAD, "gnd": 0}, 0.5],
	]
	for p in poses:
		var s := fake(p[1])
		h.on_event(["spawn"], s)
		if p[1].has("_throw"):
			h.on_event(["throw", Sim.CAPFORWARD], s)
		var dur: float = p[2]
		var steps := int(dur * 60)
		for i in steps:
			h.animate(s, Vector3.ZERO, 0.0, 1.0 / 60.0)
		await process_frame
		await process_frame
		var img := get_root().get_texture().get_image()
		img.save_png(out.path_join("pose_%s.png" % p[0]))
	quit(0)
