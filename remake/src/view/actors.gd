# The enemies and the thrown cap, drawn where the sim has them (interpolated between steps).
# Walkers are grumpy purple blobs that waddle and hop on the music's "bah"; buzzers are bees.
# enemies.h's crowd: masked shy-walkers, shell walkers and their shells, cap-thief crows, cloud
# riders, chained chomps, puffers, stone thwomps, beat blocks and beat pistons; worlds.c's crabs.
# A knocked-out enemy leaves the sim at once, so its knock-out (squashed flat by a stomp, sent
# spinning by the cap or a slide) plays here on its own.
extends Node3D

const Sim := preload("res://src/sim/sim.gd")
const Meshes := preload("res://src/view/meshes.gd")
const Mats := preload("res://src/view/mats.gd")
const Hatrick := preload("res://src/view/hatrick.gd")

const PURPLE := Color("9a48d0")
const PURPLE_DARK := Color("5a2088")
const BEE := Color("ffd23c")
const BEE_DARK := Color("2a2a2a")

var sim
var lh := 32
var nodes: Array = []      # per enemy index: Node3D or null
var prev: Array = []       # per enemy index: Vector2 position (px) before the last step
var yaw: Array = []
var corpses: Array = []    # [node, kind, t, vel]
var cap: Node3D
var cap_spin: Node3D
var cap_prev := Vector2.ZERO
var t := 0.0
var hop := 0.0             # 0..1 through a hop on the music's "bah"


func _ready() -> void:
	cap = Node3D.new()
	add_child(cap)
	cap_spin = Node3D.new()
	cap_spin.scale = Vector3.ONE * 1.35
	cap.add_child(cap_spin)
	var inner := Node3D.new()
	inner.position.y = -0.12
	cap_spin.add_child(inner)
	Hatrick.build_cap(inner, func(c: Color) -> Material: return Mats.toon(c, true, 0.5, 0.35, 0.016))
	cap.visible = false


func build(s) -> void:
	sim = s
	lh = s.lh
	for n in nodes:
		if n: n.queue_free()
	nodes.clear()
	prev.clear()
	yaw.clear()
	for c in corpses:
		c[0].queue_free()
	corpses.clear()
	_grow()
	cap_prev = Vector2(s.cxp, s.cyp) / 256.0


# A node for every enemy of this area, including ones that came along since (a cloud rider drops
# walkers).
func _grow() -> void:
	while nodes.size() < sim.wd.en.size():
		var i := nodes.size()
		var e = sim.wd.en[i]
		var n: Node3D = null
		if e.r == sim.room and e.a:
			n = model(e)
			add_child(n)
		nodes.append(n)
		prev.append(Vector2(e.x, e.y) / 256.0)
		yaw.append(-50.0)


func model(e) -> Node3D:
	match e.t:
		1: return walker()
		Sim.E_CRAB: return crab()
		Sim.T_SHY: return shy()
		Sim.T_SHELL: return shell()
		Sim.T_THIEF: return crow()
		Sim.T_RIDER: return rider()
		Sim.T_CHOMP: return chomp(e)
		Sim.T_PUFF: return puffer()
		Sim.T_THWOMP: return thwomp()
		Sim.T_BEATPLAT: return beatblocks(e.w)
		Sim.T_PISTON: return piston(e)
	return buzzer()


# Before each step: where everything was, for interpolation.
func snapshot() -> void:
	for i in mini(prev.size(), sim.wd.en.size()):
		var e = sim.wd.en[i]
		prev[i] = Vector2(e.x, e.y) / 256.0
	cap_prev = Vector2(sim.cxp, sim.cyp) / 256.0


func on_event(e: Array) -> void:
	if e[0] == "kill":
		var i: int = e[1]
		if i < nodes.size() and nodes[i]:
			var n: Node3D = nodes[i]
			nodes[i] = null
			var en = sim.wd.en[i]
			var how: String = e[2]
			if how == "stomp":
				corpses.append([n, "squash", 0.0, Vector3.ZERO])
			else:
				var dir := 1.0 if (sim.hx >> 8) + 3 < (en.x >> 8) + 4 else -1.0
				corpses.append([n, "fly", 0.0, Vector3(dir * 5.0, 11.0, 2.0)])


static func _part(parent: Node3D, mesh: Mesh, c: Color, pos := Vector3.ZERO, rot := Vector3.ZERO, outline := true) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.position = pos
	mi.rotation_degrees = rot
	mi.material_override = Mats.toon(c, outline, 0.5, 0.4, 0.02)
	parent.add_child(mi)
	return mi


static func walker(c := PURPLE, dark := PURPLE_DARK, ink := Color("301040")) -> Node3D:
	var n := Node3D.new()
	var body := Node3D.new()
	body.name = "body"
	n.add_child(body)
	_part(body, Meshes.ellipsoid(Vector3(0.46, 0.4, 0.42)), c, Vector3(0, 0.44, 0))
	_part(body, Meshes.ellipsoid(Vector3(0.3, 0.18, 0.3)), c.lightened(0.25), Vector3(0, 0.62, 0.12), Vector3(-30, 0, 0), false)
	for sx in [-1, 1]:
		var eye := _part(body, Meshes.ellipsoid(Vector3(0.12, 0.15, 0.06)), Color.WHITE, Vector3(sx * 0.15, 0.5, 0.37), Vector3(-5, sx * 18, 0))
		_part(eye, Meshes.ellipsoid(Vector3(0.06, 0.08, 0.04)), ink, Vector3(sx * -0.015, -0.02, 0.04), Vector3.ZERO, false)
		# angry brows sloping in
		_part(body, Meshes.capsule(0.035, 0.22), ink, Vector3(sx * 0.15, 0.68, 0.36), Vector3(-10, sx * 18, 90 - sx * 25), false)
		var foot := _part(n, Meshes.ellipsoid(Vector3(0.15, 0.1, 0.2)), dark, Vector3(sx * 0.2, 0.08, 0.05))
		foot.name = "foot" + ("L" if sx < 0 else "R")
	# a frown with two little fangs
	_part(body, Meshes.capsule(0.03, 0.2), ink, Vector3(0, 0.32, 0.4), Vector3(0, 0, 90), false)
	for sx in [-1, 1]:
		_part(body, Meshes.cylinder(0.0, 0.035, 0.07, 6), Color.WHITE, Vector3(sx * 0.06, 0.29, 0.405), Vector3(180, 0, 0), false)
	return n


static func buzzer(c := BEE) -> Node3D:
	var n := Node3D.new()
	var body := Node3D.new()
	body.name = "body"
	body.position.y = 0.5
	n.add_child(body)
	_part(body, Meshes.ellipsoid(Vector3(0.36, 0.32, 0.32)), c)
	var band := _part(body, Meshes.torus(0.29, 0.35, 24, 6), BEE_DARK, Vector3(-0.08, 0, 0), Vector3(0, 0, 90), false)
	band.scale = Vector3(1, 1, 0.95)
	_part(body, Meshes.torus(0.24, 0.31, 24, 6), BEE_DARK, Vector3(-0.22, 0, 0), Vector3(0, 0, 90), false)
	_part(body, Meshes.cylinder(0.0, 0.07, 0.18, 8), BEE_DARK, Vector3(-0.42, -0.02, 0), Vector3(0, 0, 90), false)
	for sx in [-1, 1]:
		_part(body, Meshes.ellipsoid(Vector3(0.08, 0.1, 0.05)), Color.WHITE, Vector3(0.22, 0.08, sx * 0.17), Vector3(0, -sx * 50, 0))
		_part(body, Meshes.ellipsoid(Vector3(0.04, 0.06, 0.03)), BEE_DARK, Vector3(0.27, 0.08, sx * 0.2), Vector3(0, -sx * 50, 0), false)
	var wm := StandardMaterial3D.new()
	wm.albedo_color = Color(0.92, 0.97, 1.0, 0.55)
	wm.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	wm.roughness = 0.2
	for sz in [-1, 1]:
		var w := Node3D.new()
		w.name = "wing" + ("L" if sz < 0 else "R")
		w.position = Vector3(0.02, 0.24, sz * 0.08)
		body.add_child(w)
		var m := MeshInstance3D.new()
		m.mesh = Meshes.ellipsoid(Vector3(0.16, 0.03, 0.26))
		m.position = Vector3(0, 0.02, sz * 0.22)
		m.material_override = wm
		m.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		w.add_child(m)
	return n


# ---------- enemies.h and worlds.c ----------
static func _eyes(body: Node3D, y: float, z: float, gap: float, r: float, pupil: Color, angry := false) -> void:
	for sx in [-1, 1]:
		var eye := _part(body, Meshes.ellipsoid(Vector3(r, r * 1.25, r * 0.5)), Color.WHITE, Vector3(sx * gap, y, z), Vector3(-5, sx * 18, 0))
		_part(eye, Meshes.ellipsoid(Vector3(r * 0.5, r * 0.65, r * 0.33)), pupil, Vector3(0, -r * 0.15, r * 0.35), Vector3.ZERO, false)
		if angry:
			_part(body, Meshes.capsule(r * 0.28, r * 1.9), pupil, Vector3(sx * gap, y + r * 1.4, z - 0.01), Vector3(-10, sx * 18, 90 - sx * 25), false)


static func _feet(n: Node3D, c: Color, w := 0.2) -> void:
	for sx in [-1, 1]:
		var foot := _part(n, Meshes.ellipsoid(Vector3(0.14, 0.09, 0.19)), c, Vector3(sx * w, 0.08, 0.05))
		foot.name = "foot" + ("L" if sx < 0 else "R")


static func shy() -> Node3D:   # u: a red walker behind a white mask that blocks the cap
	var n := Node3D.new()
	var body := Node3D.new()
	body.name = "body"
	n.add_child(body)
	var red := Color("d83a3a")
	_part(body, Meshes.ellipsoid(Vector3(0.4, 0.42, 0.38)), red, Vector3(0, 0.46, 0))
	_part(body, Meshes.ellipsoid(Vector3(0.42, 0.14, 0.4)), red.darkened(0.25), Vector3(0, 0.2, 0), Vector3.ZERO, false)
	var mask := Node3D.new()
	mask.name = "mask"
	mask.position = Vector3(0, 0.52, 0.3)
	body.add_child(mask)
	_part(mask, Meshes.ellipsoid(Vector3(0.3, 0.34, 0.1)), Color("f4f0e8"))
	for sx in [-1, 1]:
		_part(mask, Meshes.ellipsoid(Vector3(0.06, 0.09, 0.04)), Color("2a1418"), Vector3(sx * 0.11, 0.06, 0.08), Vector3.ZERO, false)
	_part(mask, Meshes.ellipsoid(Vector3(0.08, 0.04, 0.04)), Color("2a1418"), Vector3(0, -0.15, 0.08), Vector3.ZERO, false)
	_feet(n, Color("2a1418"))
	return n


static func shell() -> Node3D:   # k: a shell walker; its shell (s 1, 2) is the same node, tucked in
	var n := Node3D.new()
	var body := Node3D.new()
	body.name = "body"
	n.add_child(body)
	var green := Color("3cb04a")
	var skin := Color("f8d878")
	var sh := Node3D.new()
	sh.name = "shell"
	sh.position.y = 0.36
	body.add_child(sh)
	_part(sh, Meshes.ellipsoid(Vector3(0.44, 0.34, 0.4)), green, Vector3(0, 0.04, 0))
	_part(sh, Meshes.torus(0.36, 0.46, 24, 6), Color("f4f4e0"), Vector3(0, -0.06, 0))
	for i in 5:
		var a := TAU * i / 5.0
		_part(sh, Meshes.ellipsoid(Vector3(0.12, 0.06, 0.12)), green.lightened(0.3), Vector3(cos(a) * 0.2, 0.3, sin(a) * 0.2), Vector3(0, 0, 0), false)
	var head := Node3D.new()
	head.name = "head"
	head.position = Vector3(0, 0.62, 0.3)
	body.add_child(head)
	_part(head, Meshes.ellipsoid(Vector3(0.2, 0.22, 0.2)), skin)
	_eyes(head, 0.04, 0.16, 0.08, 0.06, Color("1c3020"))
	_feet(n, skin, 0.22)
	return n


static func crow() -> Node3D:   # v: a cap-thief crow
	var n := Node3D.new()
	var body := Node3D.new()
	body.name = "body"
	body.position.y = 0.5
	n.add_child(body)
	var c := Color("3a2c5a")
	_part(body, Meshes.ellipsoid(Vector3(0.3, 0.28, 0.26)), c)
	_part(body, Meshes.ellipsoid(Vector3(0.2, 0.2, 0.2)), c, Vector3(0.2, 0.18, 0))
	_part(body, Meshes.cylinder(0.0, 0.08, 0.24, 8), Color("ffc830"), Vector3(0.42, 0.16, 0), Vector3(0, 0, -90))
	for sz in [-1, 1]:
		_part(body, Meshes.ellipsoid(Vector3(0.05, 0.06, 0.03)), Color.WHITE, Vector3(0.3, 0.26, sz * 0.12))
		_part(body, Meshes.ellipsoid(Vector3(0.025, 0.03, 0.02)), Color("1a1428"), Vector3(0.32, 0.26, sz * 0.14), Vector3.ZERO, false)
		var w := Node3D.new()
		w.name = "wing" + ("L" if sz < 0 else "R")
		w.position = Vector3(-0.02, 0.1, sz * 0.18)
		body.add_child(w)
		_part(w, Meshes.ellipsoid(Vector3(0.2, 0.04, 0.3)), c.lightened(0.12), Vector3(-0.05, 0, sz * 0.24))
	_part(body, Meshes.ellipsoid(Vector3(0.18, 0.04, 0.12)), c.darkened(0.2), Vector3(-0.34, 0.05, 0), Vector3(0, 0, 20))
	for sz in [-1, 1]:
		_part(body, Meshes.capsule(0.025, 0.2), Color("ffc830"), Vector3(0.02, -0.3, sz * 0.08), Vector3.ZERO, false)
	return n


static func cloud(parent: Node3D, c: Color, scale := 1.0) -> void:
	for i in 3:
		var r := (0.34 if i == 1 else 0.27) * scale
		_part(parent, Meshes.ellipsoid(Vector3(r * 1.1, r, r)), c if i == 1 else c.darkened(0.06), Vector3((i - 1) * 0.42 * scale, (0.08 if i == 1 else 0.0) * scale, 0))
	_part(parent, Meshes.ellipsoid(Vector3(0.75 * scale, 0.16 * scale, 0.32 * scale)), c.darkened(0.12), Vector3(0, -0.14 * scale, 0), Vector3.ZERO, false)


static func rider() -> Node3D:   # L: rides a cloud above Hatrick, dropping walkers
	var n := Node3D.new()
	var body := Node3D.new()
	body.name = "body"
	body.position = Vector3(0.0, 0.55, 0)
	n.add_child(body)
	cloud(body, Color("f4f8ff"))
	var pilot := Node3D.new()
	pilot.position.y = 0.3
	body.add_child(pilot)
	_part(pilot, Meshes.ellipsoid(Vector3(0.26, 0.24, 0.24)), Color("f0a040"))
	_part(pilot, Meshes.torus(0.13, 0.2, 16, 6), Color("302010"), Vector3(0, 0.08, 0.14), Vector3(90, 0, 0), false)
	for sx in [-1, 1]:
		_part(pilot, Meshes.ellipsoid(Vector3(0.08, 0.07, 0.04)), Color("60d8ff"), Vector3(sx * 0.09, 0.08, 0.2), Vector3.ZERO, false)
	var drop := walker()
	drop.name = "drop"
	drop.scale = Vector3.ONE * 0.8
	drop.position = Vector3(0, -0.85, 0)
	drop.visible = false
	body.add_child(drop)
	return n


static func chomp(e) -> Node3D:   # x: a chained chomp; the node sits at its stake, the ball moves
	var n := Node3D.new()
	var ball := Node3D.new()
	ball.name = "body"
	n.add_child(ball)
	_part(ball, Meshes.ellipsoid(Vector3(0.62, 0.62, 0.58)), Color("181820"), Vector3.ZERO, Vector3.ZERO, true)
	_part(ball, Meshes.ellipsoid(Vector3(0.2, 0.14, 0.1)), Color(0.5, 0.52, 0.6), Vector3(-0.22, 0.32, 0.42), Vector3(0, 0, 25), false)
	for sx in [-1, 1]:
		var eye := _part(ball, Meshes.ellipsoid(Vector3(0.1, 0.12, 0.05)), Color.WHITE, Vector3(sx * 0.17, 0.2, 0.56), Vector3(0, sx * 15, 0), false)
		_part(eye, Meshes.ellipsoid(Vector3(0.045, 0.06, 0.03)), Color("101018"), Vector3(0.02, 0, 0.03), Vector3.ZERO, false)
	var jaw := Node3D.new()
	jaw.name = "jaw"
	jaw.position = Vector3(0.0, -0.14, 0.42)
	ball.add_child(jaw)
	_part(jaw, Meshes.ellipsoid(Vector3(0.3, 0.12, 0.2)), Color("d02838"), Vector3.ZERO, Vector3.ZERO, false)
	for i in 5:
		_part(jaw, Meshes.cylinder(0.0, 0.04, 0.09, 6), Color.WHITE, Vector3(-0.2 + i * 0.1, 0.1, 0.12), Vector3(180, 0, 0), false)
	var stake := Node3D.new()
	stake.name = "stake"
	n.add_child(stake)
	_part(stake, Meshes.cylinder(0.08, 0.1, 0.8, 10), Color("b07a40"), Vector3(0, 0.0, 0))
	_part(stake, Meshes.cylinder(0.13, 0.13, 0.08, 12), Color("6a4020"), Vector3(0, 0.38, 0))
	var chain := Node3D.new()
	chain.name = "chain"
	n.add_child(chain)
	for i in 4:
		var l := _part(chain, Meshes.torus(0.05, 0.11, 12, 6), Color("8890a0"), Vector3.ZERO, Vector3(90 if i & 1 else 0, 0, 0), false)
		l.name = "l%d" % i
	return n


static func puffer() -> Node3D:   # p: a pink buzzer the cap blows up into a bouncy ball
	var n := Node3D.new()
	var body := Node3D.new()
	body.name = "body"
	body.position.y = 0.5
	n.add_child(body)
	var small := Node3D.new()
	small.name = "small"
	body.add_child(small)
	_part(small, Meshes.ellipsoid(Vector3(0.34, 0.32, 0.32)), Color("f080a8"))
	_eyes(small, 0.08, 0.26, 0.12, 0.07, Color("1a1020"))
	for sz in [-1, 1]:
		var w := _part(small, Meshes.ellipsoid(Vector3(0.12, 0.03, 0.2)), Color(1, 0.85, 0.9), Vector3(-0.04, 0.3, sz * 0.18), Vector3.ZERO, false)
		w.name = "wing" + ("L" if sz < 0 else "R")
	var big := Node3D.new()
	big.name = "big"
	big.visible = false
	body.add_child(big)
	_part(big, Meshes.ellipsoid(Vector3(0.95, 0.9, 0.9)), Color("ffa0c8"))
	for i in 8:
		var a := TAU * i / 8.0
		_part(big, Meshes.cylinder(0.0, 0.07, 0.2, 6), Color("c04870"), Vector3(cos(a) * 0.95, sin(a) * 0.9, 0), Vector3(0, 0, rad_to_deg(a) - 90), false)
	_eyes(big, 0.25, 0.8, 0.28, 0.1, Color("1a1020"))
	return n


static func thwomp() -> Node3D:   # Q: a 2 x 2 stone face that drops on Hatrick
	var n := Node3D.new()
	var body := Node3D.new()
	body.name = "body"
	n.add_child(body)
	var mi := MeshInstance3D.new()
	mi.mesh = Meshes.rounded_box(Vector3(1.96, 1.96, 1.6), 0.18, 3)
	mi.position.y = 0.98
	mi.material_override = Mats.toon(Color("98a2b2"), true, 0.85, 0.2, 0.02)
	body.add_child(mi)
	for i in 6:   # chiselled studs on the rim
		var a := TAU * i / 6.0 + 0.5
		_part(body, Meshes.cylinder(0.0, 0.12, 0.22, 6), Color("7a8494"), Vector3(cos(a) * 1.08, 0.98 + sin(a) * 1.08, 0), Vector3(0, 0, rad_to_deg(a) - 90), false)
	var face := Node3D.new()
	face.name = "face"
	face.position = Vector3(0, 0.98, 0.81)
	body.add_child(face)
	for sx in [-1, 1]:
		var eye := _part(face, Meshes.ellipsoid(Vector3(0.22, 0.2, 0.06)), Color.WHITE, Vector3(sx * 0.42, 0.18, 0), Vector3.ZERO, false)
		var pupil := _part(eye, Meshes.ellipsoid(Vector3(0.09, 0.09, 0.04)), Color("101018"), Vector3(0, 0, 0.04), Vector3.ZERO, false)
		pupil.name = "pupil"
		eye.name = "eye" + ("L" if sx < 0 else "R")
		var brow := _part(face, Meshes.rounded_box(Vector3(0.5, 0.1, 0.1), 0.03, 1), Color("202430"), Vector3(sx * 0.42, 0.44, 0.02), Vector3.ZERO, false)
		brow.name = "brow" + ("L" if sx < 0 else "R")
	var teeth := _part(face, Meshes.rounded_box(Vector3(1.2, 0.2, 0.06), 0.04, 1), Color.WHITE, Vector3(0, -0.42, 0), Vector3.ZERO, false)
	for i in 5:
		_part(teeth, Meshes.rounded_box(Vector3(0.03, 0.2, 0.07), 0.01, 1), Color("404858"), Vector3(-0.4 + i * 0.2, 0, 0), Vector3.ZERO, false)
	return n


static func beatblocks(w: int) -> Node3D:   # Y: a run of blocks that hops two tiles on the beat
	var n := Node3D.new()
	var body := Node3D.new()
	body.name = "body"
	n.add_child(body)
	for i in w:
		var b := MeshInstance3D.new()
		b.mesh = Meshes.rounded_box(Vector3(0.96, 0.96, 0.96), 0.12, 2)
		b.position = Vector3(i + 0.5, 0.5, 0)
		b.material_override = Mats.toon(Color("b060ff"), true, 0.4, 0.5, 0.02)
		b.name = "b%d" % i
		body.add_child(b)
		var note := Meshes.extrude(PackedVector2Array([Vector2(-0.06, -0.2), Vector2(0.04, -0.2), Vector2(0.04, 0.18), Vector2(0.16, 0.12), Vector2(0.16, 0.22), Vector2(0.04, 0.28), Vector2(-0.06, 0.28)]), 0.04, "note_stem")
		_part(b, note, Color.WHITE, Vector3(0.04, 0.0, 0.49), Vector3.ZERO, false)
		_part(b, Meshes.ellipsoid(Vector3(0.11, 0.08, 0.03)), Color.WHITE, Vector3(-0.04, -0.2, 0.5), Vector3(0, 0, 20), false)
	return n


static func piston(e) -> Node3D:   # Z: a stone block that shoots four spikes out of its free side
	var n := Node3D.new()
	var body := Node3D.new()
	body.name = "body"
	n.add_child(body)
	var dir := Vector3(e.vx, -e.vy, 0)
	body.basis = Basis(Vector3.BACK, Vector3.UP.signed_angle_to(dir, Vector3.BACK))
	var plate := MeshInstance3D.new()
	plate.mesh = Meshes.rounded_box(Vector3(0.98, 0.1, 0.98), 0.03, 1)
	plate.position.y = 0.5
	plate.material_override = Mats.toon(Color("ffd030"), false, 0.5, 0.3)
	body.add_child(plate)
	for i in 3:
		_part(body, Meshes.rounded_box(Vector3(0.14, 0.11, 1.0), 0.02, 1), Color("202020"), Vector3(-0.3 + i * 0.3, 0.51, 0), Vector3(0, 30, 0), false)
	var spikes := Node3D.new()
	spikes.name = "spikes"
	spikes.position.y = 0.5
	body.add_child(spikes)
	for i in 4:
		var c := MeshInstance3D.new()
		c.mesh = Meshes.cylinder(0.0, 0.13, 0.9, 10)
		c.position = Vector3(-0.3 + 0.2 * i, 0.45, 0)
		c.material_override = Mats.pbr(Color(0.86, 0.88, 0.93), 0.3, 0.7)
		spikes.add_child(c)
	return n


static func crab() -> Node3D:   # c: a crab that raises its claws now and then
	var n := Node3D.new()
	var body := Node3D.new()
	body.name = "body"
	n.add_child(body)
	var red := Color("e8503a")
	_part(body, Meshes.ellipsoid(Vector3(0.44, 0.24, 0.34)), red, Vector3(0, 0.34, 0))
	_part(body, Meshes.ellipsoid(Vector3(0.26, 0.08, 0.2)), Color("ff8a6a"), Vector3(0, 0.52, 0.04), Vector3.ZERO, false)
	for sx in [-1, 1]:
		_part(body, Meshes.capsule(0.03, 0.22), Color("a02a1a"), Vector3(sx * 0.12, 0.6, 0.12), Vector3.ZERO, false)
		var eye := _part(body, Meshes.ellipsoid(Vector3(0.07, 0.08, 0.07)), Color.WHITE, Vector3(sx * 0.12, 0.74, 0.14))
		_part(eye, Meshes.ellipsoid(Vector3(0.035, 0.04, 0.03)), Color("1a1010"), Vector3(0, 0, 0.05), Vector3.ZERO, false)
		for j in 3:
			_part(body, Meshes.capsule(0.025, 0.26), Color("a02a1a"), Vector3(sx * (0.28 + j * 0.05), 0.14, -0.15 + j * 0.15), Vector3(0, 0, sx * 50), false)
		var arm := Node3D.new()
		arm.name = "claw" + ("L" if sx < 0 else "R")
		arm.position = Vector3(sx * 0.4, 0.38, 0.12)
		body.add_child(arm)
		_part(arm, Meshes.capsule(0.05, 0.3), red, Vector3(sx * 0.08, 0.1, 0), Vector3(0, 0, sx * -30), false)
		var pincer := _part(arm, Meshes.ellipsoid(Vector3(0.16, 0.12, 0.1)), red, Vector3(sx * 0.16, 0.26, 0))
		_part(pincer, Meshes.ellipsoid(Vector3(0.08, 0.05, 0.08)), Color("ff8a6a"), Vector3(sx * 0.06, 0.08, 0), Vector3.ZERO, false)
	return n


func update(alpha: float, dt: float) -> void:
	t += dt
	_grow()
	var lift := sin(hop * PI) if hop > 0.0 else 0.0
	for i in nodes.size():
		var n: Node3D = nodes[i]
		if n == null:
			continue
		var e = sim.wd.en[i]
		n.visible = e.a != 0 and e.r == sim.room
		if not n.visible:
			continue
		var cur := Vector2(e.x, e.y) / 256.0
		var p: Vector2 = prev[i].lerp(cur, alpha)
		if p.distance_to(cur) > 24.0:
			p = cur
		var body: Node3D = n.get_node("body")
		if e.t >= Sim.T_SHY or e.t == Sim.E_CRAB:
			_animate(i, e, n, body, p, cur, lift, dt)
			continue
		if e.t == 1:
			n.position = Vector3((p.x + 4.0) / 8.0, lh - (p.y + 8.0) / 8.0 + lift * 0.375, 0)
			var want := 50.0 if e.vx > 0 else -50.0
			yaw[i] = lerpf(yaw[i], want, 1.0 - exp(-10.0 * dt))
			n.rotation_degrees.y = yaw[i]
			var ph := p.x * 0.9
			body.rotation_degrees.z = sin(ph) * 7.0
			body.position.y = absf(sin(ph)) * 0.04
			var st := lift / 6.0 * 2.0
			body.scale = Vector3(1.0 - st * 0.5, 1.0 + st, 1.0 - st * 0.5)
			n.get_node("footL").position.y = 0.08 + maxf(0.0, sin(ph)) * 0.1
			n.get_node("footR").position.y = 0.08 + maxf(0.0, -sin(ph)) * 0.1
		else:
			n.position = Vector3((p.x + 4.0) / 8.0, lh - (p.y + 8.0) / 8.0 + lift * 0.25, 0)
			var f := sin(t * 60.0)
			body.get_node("wingL").rotation_degrees.x = -20.0 + f * 35.0
			body.get_node("wingR").rotation_degrees.x = 20.0 - f * 35.0
			body.rotation_degrees.z = sin(t * 3.0) * 6.0
			var dx: float = cur.x - prev[i].x
			var want: float = yaw[i]
			if dx > 0.01: want = -25.0
			elif dx < -0.01: want = 205.0
			elif e.t == 2: want = -60.0
			yaw[i] = lerpf(yaw[i], want, 1.0 - exp(-8.0 * dt))
			n.rotation_degrees.y = yaw[i]
	for c in corpses.duplicate():
		var n: Node3D = c[0]
		c[2] += dt
		if c[1] == "squash":
			n.scale = Vector3(1.4, maxf(0.2, 1.0 - c[2] * 12.0), 1.4)
			if c[2] > 0.45:
				n.queue_free()
				corpses.erase(c)
		else:
			var v: Vector3 = c[3]
			v.y -= 38.0 * dt
			c[3] = v
			n.position += v * dt
			n.rotation_degrees.z += 540.0 * dt * signf(v.x)
			n.rotation_degrees.x = 180.0
			if c[2] > 2.0:
				n.queue_free()
				corpses.erase(c)
	# the thrown cap: a 8 x 4 sprite at (cxp, cyp) in the C game
	cap.visible = sim.cst != 0 and sim.st != Sim.TUBE
	if cap.visible:
		var cur := Vector2(sim.cxp, sim.cyp) / 256.0
		var p := cap_prev.lerp(cur, alpha)
		if p.distance_to(cur) > 24.0:
			p = cur
		cap.position = Vector3((p.x + 4.0) / 8.0, lh - (p.y + 2.0) / 8.0, 0.0)
		if sim.cst == 4:   # resting on a post
			cap_spin.rotation = Vector3.ZERO
		else:
			cap_spin.rotation.y += dt * 26.0
			cap_spin.rotation.x = deg_to_rad(-12.0)


func _walk_cycle(n: Node3D, body: Node3D, ph: float, sway := 7.0) -> void:
	body.rotation_degrees.z = sin(ph) * sway
	body.position.y = absf(sin(ph)) * 0.04
	if n.has_node("footL"):
		n.get_node("footL").position.y = 0.08 + maxf(0.0, sin(ph)) * 0.1
		n.get_node("footR").position.y = 0.08 + maxf(0.0, -sin(ph)) * 0.1


func _face_toward(i: int, want: float, dt: float, n: Node3D, rate := 10.0) -> void:
	yaw[i] = lerpf(yaw[i], want, 1.0 - exp(-rate * dt))
	n.rotation_degrees.y = yaw[i]


func _animate(i: int, e, n: Node3D, body: Node3D, p: Vector2, cur: Vector2, lift: float, dt: float) -> void:
	var X: float = sim.hx / 256.0 + 3.0
	match e.t:
		Sim.T_SHY:
			n.position = Vector3((p.x + 4.0) / 8.0, lh - (p.y + 8.0) / 8.0 + lift * 0.375, 0)
			var want := 50.0 if e.vx > 0 else -50.0
			if e.u: want = lerpf(want, -want, 1.0 - e.u / 6.0)   # turning to face the cap
			_face_toward(i, want, dt, n, 14.0)
			_walk_cycle(n, body, p.x * 0.9)
		Sim.E_CRAB:
			n.position = Vector3((p.x + 4.0) / 8.0, lh - (p.y + 8.0) / 8.0 + lift * 0.3, 0)
			_face_toward(i, 0.0, dt, n)   # crabs walk sideways
			body.position.x = sin(p.x * 1.4) * 0.03
			var up: bool = sim.worldclaws(e)
			for side in ["L", "R"]:
				var arm: Node3D = body.get_node("claw" + side)
				var sx := -1.0 if side == "L" else 1.0
				var target := Vector3(0, 0, sx * -70.0) if up else Vector3(0, 0, sx * 10.0 + sin(t * 8.0) * 6.0)
				arm.rotation_degrees = arm.rotation_degrees.lerp(target, 1.0 - exp(-12.0 * dt))
		Sim.T_SHELL:
			n.position = Vector3((p.x + 4.0) / 8.0, lh - (p.y + 8.0) / 8.0 + (lift * 0.375 if e.s == 0 else 0.0), 0)
			var head: Node3D = body.get_node("head")
			var sh: Node3D = body.get_node("shell")
			head.visible = e.s == 0
			n.get_node("footL").visible = e.s == 0
			n.get_node("footR").visible = e.s == 0
			if e.s == 0:
				_face_toward(i, 50.0 if e.vx > 0 else -50.0, dt, n)
				_walk_cycle(n, body, p.x * 0.9, 5.0)
				sh.position.y = 0.36
				sh.rotation = Vector3.ZERO
			else:
				body.rotation = Vector3.ZERO
				body.position.y = 0.0
				sh.position.y = 0.28
				if e.s == 2:
					sh.rotation.y += dt * 30.0 * signf(e.vx)
				else:
					sh.rotation.y = 0.0
					sh.position.x = (0.04 if (sim.fr >> 1) & 1 else -0.04) if e.w > 360 else 0.0
		Sim.T_THIEF:
			n.position = Vector3((p.x + 4.0) / 8.0, lh - (p.y + 8.0) / 8.0, 0)
			var right: bool = (X > p.x + 4.0) if e.s == 0 else e.vx >= 0
			_face_toward(i, -20.0 if right else 200.0, dt, n, 8.0)
			var speed := 14.0 if e.s == 0 else 40.0
			var f := sin(t * speed)
			body.get_node("wingL").rotation_degrees.x = -10.0 + f * 40.0
			body.get_node("wingR").rotation_degrees.x = 10.0 - f * 40.0
			body.rotation_degrees.z = clampf(-e.vy / 20.0, -25.0, 25.0) if e.s else sin(t * 2.0) * 4.0
		Sim.T_RIDER:
			n.position = Vector3((p.x + 7.0) / 8.0, lh - (p.y + 8.0) / 8.0, 0)
			_face_toward(i, -15.0 if X > p.x + 7.0 else 195.0, dt, n, 5.0)
			body.rotation_degrees.z = sin(t * 2.2) * 5.0
			body.get_node("drop").visible = e.s != 0 and e.u > 125
		Sim.T_CHOMP:
			var sx: int = e.h >> 9
			var sy: int = e.h & 511
			var stake := Vector3(sx + 0.5, lh - sy - 0.5, 0)
			n.position = Vector3.ZERO
			var bc := Vector3((p.x + 5.0) / 8.0, lh - (p.y + 5.0) / 8.0, 0.05)
			if e.s == 1:
				bc.x += 0.08 if (sim.fr & 2) else -0.08
			body.position = bc
			var look := 1.0 if X > p.x + 5.0 else -1.0
			if e.s == 10: look = signf(e.vx)
			yaw[i] = lerpf(yaw[i], 40.0 * look, 1.0 - exp(-12.0 * dt))
			body.rotation_degrees.y = yaw[i]
			if e.s == 10:
				body.rotation_degrees.z -= dt * 400.0 * signf(e.vx)
			var jaw: Node3D = body.get_node("jaw")
			var open: bool = e.s >= 1 and e.s <= 3
			jaw.rotation_degrees.z = lerpf(jaw.rotation_degrees.z, -35.0 if open else -5.0, 1.0 - exp(-14.0 * dt))
			var st: Node3D = n.get_node("stake")
			st.visible = true
			st.position = stake + Vector3(0, 0.1 - e.w * 0.12, -0.1)
			var ch: Node3D = n.get_node("chain")
			ch.visible = e.s < 10
			for k in 4:
				var f := (k + 1) / 5.0
				var lk: Node3D = ch.get_node("l%d" % k)
				lk.position = stake.lerp(bc, f) + Vector3(0, -(f * (1.0 - f)) * (0.6 if e.s == 0 else 0.0), 0)
		Sim.T_PUFF:
			n.position = Vector3((p.x + 4.0) / 8.0, lh - (p.y + 8.0) / 8.0, 0)
			var small: Node3D = body.get_node("small")
			var big: Node3D = body.get_node("big")
			small.visible = e.s == 0
			big.visible = e.s != 0
			if e.s == 0:
				var f := sin(t * 60.0)
				small.get_node("wingL").rotation_degrees.x = -20.0 + f * 35.0
				small.get_node("wingR").rotation_degrees.x = 20.0 - f * 35.0
				big.scale = Vector3.ONE * 0.4
			else:
				var sq: float = e.vx / 8.0
				var target := Vector3(1.0 + sq * 0.25, 1.0 - sq * 0.3, 1.0 + sq * 0.25)
				big.scale = big.scale.lerp(target, 1.0 - exp(-20.0 * dt))
				big.visible = not (e.u < 60 and (sim.fr & 4))
			_face_toward(i, -25.0 if X > p.x + 4.0 else 205.0, dt, n, 6.0)
		Sim.T_THWOMP:
			n.position = Vector3((p.x + 8.0) / 8.0, lh - (p.y + 16.0) / 8.0, -0.2)
			var face: Node3D = body.get_node("face")
			var angry: bool = e.s == 1 or e.s == 2 or (e.s == 0 and absf(X - p.x - 8.0) < 40.0)
			var look := clampf((X - p.x - 8.0) / 40.0, -1.0, 1.0)
			for side in ["L", "R"]:
				var sx := -1.0 if side == "L" else 1.0
				face.get_node("eye" + side).get_node("pupil").position.x = look * 0.09
				var brow: Node3D = face.get_node("brow" + side)
				brow.rotation_degrees.z = lerpf(brow.rotation_degrees.z, sx * (-22.0 if angry else 0.0), 1.0 - exp(-14.0 * dt))
				brow.position.y = 0.4 if angry else 0.46
			body.position.x = (0.03 if (sim.fr & 2) else -0.03) if e.s == 0 and angry and e.u == 0 else 0.0
		Sim.T_BEATPLAT:
			var tx0: int = (e.x >> 11) + e.s
			var target := Vector3(tx0, lh - (e.y >> 11) - 1, 0)
			n.position = n.position.lerp(target, 1.0 - exp(-30.0 * dt)) if n.position.distance_to(target) < 3.0 else target
			var bp: int = sim.beatpos()
			var soon: bool = ((bp >> 8) & 3) == 3 and e.vx != 0
			var pulse: float = snappedf(maxf(0.0, 1.0 - (bp & 255) / 64.0), 0.125)   # few steps: materials are cached by colour
			body.scale = Vector3(1.0, 1.0 - pulse * 0.08, 1.0)
			for k in e.w:
				var b: MeshInstance3D = body.get_node("b%d" % k)
				b.material_override = Mats.toon(Color.WHITE if soon and (sim.fr & 4) else Color("b060ff").lerp(Color("ff70d0"), pulse), true, 0.4, 0.5, 0.02)
		Sim.T_PISTON:
			n.position = Vector3((e.x >> 11) + 0.5, lh - (e.y >> 11) - 0.5, 0)
			body.position = Vector3.ZERO
			var sp: Node3D = body.get_node("spikes")
			var f: int = e.u
			var warn: bool = not (e.w & 1) and f > 200
			var length := (minf(f / 24.0, 1.0) if e.s else (0.15 if warn else 0.0))
			sp.scale = Vector3(1, maxf(length, 0.01), 1)
			sp.visible = length > 0.0
