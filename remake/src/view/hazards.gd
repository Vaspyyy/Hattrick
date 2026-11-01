# Everything else the sim moves that isn't an enemy or a boss: fire bars, tube dwellers and their
# seeds, gimmicks.h's seesaws, scale lifts, flip flowers, gold blocks, doors, cannons, fans and
# cannonballs, the rising lava (or water), worlds.c's avalanche, and collect.h's postcard and secret
# exit. Built per area like the level view, then posed from the sim every frame.
extends Node3D

const Sim := preload("res://src/sim/sim.gd")
const Meshes := preload("res://src/view/meshes.gd")
const Mats := preload("res://src/view/mats.gd")
const Actors := preload("res://src/view/actors.gd")
const LAVA := preload("res://shaders/lava.gdshader")

const SNAPPER := Color("2f8f9a")
const SPITTER := Color("e0586a")
const WOOD := Color("c87a3a")

var sim
var lh := 32
var room := 0
var t := 0.0
var bars: Array = []     # [bar, pivot node, flame nodes]
var dws: Array = []      # [home index, node, head, jaw]
var seeds: Array = []
var plats: Array = []    # [index, node]
var obs: Array = []      # [index, node]
var balls: Array = []
var ball_prev: Array = []
var rise: Node3D
var aval: Node3D
var card: Node3D
var secret: Node3D
var secret_pennant: Node3D
var gold_hat: Node3D     # the gold block on Hatrick's head
var flame_mat: StandardMaterial3D
var flame_core: StandardMaterial3D


func _init() -> void:
	flame_mat = StandardMaterial3D.new()   # a soft orange halo round a white-hot core
	flame_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	flame_mat.albedo_color = Color(1.0, 0.36, 0.06, 0.7)
	flame_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	flame_mat.emission_enabled = true
	flame_mat.emission = Color(1.0, 0.35, 0.05)
	flame_mat.emission_energy_multiplier = 1.5
	flame_core = StandardMaterial3D.new()
	flame_core.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	flame_core.albedo_color = Color(1.0, 0.78, 0.25)
	flame_core.emission_enabled = true
	flame_core.emission = Color(1.0, 0.6, 0.12)
	flame_core.emission_energy_multiplier = 2.0


func px(x: float, y: float, z := 0.0) -> Vector3:   # game pixels (y down) to world units
	return Vector3(x / 8.0, lh - y / 8.0, z)


func build(s) -> void:
	sim = s
	lh = s.lh
	room = s.room
	for c in get_children():
		c.queue_free()
	bars.clear(); dws.clear(); seeds.clear(); plats.clear(); obs.clear(); balls.clear(); ball_prev.clear()
	rise = null; aval = null; card = null; secret = null; secret_pennant = null
	var L: Dictionary = s.L
	for b in L.bars:
		if b.room == room:
			_bar(b)
	for i in L.homes.size():
		if L.homes[i].room == room:
			_dweller(i)
	for i in s.wd.sh.size():
		var n := _seed()
		add_child(n)
		seeds.append(n)
	var gw = s.wd.gw
	for i in gw.pl.size():
		if gw.pl[i].room == room:
			var n := _seesaw(gw.pl[i]) if gw.pl[i].kind == 2 else _lift(gw.pl, i)
			add_child(n)
			plats.append([i, n])
	for i in gw.ob.size():
		var o = gw.ob[i]
		if o.room != room:
			continue
		var n: Node3D = null
		match o.kind:
			79: n = flip_flower()
			65: n = gold_block()
			68: n = _door()
			69: n = _cannon()
			87: n = _fan()
		if n:
			n.position = Vector3(o.x + 0.5, lh - o.y - 1, 0)
			add_child(n)
			obs.append([i, n])
	for i in gw.ball.size():
		var n := _ball()
		add_child(n)
		balls.append(n)
		ball_prev.append(Vector2.ZERO)
	if L.gim[Sim.GO_RISE] and room == 0:
		rise = _rise(L.gim[Sim.GO_RISE] == 1)
		add_child(rise)
	if L.avalanche and room == 0:
		aval = _avalanche()
		add_child(aval)
	var cl: Dictionary = L.cl
	if cl.pc and cl.pr == room and s.cl_record(s.lvl) and not s.cardgot:
		card = postcard()
		card.position = Vector3(cl.px + 0.5, lh - cl.py - 0.5, 0.1)
		add_child(card)
	if cl.sg and cl.gr == room:
		_secret_exit(cl)
	gold_hat = gold_block()
	gold_hat.scale = Vector3.ONE * 0.85
	gold_hat.visible = false
	add_child(gold_hat)


func on_event(e: Array) -> void:
	match e[0]:
		"postcard":
			if card:
				var tw := create_tween()
				tw.tween_property(card, "scale", Vector3.ONE * 1.6, 0.15)
				tw.tween_property(card, "scale", Vector3.ZERO, 0.25)
		"cannon":
			pass


# ---------- models ----------
func _bar(b: Dictionary) -> void:
	var pivot := Node3D.new()
	pivot.position = Vector3(b.x + 0.5, lh - b.y - 0.5, 0)
	add_child(pivot)
	var hub := MeshInstance3D.new()   # a riveted iron block the bar turns on
	hub.mesh = Meshes.rounded_box(Vector3(0.96, 0.96, 0.96), 0.1, 2)
	hub.material_override = Mats.pbr(Color(0.32, 0.3, 0.34), 0.45, 0.6)
	pivot.add_child(hub)
	var axle := MeshInstance3D.new()
	axle.mesh = Meshes.cylinder(0.2, 0.2, 0.2, 16)
	axle.rotation_degrees.x = 90
	axle.position.z = 0.52
	axle.material_override = Mats.pbr(Color(0.85, 0.6, 0.2), 0.3, 0.8)
	pivot.add_child(axle)
	var flames: Array = []
	var sphere := Meshes.ellipsoid(Vector3(0.42, 0.42, 0.42), 12, 8)
	var core := Meshes.ellipsoid(Vector3(0.24, 0.24, 0.24), 10, 6)
	for i in range(1, b.len):
		var f := MeshInstance3D.new()
		f.mesh = sphere
		f.material_override = flame_mat
		var c := MeshInstance3D.new()
		c.mesh = core
		c.material_override = flame_core
		f.add_child(c)
		pivot.add_child(f)
		flames.append(f)
	var light := OmniLight3D.new()   # the bar lights its surroundings
	light.light_color = Color(1.0, 0.55, 0.2)
	light.light_energy = 0.8
	light.omni_range = 2.0 + b.len * 0.6
	light.position.z = 0.8
	pivot.add_child(light)
	bars.append([b, pivot, flames])


static func plant(spit: bool, tint := Color(0, 0, 0, 0)) -> Node3D:   # a tube dweller: a stalk and a biting head, mouth up
	var n := Node3D.new()
	var c := (SPITTER if spit else SNAPPER) if tint.a == 0.0 else tint
	var stalk := Actors._part(n, Meshes.cylinder(0.12, 0.14, 1.2, 10), Color("3a9a40"), Vector3(0, 0.6, 0))
	stalk.name = "stalk"
	for sx in [-1, 1]:
		Actors._part(stalk, Meshes.ellipsoid(Vector3(0.28, 0.06, 0.14)), Color("4cb850"), Vector3(sx * 0.24, -0.1, 0), Vector3(0, 0, sx * -25), false)
	var head := Node3D.new()
	head.name = "head"
	head.position.y = 1.45
	n.add_child(head)
	var top := Node3D.new()
	top.name = "jaw"
	head.add_child(top)
	Actors._part(top, Meshes.ellipsoid(Vector3(0.42, 0.3, 0.4)), c, Vector3(0, 0.12, 0))
	for i in 6:   # spots
		var a := TAU * i / 6.0
		Actors._part(top, Meshes.ellipsoid(Vector3(0.07, 0.07, 0.04)), Color.WHITE, Vector3(cos(a) * 0.26, 0.28, sin(a) * 0.26 + 0.05), Vector3(-60, 0, 0), false)
	for i in 5:
		Actors._part(top, Meshes.cylinder(0.0, 0.05, 0.12, 6), Color.WHITE, Vector3(-0.24 + i * 0.12, -0.06, 0.3), Vector3(180, 0, 0), false)
	Actors._part(head, Meshes.ellipsoid(Vector3(0.4, 0.18, 0.38)), c.darkened(0.15), Vector3(0, -0.12, 0))
	Actors._part(head, Meshes.ellipsoid(Vector3(0.32, 0.08, 0.3)), Color("8a1830"), Vector3(0, -0.02, 0.02), Vector3.ZERO, false)
	for i in 5:
		Actors._part(head, Meshes.cylinder(0.0, 0.05, 0.12, 6), Color.WHITE, Vector3(-0.24 + i * 0.12, 0.02, 0.3), Vector3.ZERO, false)
	if spit:   # a spitter's lips
		Actors._part(head, Meshes.torus(0.08, 0.14, 12, 6), c.lightened(0.3), Vector3(0, 0.0, 0.36), Vector3(90, 0, 0), false)
	return n


func _dweller(i: int) -> void:
	var h: Dictionary = sim.L.homes[i]
	var sp: Array = sim.dwellerspot(h)
	var root := Node3D.new()
	root.position = px(sp[0], sp[1], 0)
	if not sp[2]:
		root.rotation_degrees.z = 180
	add_child(root)
	var n := plant(h.spit != 0)
	n.scale = Vector3.ONE * 0.85
	root.add_child(n)
	dws.append([i, root, n])


static func _seed() -> Node3D:
	var n := Node3D.new()
	Actors._part(n, Meshes.ellipsoid(Vector3(0.22, 0.28, 0.2)), Color("9a6a3a"))
	Actors._part(n, Meshes.ellipsoid(Vector3(0.08, 0.1, 0.05)), Color("e8c890"), Vector3(-0.06, 0.08, 0.16), Vector3.ZERO, false)
	n.visible = false
	return n


func _plank(len_: float) -> Node3D:
	var n := Node3D.new()
	var mi := MeshInstance3D.new()
	mi.mesh = Meshes.rounded_box(Vector3(len_, 0.4, 0.9), 0.06, 2)
	mi.position.y = -0.2
	mi.material_override = Mats.toon(WOOD, true, 0.8, 0.2, 0.015)
	n.add_child(mi)
	for k in int(len_):
		var seam := MeshInstance3D.new()
		seam.mesh = Meshes.rounded_box(Vector3(0.04, 0.41, 0.91), 0.01, 1)
		seam.position = Vector3(-len_ * 0.5 + k + 0.5 + 0.5, -0.2, 0)
		seam.material_override = Mats.toon(Color("9a5a28"), false)
		if k + 1 < int(len_): n.add_child(seam)
	return n


func _seesaw(p) -> Node3D:   # the plank tilts about its pivot's top
	var n := Node3D.new()
	n.position = px(p.x >> 8, p.y >> 8, 0)
	var stand := MeshInstance3D.new()   # a triangular stand under the pivot
	stand.mesh = Meshes.cylinder(0.0, 0.5, 0.78, 3)
	stand.position = Vector3(0, -0.2 - 0.39, 0)
	stand.material_override = Mats.toon(Color("8a94a0"), true, 0.4, 0.3, 0.015)
	n.add_child(stand)
	var bolt := MeshInstance3D.new()
	bolt.mesh = Meshes.cylinder(0.08, 0.08, 1.0, 12)
	bolt.rotation_degrees.x = 90
	bolt.position.y = -0.18
	bolt.material_override = Mats.pbr(Color(0.6, 0.62, 0.68), 0.3, 0.8)
	n.add_child(bolt)
	var board := _plank(p.w * 2 / 8.0)
	board.name = "board"
	n.add_child(board)
	return n


func _lift(pl: Array, i: int) -> Node3D:   # a scale lift's plank, with ropes up to the pulley bar
	var p = pl[i]
	var n := Node3D.new()
	var board := _plank(p.w / 8.0)
	board.name = "board"
	n.add_child(board)
	var rope_mat := Mats.toon(Color("c8b490"), false)
	for k in 2:
		var r := MeshInstance3D.new()
		r.mesh = Meshes.cylinder(0.035, 0.035, 1.0, 6)
		r.material_override = rope_mat
		r.name = "rope%d" % k
		n.add_child(r)
	if p.link > i:
		var q = pl[p.link]
		var bar := Node3D.new()
		bar.name = "pulley"
		n.add_child(bar)
		var x0: float = ((p.x >> 8) + p.w * 0.5) / 8.0
		var x1: float = ((q.x >> 8) + q.w * 0.5) / 8.0
		var rod := MeshInstance3D.new()
		rod.mesh = Meshes.cylinder(0.07, 0.07, x1 - x0, 10)
		rod.rotation_degrees.z = 90
		rod.position.x = (x1 - x0) * 0.5
		rod.material_override = Mats.pbr(Color(0.54, 0.58, 0.63), 0.35, 0.7)
		bar.add_child(rod)
		for k in 2:
			var w := MeshInstance3D.new()
			w.mesh = Meshes.cylinder(0.3, 0.3, 0.16, 18)
			w.rotation_degrees.x = 90
			w.position = Vector3((x1 - x0) * k, 0, 0.1)
			w.material_override = Mats.toon(Color("586070"), true, 0.4, 0.3, 0.015)
			w.name = "wheel%d" % k
			bar.add_child(w)
			var hub := MeshInstance3D.new()
			hub.mesh = Meshes.cylinder(0.08, 0.08, 0.2, 10)
			hub.rotation_degrees.x = 90
			hub.position = Vector3((x1 - x0) * k, 0, 0.12)
			hub.material_override = Mats.pbr(Color(0.9, 0.92, 0.95), 0.3, 0.8)
			bar.add_child(hub)
	return n


static func flip_flower() -> Node3D:   # O: a spinning flower with a little top hat for a middle
	var n := Node3D.new()
	var spin := Node3D.new()
	spin.name = "spin"
	spin.position.y = 0.62
	n.add_child(spin)
	Actors._part(n, Meshes.cylinder(0.05, 0.06, 0.55, 8), Color("2e9a40"), Vector3(0, 0.27, 0), Vector3.ZERO, false)
	Actors._part(n, Meshes.ellipsoid(Vector3(0.18, 0.05, 0.1)), Color("3cb850"), Vector3(0.12, 0.2, 0), Vector3(0, 0, 30), false)
	for i in 5:
		var a := TAU * i / 5.0
		Actors._part(spin, Meshes.ellipsoid(Vector3(0.14, 0.14, 0.06)), Color("ff80e0") if i & 1 == 0 else Color("ffd84a"), Vector3(cos(a) * 0.26, sin(a) * 0.26, 0))
	var hat := Node3D.new()
	hat.position.z = 0.08
	n.get_node("spin").add_child(hat)
	Actors._part(hat, Meshes.cylinder(0.17, 0.17, 0.04, 16), Color("733431"), Vector3(0, -0.06, 0), Vector3(0, 0, 0), true)
	Actors._part(hat, Meshes.cylinder(0.1, 0.11, 0.2, 14), Color("733431"), Vector3(0, 0.05, 0))
	Actors._part(hat, Meshes.cylinder(0.112, 0.112, 0.04, 14), Color("ffd84a"), Vector3(0, -0.01, 0), Vector3.ZERO, false)
	return n


static func gold_block() -> Node3D:   # A: a gold block; bump it and it rides on Hatrick's head
	var n := Node3D.new()
	var mi := MeshInstance3D.new()
	mi.mesh = Meshes.rounded_box(Vector3(0.96, 0.96, 0.96), 0.1, 2)
	mi.position.y = 0.5
	mi.material_override = Mats.pbr(Color(1.0, 0.76, 0.2), 0.22, 0.95, Color(0.25, 0.16, 0.0))
	n.add_child(mi)
	for sx in [-1, 1]:   # two little slots
		var slot := MeshInstance3D.new()
		slot.mesh = Meshes.rounded_box(Vector3(0.1, 0.42, 0.04), 0.02, 1)
		slot.position = Vector3(sx * 0.19, 0.5, 0.48)
		slot.material_override = Mats.pbr(Color(0.6, 0.4, 0.08), 0.4, 0.9)
		n.add_child(slot)
	return n


func _door() -> Node3D:   # D: a door with a turning arrow on it; going in turns the room
	var n := Node3D.new()
	var frame := MeshInstance3D.new()
	frame.mesh = Meshes.rounded_box(Vector3(1.06, 1.85, 0.3), 0.06, 2)
	frame.position = Vector3(0, 0.92, -0.2)
	frame.material_override = Mats.toon(Color("5a3418"), true, 0.8, 0.2, 0.015)
	n.add_child(frame)
	var leaf := MeshInstance3D.new()
	leaf.mesh = Meshes.rounded_box(Vector3(0.86, 1.7, 0.14), 0.05, 2)
	leaf.position = Vector3(0, 0.86, -0.02)
	leaf.material_override = Mats.toon(Color("a66a34"), true, 0.8, 0.2, 0.012)
	n.add_child(leaf)
	for k in 3:
		var plank := MeshInstance3D.new()
		plank.mesh = Meshes.rounded_box(Vector3(0.03, 1.6, 0.02), 0.01, 1)
		plank.position = Vector3(-0.22 + k * 0.22, 0.86, 0.06)
		plank.material_override = Mats.toon(Color("8a5428"), false)
		n.add_child(plank)
	var knob := Actors._part(n, Meshes.ellipsoid(Vector3(0.07, 0.07, 0.07)), Color("ffd84a"), Vector3(0.3, 0.75, 0.1), Vector3.ZERO, false)
	knob.name = "knob"
	var arrow := Node3D.new()
	arrow.name = "arrow"
	arrow.position = Vector3(0, 1.25, 0.08)
	n.add_child(arrow)
	for i in 6:
		var a := TAU * i / 7.0
		Actors._part(arrow, Meshes.ellipsoid(Vector3(0.05, 0.05, 0.03)), Color("ffd84a"), Vector3(cos(a) * 0.22, sin(a) * 0.22, 0), Vector3.ZERO, false)
	Actors._part(arrow, Meshes.cylinder(0.0, 0.09, 0.14, 3), Color("ffd84a"), Vector3(0.22 * cos(TAU * 6 / 7.0), 0.22 * sin(TAU * 6 / 7.0), 0), Vector3(0, 0, -20), false)
	return n


func _cannon() -> Node3D:   # E: a cannon block, its muzzle toward Hatrick
	var n := Node3D.new()
	var mi := MeshInstance3D.new()
	mi.mesh = Meshes.rounded_box(Vector3(0.98, 0.98, 0.98), 0.08, 2)
	mi.position.y = 0.5
	mi.material_override = Mats.toon(Color("2a2a36"), true, 0.5, 0.3, 0.015)
	n.add_child(mi)
	for sx in [-1, 1]:
		for sy in [-1, 1]:
			Actors._part(n, Meshes.ellipsoid(Vector3(0.06, 0.06, 0.04)), Color("a0a0b0"), Vector3(sx * 0.34, 0.5 + sy * 0.34, 0.49), Vector3.ZERO, false)
	var barrel := Node3D.new()
	barrel.name = "barrel"
	barrel.position = Vector3(0, 0.5, 0)
	n.add_child(barrel)
	var tube := MeshInstance3D.new()
	tube.mesh = Meshes.cylinder(0.26, 0.3, 0.5, 18)
	tube.rotation_degrees.z = -90
	tube.position.x = 0.42
	tube.material_override = Mats.pbr(Color(0.12, 0.12, 0.16), 0.35, 0.7)
	barrel.add_child(tube)
	var rim := MeshInstance3D.new()
	rim.mesh = Meshes.torus(0.2, 0.32, 18, 6)
	rim.rotation_degrees.z = 90
	rim.position.x = 0.66
	rim.material_override = Mats.pbr(Color(0.48, 0.48, 0.55), 0.3, 0.8)
	barrel.add_child(rim)
	var hole := MeshInstance3D.new()
	hole.mesh = Meshes.cylinder(0.19, 0.19, 0.02, 16)
	hole.rotation_degrees.z = 90
	hole.position.x = 0.68
	hole.material_override = Mats.unshaded(Color(0.03, 0.03, 0.05))
	barrel.add_child(hole)
	return n


func _fan() -> Node3D:   # W: a propeller fan's updraft, up to ten tiles
	var n := Node3D.new()
	var base := MeshInstance3D.new()
	base.mesh = Meshes.rounded_box(Vector3(0.98, 0.62, 0.98), 0.08, 2)
	base.position.y = 0.31
	base.material_override = Mats.toon(Color("7c8796"), true, 0.4, 0.3, 0.015)
	n.add_child(base)
	var grill := MeshInstance3D.new()
	grill.mesh = Meshes.cylinder(0.4, 0.4, 0.04, 20)
	grill.position.y = 0.63
	grill.material_override = Mats.toon(Color("404850"), false)
	n.add_child(grill)
	var blades := Node3D.new()
	blades.name = "blades"
	blades.position.y = 0.72
	n.add_child(blades)
	for i in 3:
		var b := Actors._part(blades, Meshes.ellipsoid(Vector3(0.36, 0.03, 0.11)), Color("e8ecf0"), Vector3.ZERO, Vector3(12, i * 120.0, 0), false)
		b.position = Vector3(cos(TAU * i / 3.0) * 0.18, 0, -sin(TAU * i / 3.0) * 0.18)
	Actors._part(blades, Meshes.ellipsoid(Vector3(0.08, 0.06, 0.08)), Color("586070"), Vector3.ZERO, Vector3.ZERO, false)
	var wind := Node3D.new()   # streaks drifting up the draft
	wind.name = "wind"
	n.add_child(wind)
	var m := Mats.unshaded(Color(1, 1, 1, 0.35))
	for i in 7:
		var s := MeshInstance3D.new()
		s.mesh = Meshes.capsule(0.025, 0.5)
		s.material_override = m
		s.position = Vector3(randf_range(-0.38, 0.38), 1.0 + i * 1.2, randf_range(-0.2, 0.3))
		wind.add_child(s)
	return n


static func _ball() -> Node3D:
	var n := Node3D.new()
	var mi := MeshInstance3D.new()
	mi.mesh = Meshes.ellipsoid(Vector3(0.46, 0.46, 0.46), 18, 12)
	mi.material_override = Mats.toon(Color("2a2a36"), true, 0.3, 0.6, 0.02)
	n.add_child(mi)
	Actors._part(n, Meshes.ellipsoid(Vector3(0.12, 0.09, 0.05)), Color("c8c8d8"), Vector3(-0.16, 0.18, 0.38), Vector3(0, 0, 30), false)
	n.visible = false
	return n


func _rise(lava: bool) -> Node3D:   # lava (or water) up to its wavy top, filling the level's width
	var n := Node3D.new()
	var w: float = sim.lw + 40.0
	var body := MeshInstance3D.new()
	body.mesh = Meshes.rounded_box(Vector3(w, 60.0, 6.0), 0.2, 1)
	body.position = Vector3(sim.lw * 0.5, -30.0, 0.5)
	n.add_child(body)
	var surf := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(w, 6.0)
	pm.subdivide_width = int(w * 2)
	surf.mesh = pm
	surf.position = Vector3(sim.lw * 0.5, 0.0, 0.5)
	n.add_child(surf)
	var crust := MeshInstance3D.new()
	crust.mesh = Meshes.rounded_box(Vector3(w, 0.25, 6.0), 0.1, 1)
	crust.position = Vector3(sim.lw * 0.5, -0.12, 0.5)
	n.add_child(crust)
	if lava:
		var m := ShaderMaterial.new()
		m.shader = LAVA
		body.material_override = m
		body.set_instance_shader_parameter("top_local", 30.0)
		surf.visible = false
		crust.visible = false
		var light := OmniLight3D.new()
		light.name = "glow"
		light.light_color = Color(1.0, 0.45, 0.15)
		light.light_energy = 2.0
		light.omni_range = 14.0
		light.position = Vector3(0, 2.0, 3.0)
		n.add_child(light)
	else:
		var m := StandardMaterial3D.new()
		m.albedo_color = Color(0.11, 0.31, 0.69, 0.62)
		m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		m.roughness = 0.15
		body.material_override = m
		surf.material_override = Mats.unshaded(Color(0.82, 0.94, 1.0, 0.7))
		crust.material_override = Mats.unshaded(Color(0.38, 0.66, 0.91, 0.6))
	return n


func _avalanche() -> Node3D:   # a rolling wall of snow, its front at avx
	var n := Node3D.new()
	var snow := Mats.toon(Color("f4f8ff"), true, 0.9, 0.5, 0.02)
	var shade := Mats.toon(Color("d0dcf0"), false, 0.9, 0.3)
	var rng := RandomNumberGenerator.new()
	rng.seed = 7
	var roll := Node3D.new()
	roll.name = "roll"
	n.add_child(roll)
	for i in 34:   # boulders of snow tumbling at the front, piled higher behind
		var r := rng.randf_range(0.8, 2.0)
		var mi := MeshInstance3D.new()
		mi.mesh = Meshes.ellipsoid(Vector3(r, r * 0.9, r), 14, 10)
		mi.material_override = snow if i % 3 else shade
		var back := rng.randf_range(0.0, 12.0)
		mi.position = Vector3(-back - r * 0.4, rng.randf_range(0.0, 3.0 + back * 0.6), rng.randf_range(-1.5, 1.2))
		roll.add_child(mi)
	var wall := MeshInstance3D.new()
	wall.mesh = Meshes.rounded_box(Vector3(60.0, 80.0, 5.0), 0.5, 1)
	wall.position = Vector3(-38.0, 10.0, -0.5)
	wall.material_override = snow
	n.add_child(wall)
	return n


static func postcard() -> Node3D:   # P: a postcard from the level, for the album at home
	var n := Node3D.new()
	var spin := Node3D.new()
	spin.name = "spin"
	n.add_child(spin)
	var paper := MeshInstance3D.new()
	paper.mesh = Meshes.rounded_box(Vector3(0.95, 0.66, 0.05), 0.04, 1)
	paper.material_override = Mats.toon(Color("fff4dc"), true, 0.7, 0.4, 0.015)
	spin.add_child(paper)
	var pic := MeshInstance3D.new()   # a little landscape on the front
	pic.mesh = Meshes.rounded_box(Vector3(0.48, 0.46, 0.01), 0.02, 1)
	pic.position = Vector3(-0.17, 0.0, 0.03)
	pic.material_override = Mats.toon(Color("6cc0f0"), false)
	spin.add_child(pic)
	Actors._part(spin, Meshes.ellipsoid(Vector3(0.24, 0.1, 0.01)), Color("4cb050"), Vector3(-0.17, -0.14, 0.04), Vector3.ZERO, false)
	Actors._part(spin, Meshes.ellipsoid(Vector3(0.06, 0.06, 0.01)), Color("ffd84a"), Vector3(-0.06, 0.12, 0.04), Vector3.ZERO, false)
	var stamp := MeshInstance3D.new()
	stamp.mesh = Meshes.rounded_box(Vector3(0.16, 0.2, 0.01), 0.01, 1)
	stamp.position = Vector3(0.32, 0.17, 0.03)
	stamp.material_override = Mats.toon(Color("e0586a"), false)
	spin.add_child(stamp)
	for k in 3:
		var line := MeshInstance3D.new()
		line.mesh = Meshes.rounded_box(Vector3(0.3, 0.025, 0.01), 0.005, 1)
		line.position = Vector3(0.24, -0.04 - k * 0.1, 0.03)
		line.material_override = Mats.toon(Color("8a7a6a"), false)
		spin.add_child(line)
	var glow := OmniLight3D.new()
	glow.light_color = Color(1.0, 0.9, 0.6)
	glow.light_energy = 0.6
	glow.omni_range = 2.5
	glow.position.z = 0.6
	n.add_child(glow)
	return n


func _secret_exit(cl: Dictionary) -> void:   # G: a second, purple flag that ends the level another way
	secret = Node3D.new()
	add_child(secret)
	var x: float = cl.gx + 0.5
	var bottom: float = lh - cl.gy - 1
	var gy: int = cl.gy
	while gy + 1 < lh and not ((Sim.SOLID >> sim.rmget(room, cl.gx, gy + 1)) & 1):
		gy += 1
	bottom = lh - gy - 1
	var top := bottom + 6.0
	var pole := MeshInstance3D.new()
	pole.mesh = Meshes.cylinder(0.085, 0.085, top - bottom, 12)
	pole.position = Vector3(x, (top + bottom) * 0.5, -0.2)
	pole.material_override = Mats.pbr(Color(0.82, 0.84, 0.9), 0.25, 0.85)
	secret.add_child(pole)
	var star := MeshInstance3D.new()
	star.mesh = Meshes.extrude(Meshes.star_poly(0.32, 0.14, 5), 0.1, "secret_star")
	star.position = Vector3(x, top + 0.3, -0.2)
	star.material_override = Mats.pbr(Color(0.75, 0.5, 1.0), 0.3, 0.6, Color(0.4, 0.15, 0.6))
	star.name = "star"
	secret.add_child(star)
	var base := MeshInstance3D.new()
	base.mesh = Meshes.rounded_box(Vector3(0.8, 0.5, 0.8), 0.08, 2)
	base.position = Vector3(x, bottom + 0.25, -0.2)
	base.material_override = Mats.toon(Color("6a5a8a"), true, 0.8, 0.2, 0.015)
	secret.add_child(base)
	secret_pennant = Node3D.new()
	secret_pennant.position = Vector3(x, top, -0.2)
	secret.add_child(secret_pennant)
	var cloth := MeshInstance3D.new()
	cloth.mesh = Meshes.extrude(PackedVector2Array([Vector2(0, 0), Vector2(-1.4, -0.5), Vector2(0, -1.0)]), 0.04, "secret_pennant")
	cloth.material_override = Mats.toon(Color("9a4ae0"), true, 0.6, 0.3, 0.015)
	secret_pennant.add_child(cloth)
	secret.set_meta("bottom", bottom)
	secret.set_meta("top", top)


# ---------- every frame ----------
func update(alpha: float, dt: float) -> void:
	if sim == null:
		return
	t += dt
	var fr: float = sim.fr - 1 + alpha
	for b in bars:
		var bar: Dictionary = b[0]
		var a: float
		if absi(bar.speed) != 1:
			a = (bar.a0 * 256.0 + bar.speed * fr) / 256.0
		else:
			a = sim.barangle(bar)
		var ang := a / 256.0 * TAU
		var flames: Array = b[2]
		for i in flames.size():
			var f: MeshInstance3D = flames[i]
			var r := (i + 1) * 1.0
			f.position = Vector3(cos(ang) * r, -sin(ang) * r, 0.25)   # y down in the sim
			var flick := 1.0 + 0.12 * sin(t * 30.0 + i * 1.7)
			f.scale = Vector3.ONE * flick
	var hx: float = sim.hx / 256.0 + 3.0
	for d in dws:
		var dw = sim.wd.dw[d[0]]
		var root: Node3D = d[1]
		var n: Node3D = d[2]
		root.visible = dw.a != 0 and dw.ofs > 0
		if not root.visible:
			continue
		n.position.y = (dw.ofs - 16.0) / 8.0 - 0.15
		var jaw: Node3D = n.get_node("head/jaw")
		var bite := 0.0
		if dw.phase == 2:
			bite = absf(sin(t * 9.0)) * 30.0
			if sim.L.homes[d[0]].spit and (dw.t > 44 and dw.t < 52 or dw.t > 16 and dw.t < 24):
				bite = 40.0
		jaw.rotation_degrees.x = -bite
		n.get_node("head").rotation_degrees.y = clampf((hx - root.position.x * 8.0) * 1.5, -50.0, 50.0) * (1.0 if root.rotation_degrees.z == 0 else -1.0)
		n.get_node("stalk").rotation_degrees.z = sin(t * 3.0 + d[0]) * 4.0
	for i in seeds.size():
		var p = sim.wd.sh[i]
		var n: Node3D = seeds[i]
		n.visible = p.a != 0 and p.room == room
		if n.visible:
			n.position = px((p.x - p.vx * (1.0 - alpha)) / 256.0, (p.y - p.vy * (1.0 - alpha)) / 256.0, 0.2)
			n.rotation.z += dt * 8.0
	var gw = sim.wd.gw
	for e in plats:
		var p = gw.pl[e[0]]
		var n: Node3D = e[1]
		var board: Node3D = n.get_node("board")
		if p.kind == 2:
			board.rotation.z = -atan(p.tilt / 256.0)
		else:
			var x: float = p.x >> 8
			var y: float = p.y / 256.0
			n.position = Vector3.ZERO
			board.position = px(x + p.w * 0.5, y, 0)
			var top: float = lh - p.py / 256.0 / 8.0
			for k in 2:
				var r: MeshInstance3D = n.get_node("rope%d" % k)
				var rx: float = (x + 2.5 if k == 0 else x + p.w - 2.5) / 8.0
				var y0: float = lh - y / 8.0
				r.position = Vector3(rx, (y0 + top) * 0.5, -0.1)
				r.scale = Vector3(1, maxf(top - y0, 0.01), 1)
			if n.has_node("pulley"):
				var bar: Node3D = n.get_node("pulley")
				bar.position = Vector3((x + p.w * 0.5) / 8.0, top, -0.15)

				var turn: float = p.y / 256.0 / 8.0 / 0.3
				bar.get_node("wheel0").rotation.y = turn
				bar.get_node("wheel1").rotation.y = -turn
	for e in obs:
		var o = gw.ob[e[0]]
		var n: Node3D = e[1]
		match o.kind:
			79:
				n.visible = o.t == 0
				n.get_node("spin").rotation.z = t * 4.4
				n.get_node("spin").position.y = 0.62 + sin(t * 6.0) * 0.06
			65:
				n.visible = o.t == 0
			68:
				n.get_node("arrow").rotation.z = -t * 3.0
			69:
				var right: bool = hx > o.x * 8 + 4
				var bl: Node3D = n.get_node("barrel")
				bl.rotation_degrees.y = lerpf(bl.rotation_degrees.y, 0.0 if right else 180.0, 1.0 - exp(-8.0 * dt))
				bl.position.x = -0.12 if o.t < 6 else lerpf(bl.position.x, 0.0, 1.0 - exp(-10.0 * dt))
			87:
				n.get_node("blades").rotation.y += dt * 25.0
				var wind: Node3D = n.get_node("wind")
				var top: int = o.y
				while top > 0 and o.y - top < 10 and not ((Sim.SOLID >> sim.tile(o.x, top - 1)) & 1): top -= 1
				var hgt: float = o.y - top
				for s in wind.get_children():
					s.position.y += dt * 9.0
					if s.position.y > hgt + 0.5: s.position.y = 1.0
					s.visible = hgt > 0.5
	for i in balls.size():
		var b = gw.ball[i]
		var n: Node3D = balls[i]
		n.visible = b.a != 0 and b.room == room
		if n.visible:
			n.position = px((b.x - b.vx * (1.0 - alpha)) / 256.0 + 4.0, (b.y >> 8) + 4.0, 0.15)
			n.rotation.z -= dt * 6.0 * signf(b.vx)
	if rise:
		var top: float = lh - gw.rise / 256.0 / 8.0
		rise.position.y = lerpf(rise.position.y, top, 1.0 - exp(-20.0 * dt)) if absf(rise.position.y - top) < 4.0 else top
		var surf: MeshInstance3D = rise.get_child(1)
		surf.position.y = sin(t * 2.0) * 0.12
		if rise.has_node("glow"):
			rise.get_node("glow").position.x = hx / 8.0
	if aval:
		var front: float = sim.avx / 256.0 / 8.0
		aval.position = Vector3(front, _floor_at(front), 0.4)
		aval.get_node("roll").position.y = absf(sin(t * 5.0)) * 0.3
		for c in aval.get_node("roll").get_children():
			c.rotation.z -= dt * 3.0
	if card:
		card.visible = not sim.cardgot or card.scale.x > 0.01
		card.get_node("spin").rotation.y = sin(t * 1.6) * 0.6
		card.get_node("spin").position.y = sin(t * 2.4) * 0.1
	if secret:
		secret.get_node("star").rotation.y = t * 2.0
		secret_pennant.get_child(0).rotation.y = sin(t * 3.0) * 0.15
		if sim.st == Sim.WIN and sim.clsecretnow:
			var bottom: float = secret.get_meta("bottom")
			secret_pennant.position.y = lerpf(secret_pennant.position.y, bottom + 1.2, 1.0 - exp(-3.0 * dt))
	gold_hat.visible = gw.gold != 0 and sim.st < Sim.TUBE and (gw.gold > 180 or (sim.fr & 4))
	if gold_hat.visible:
		gold_hat.position = px(sim.hx / 256.0 + 2.0, (sim.hy >> 8) + sim.duck - 8.0, 0.1)


func _floor_at(x: float) -> float:   # the top of the ground under world x (tiles), for the avalanche
	var tx := clampi(int(x), 0, sim.lw - 1)
	for ty in range(sim.lh - 1, -1, -1):
		if not ((Sim.SOLID >> sim.rmget(room, tx, ty)) & 1):
			return float(lh - ty - 1)
	return 0.0
