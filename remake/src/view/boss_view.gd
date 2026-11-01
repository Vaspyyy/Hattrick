# boss.c's fights, drawn: the haberdasher (a tailor in a long coat and a top hat, throwing
# scissors), the big walker on its seesaw over a lava pit, the cloud king and his thunderclouds,
# the snapper matriarch rising out of the arena's tubes, and the two mini-bosses (a chief walker and
# a buzz boss). Their shots, the hat once it flies off (and on Hatrick's head when he wears it), and
# each boss's fall when beaten.
extends Node3D

const Sim := preload("res://src/sim/sim.gd")
const Meshes := preload("res://src/view/meshes.gd")
const Mats := preload("res://src/view/mats.gd")
const Actors := preload("res://src/view/actors.gd")
const Hazards := preload("res://src/view/hazards.gd")
const LAVA := preload("res://shaders/lava.gdshader")

const COAT := Color("5a2a7a")
const COAT2 := Color("7a44a0")
const COAT_RED := Color("a02838")
const COAT_RED2 := Color("d04050")
const SKIN := Color("f4c8a0")
const HAT := Color("241c2c")
const BAND := Color("c8283c")
const BIG := Color("e0602a")
const FLAME := Color("ffc040")
const CHIEF := Color("3a8a48")
const BUZZBOSS := Color("e84a3c")
const MATRIARCH := Color("8a3ab0")

var sim
var lh := 32
var room := 0
var t := 0.0
var bosses: Array = []    # [index, node]
var prev: Array = []      # per boss: Vector2 (px) before the step
var spin: Array = []      # per boss: a knocked-out boss's tumble
var saw: Node3D           # the big walker's board and post
var lava: Node3D
var clouds: Array = []    # the cloud king's three clouds: [node, bolt]
var mat_node: Node3D      # the matriarch and her decoy, each in a holder at the tube's mouth
var mat_root: Node3D
var decoy_node: Node3D
var decoy_root: Node3D
var hat: Node3D           # the haberdasher's hat once it's off his head
var shots: Array = []     # 24 nodes, rebuilt when a shot's kind changes
var shot_kind: Array = []
var coat_mats: Array = []


func px(x: float, y: float, z := 0.0) -> Vector3:
	return Vector3(x / 8.0, lh - y / 8.0, z)


func build(s) -> void:
	sim = s
	lh = s.lh
	room = s.room
	for c in get_children():
		c.queue_free()
	bosses.clear(); prev.clear(); spin.clear(); clouds.clear(); shots.clear(); shot_kind.clear()
	saw = null; lava = null; mat_node = null; mat_root = null; decoy_node = null; decoy_root = null
	for i in s.bz.size():
		var b = s.bz[i]
		prev.append(Vector2(b.x, b.y) / 256.0)
		spin.append(0.0)
		if b.room != room:
			continue
		var n: Node3D = null
		match b.kind:
			Sim.BOSS_HABERDASHER: n = haberdasher()
			Sim.BOSS_BIGWALKER:
				n = Node3D.new()   # turns about the sprite's middle, as the C game's sprx() does
				var w := Actors.walker(BIG, BIG.darkened(0.4), Color("401808"))
				w.name = "walker"
				w.scale = Vector3.ONE * 3.0
				w.position.y = -1.5
				n.add_child(w)
				var fl := _flames()
				fl.scale = Vector3.ONE * 3.0
				fl.position.y = -1.5
				n.add_child(fl)
				_build_saw(b)
			Sim.BOSS_CLOUDKING:
				n = cloud_king()
				for k in 3:
					var c := _thundercloud()
					add_child(c[0])
					clouds.append(c)
			Sim.BOSS_MATRIARCH:
				mat_root = Node3D.new()
				add_child(mat_root)
				mat_node = Hazards.plant(false, MATRIARCH)
				mat_node.scale = Vector3.ONE * 1.7
				_crown(mat_node.get_node("head/jaw"), Vector3(0, 0.42, 0), 0.32)
				mat_root.add_child(mat_node)
				decoy_root = Node3D.new()
				add_child(decoy_root)
				decoy_node = Hazards.plant(false)
				decoy_node.scale = Vector3.ONE * 0.85
				decoy_root.add_child(decoy_node)
				n = Node3D.new()   # her place in the list; she is drawn by the holders above
			_:
				if b.mini == Sim.MINI_WALKER:
					n = Actors.walker(CHIEF, CHIEF.darkened(0.45), Color("10301a"))
					n.scale = Vector3.ONE * 2.0
					_crown(n.get_node("body"), Vector3(0, 0.86, 0), 0.18)
				else:
					n = Actors.buzzer(BUZZBOSS)
					n.scale = Vector3.ONE * 2.0
		add_child(n)
		bosses.append([i, n])
	hat = top_hat(Color(0, 0, 0, 0))
	hat.visible = false
	add_child(hat)
	for i in s.bsh.size():
		shots.append(null)
		shot_kind.append(0)


func snapshot() -> void:
	if sim == null:
		return
	for i in sim.bz.size():
		prev[i] = Vector2(sim.bz[i].x, sim.bz[i].y) / 256.0


func on_event(_e: Array) -> void:
	pass


# ---------- models ----------
static func _crown(parent: Node3D, pos: Vector3, r: float) -> void:
	var c := Node3D.new()
	c.position = pos
	parent.add_child(c)
	var gold := Mats.pbr(Color(1.0, 0.82, 0.25), 0.25, 0.9, Color(0.2, 0.13, 0.0))
	var ring := MeshInstance3D.new()
	ring.mesh = Meshes.cylinder(r, r * 1.05, r * 0.5, 20)
	ring.material_override = gold
	c.add_child(ring)
	for i in 5:
		var a := TAU * i / 5.0
		var spike := MeshInstance3D.new()
		spike.mesh = Meshes.cylinder(0.0, r * 0.28, r * 0.7, 6)
		spike.position = Vector3(cos(a) * r * 0.88, r * 0.55, sin(a) * r * 0.88)
		spike.material_override = gold
		c.add_child(spike)
	Actors._part(c, Meshes.ellipsoid(Vector3(r * 0.2, r * 0.2, r * 0.12)), Color("e04050"), Vector3(0, 0, r * 1.02), Vector3.ZERO, false)


static func top_hat(_tint: Color) -> Node3D:   # the haberdasher's hat, brim at the node's origin
	var n := Node3D.new()
	Actors._part(n, Meshes.cylinder(0.62, 0.62, 0.07, 28), HAT, Vector3(0, 0.035, 0))
	Actors._part(n, Meshes.cylinder(0.36, 0.4, 0.9, 24), HAT, Vector3(0, 0.5, 0))
	Actors._part(n, Meshes.cylinder(0.405, 0.405, 0.2, 24), BAND, Vector3(0, 0.2, 0), Vector3.ZERO, false)
	Actors._part(n, Meshes.ellipsoid(Vector3(0.08, 0.2, 0.03)), Color("6a5a78"), Vector3(-0.2, 0.6, 0.33), Vector3(0, -30, 0), false)
	for i in 3:   # pins stuck in the band: stomping his hat does him no harm
		Actors._part(n, Meshes.cylinder(0.012, 0.012, 0.3, 6), Color("e8eef4"), Vector3(0.18 - i * 0.14, 0.3, 0.38), Vector3(70, 0, 15 - i * 15), false)
		Actors._part(n, Meshes.ellipsoid(Vector3(0.04, 0.04, 0.04)), [Color("ffd84a"), Color("60c0ff"), Color("ff70a0")][i], Vector3(0.18 - i * 0.14 - 0.03, 0.42, 0.48), Vector3.ZERO, false)
	return n


func haberdasher() -> Node3D:
	var n := Node3D.new()
	var body := Node3D.new()
	body.name = "body"
	n.add_child(body)
	for sx in [-1, 1]:   # legs: pinstripe trousers and polished shoes
		var leg := Node3D.new()
		leg.name = "leg" + ("L" if sx < 0 else "R")
		leg.position = Vector3(sx * 0.2, 0.62, 0)
		body.add_child(leg)
		Actors._part(leg, Meshes.capsule(0.12, 0.62), Color("2a2430"), Vector3(0, -0.3, 0))
		Actors._part(leg, Meshes.ellipsoid(Vector3(0.14, 0.09, 0.22)), Color("101014"), Vector3(0, -0.6, 0.08))
	var coat := Node3D.new()
	coat.name = "coat"
	body.add_child(coat)
	var c1 := Mats.toon(COAT, true, 0.6, 0.35, 0.02, true)
	var c2 := Mats.toon(COAT2, true, 0.6, 0.35, 0.02, true)
	coat_mats = [c1, c2]
	var torso := MeshInstance3D.new()   # a long coat flaring to tails
	torso.mesh = Meshes.lathe(PackedVector2Array([Vector2(0.0, 0.52), Vector2(0.5, 0.5), Vector2(0.52, 0.8), Vector2(0.46, 1.3), Vector2(0.5, 1.6), Vector2(0.3, 1.74), Vector2(0.0, 1.76)]), 24, "haber_coat")
	torso.material_override = c1
	coat.add_child(torso)
	for sx in [-1, 1]:   # lapels
		var lap := MeshInstance3D.new()
		lap.mesh = Meshes.extrude(PackedVector2Array([Vector2(0, 0), Vector2(0.18, 0.38), Vector2(0.06, 0.42)]), 0.03, "lapel")
		lap.position = Vector3(sx * 0.05, 1.28, 0.44)
		lap.scale = Vector3(-sx, 1, 1)
		lap.material_override = c2
		coat.add_child(lap)
	for i in 3:
		Actors._part(coat, Meshes.ellipsoid(Vector3(0.05, 0.05, 0.03)), Color("ffd84a"), Vector3(0, 0.82 + i * 0.18, 0.48), Vector3.ZERO, false)
	var tape := MeshInstance3D.new()   # the tape measure round his neck, hanging down both sides
	tape.mesh = Meshes.torus(0.27, 0.33, 20, 4)
	tape.position = Vector3(0, 1.62, 0.04)
	tape.rotation_degrees.x = 70
	tape.material_override = Mats.toon(Color("f0d040"), false)
	coat.add_child(tape)
	for sx in [-1, 1]:
		var strip := Actors._part(coat, Meshes.rounded_box(Vector3(0.08, 0.5, 0.02), 0.01, 1), Color("f0d040"), Vector3(sx * 0.22, 1.3, 0.48), Vector3(0, 0, sx * 6), false)
		for k in 4:
			Actors._part(strip, Meshes.rounded_box(Vector3(0.05, 0.015, 0.01), 0.002, 1), Color("3a3020"), Vector3(-0.01, -0.18 + k * 0.12, 0.012), Vector3.ZERO, false)
	for sx in [-1, 1]:   # arms
		var arm := Node3D.new()
		arm.name = "arm" + ("L" if sx < 0 else "R")
		arm.position = Vector3(sx * 0.5, 1.55, 0)
		coat.add_child(arm)
		var sl := MeshInstance3D.new()
		sl.mesh = Meshes.capsule(0.13, 0.7)
		sl.position = Vector3(0, -0.32, 0)
		sl.material_override = c1
		arm.add_child(sl)
		Actors._part(arm, Meshes.ellipsoid(Vector3(0.1, 0.1, 0.1)), Color.WHITE, Vector3(0, -0.7, 0))
		if sx > 0:   # scissors, held while he winds up a throw
			var sc := scissors()
			sc.name = "scissors"
			sc.position = Vector3(0, -0.8, 0.12)
			sc.scale = Vector3.ONE * 0.8
			arm.add_child(sc)
	var head := Node3D.new()
	head.name = "head"
	head.position.y = 2.05
	body.add_child(head)
	Actors._part(head, Meshes.ellipsoid(Vector3(0.36, 0.38, 0.34)), SKIN)
	Actors._part(head, Meshes.ellipsoid(Vector3(0.1, 0.09, 0.09)), SKIN.darkened(0.08), Vector3(0, -0.02, 0.34))
	for sx in [-1, 1]:
		Actors._part(head, Meshes.ellipsoid(Vector3(0.06, 0.08, 0.04)), Color("1a1420"), Vector3(sx * 0.13, 0.08, 0.3), Vector3(0, sx * 15, 0), false)
		Actors._part(head, Meshes.capsule(0.04, 0.24), Color("40281a"), Vector3(sx * 0.12, -0.12, 0.32), Vector3(0, 0, 90 + sx * 20), false)   # the moustache
		Actors._part(head, Meshes.ellipsoid(Vector3(0.06, 0.1, 0.06)), SKIN.darkened(0.05), Vector3(sx * 0.35, 0.0, 0))
	var mono := MeshInstance3D.new()   # the monocle and its chain
	mono.mesh = Meshes.torus(0.08, 0.11, 18, 6)
	mono.position = Vector3(0.13, 0.08, 0.34)
	mono.rotation_degrees.x = 90
	mono.material_override = Mats.pbr(Color(1.0, 0.82, 0.3), 0.25, 0.9)
	head.add_child(mono)
	var glass := MeshInstance3D.new()
	glass.mesh = Meshes.cylinder(0.085, 0.085, 0.01, 16)
	glass.position = Vector3(0.13, 0.08, 0.345)
	glass.rotation_degrees.x = 90
	glass.material_override = Mats.unshaded(Color(0.85, 0.92, 1.0, 0.35))
	head.add_child(glass)
	var brows := Node3D.new()
	brows.name = "brows"
	head.add_child(brows)
	for sx in [-1, 1]:
		var br := Actors._part(brows, Meshes.capsule(0.03, 0.18), Color("40281a"), Vector3(sx * 0.13, 0.2, 0.31), Vector3(0, 0, 90), false)
		br.name = "b" + ("L" if sx < 0 else "R")
	var h := top_hat(Color(0, 0, 0, 0))
	h.name = "hat"
	h.position.y = 0.3
	h.rotation_degrees.z = -6
	head.add_child(h)
	var sweat := Node3D.new()   # panting: sweat drops fly off
	sweat.name = "sweat"
	head.add_child(sweat)
	for k in 3:
		var d := Actors._part(sweat, Meshes.ellipsoid(Vector3(0.05, 0.08, 0.05)), Color("80c8ff"), Vector3.ZERO, Vector3.ZERO, false)
		d.name = "d%d" % k
	return n


static func scissors() -> Node3D:
	var n := Node3D.new()
	for k in 2:
		var blade := Node3D.new()
		blade.name = "blade%d" % k
		blade.rotation_degrees.z = 20 if k else -20
		n.add_child(blade)
		var b := MeshInstance3D.new()
		b.mesh = Meshes.extrude(PackedVector2Array([Vector2(0, -0.04), Vector2(0.55, 0.0), Vector2(0, 0.05)]), 0.03, "scissor_blade")
		b.material_override = Mats.pbr(Color(0.9, 0.93, 0.96), 0.2, 0.9)
		blade.add_child(b)
		var ring := MeshInstance3D.new()
		ring.mesh = Meshes.torus(0.06, 0.11, 14, 6)
		ring.rotation_degrees.x = 90
		ring.position = Vector3(-0.14, 0, 0)
		ring.material_override = Mats.toon(Color("d02838"), false)
		blade.add_child(ring)
	Actors._part(n, Meshes.cylinder(0.03, 0.03, 0.08, 8), Color("9aa4b0"), Vector3.ZERO, Vector3(90, 0, 0), false)
	return n


func _build_saw(b) -> void:
	saw = Node3D.new()
	add_child(saw)
	var post := MeshInstance3D.new()
	var top: float = lh - (sim.sawy + 3) / 8.0
	var bottom: float = lh - (lh * 8 - 10) / 8.0
	post.mesh = Meshes.rounded_box(Vector3(0.62, maxf(top - bottom, 0.1), 0.62), 0.06, 2)
	post.position = Vector3(sim.sawx / 8.0, (top + bottom) * 0.5, -0.1)
	post.material_override = Mats.toon(Color("6a7480"), true, 0.4, 0.3, 0.015)
	saw.add_child(post)
	var wedge := Actors._part(saw, Meshes.cylinder(0.0, 0.5, 0.5, 3), Color("ffd84a"), Vector3(sim.sawx / 8.0, top + 0.1, -0.1), Vector3.ZERO, true)
	wedge.name = "wedge"
	var board := Node3D.new()
	board.name = "board"
	saw.add_child(board)
	var len_ := Sim.SAWL * 2 / 8.0
	var mi := MeshInstance3D.new()
	mi.mesh = Meshes.rounded_box(Vector3(len_ + 0.12, 0.5, 1.4), 0.08, 2)
	mi.position.y = -0.25
	mi.material_override = Mats.toon(Color("b87838"), true, 0.8, 0.2, 0.015)
	board.add_child(mi)
	for k in int(len_ / 2.0):
		var seam := MeshInstance3D.new()
		seam.mesh = Meshes.rounded_box(Vector3(0.06, 0.52, 1.42), 0.02, 1)
		seam.position = Vector3(-len_ * 0.5 + 2.0 * (k + 1), -0.25, 0)
		seam.material_override = Mats.toon(Color("6a3a18"), false)
		board.add_child(seam)
	# the lava under the arena, wherever the floor is open
	lava = Node3D.new()
	add_child(lava)
	var hot := ShaderMaterial.new()
	hot.shader = LAVA
	var crust := Mats.pbr(Color(1.0, 0.85, 0.3), 0.6, 0.0, Color(1.0, 0.75, 0.2))
	var x: int = b.ax0
	while x <= b.ax1:
		if sim.rmget(room, x, lh - 1) != 0:
			x += 1
			continue
		var x1: int = x
		while x1 + 1 <= b.ax1 and sim.rmget(room, x1 + 1, lh - 1) == 0: x1 += 1
		var w := float(x1 - x + 1)
		var pool := MeshInstance3D.new()
		pool.mesh = Meshes.rounded_box(Vector3(w, 3.25, 4.0), 0.05, 1)   # deep enough to reach the ground's front
		pool.position = Vector3(x + w * 0.5, 1.15 - 1.625, -0.6)
		pool.material_override = hot
		pool.set_instance_shader_parameter("top_local", 1.625)
		lava.add_child(pool)
		for k in int(w * 1.5):   # bubbles
			var bub := MeshInstance3D.new()
			bub.mesh = Meshes.ellipsoid(Vector3(0.12, 0.12, 0.12), 8, 6)
			bub.position = Vector3(x + randf() * w, 1.1, randf_range(-1.8, 1.0))
			bub.material_override = crust
			bub.set_meta("phase", randf() * TAU)
			lava.add_child(bub)
		x = x1 + 1
	var glow := OmniLight3D.new()
	glow.light_color = Color(1.0, 0.45, 0.15)
	glow.light_energy = 1.6
	glow.omni_range = 10.0
	glow.position = Vector3((b.ax0 + b.ax1) * 0.5, 2.5, 2.0)
	lava.add_child(glow)


func _flames() -> Node3D:   # tongues of fire licking up round the big walker on its flaming leap
	var f := Node3D.new()
	f.name = "flames"
	f.visible = false
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.albedo_color = Color(1.0, 0.6, 0.15, 0.85)
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.emission_enabled = true
	mat.emission = Color(1.0, 0.45, 0.05)
	mat.emission_energy_multiplier = 2.0
	for i in 9:
		var m := MeshInstance3D.new()
		m.mesh = Meshes.cylinder(0.0, 0.14, 0.5, 8)
		var a := TAU * i / 9.0
		m.position = Vector3(cos(a) * 0.42, 0.25, sin(a) * 0.42)
		m.material_override = mat
		m.set_meta("phase", a * 3.0)
		f.add_child(m)
	var light := OmniLight3D.new()
	light.light_color = Color(1.0, 0.5, 0.15)
	light.light_energy = 2.0
	light.omni_range = 3.0
	light.position.y = 0.5
	f.add_child(light)
	return f


func cloud_king() -> Node3D:
	var n := Node3D.new()
	var body := Node3D.new()
	body.name = "body"
	n.add_child(body)
	var puff := Node3D.new()
	puff.name = "puff"
	body.add_child(puff)
	var c1 := Color("7a7e98")
	var c2 := Color("9aa0b8")
	for p in [[-0.9, 0.25, 0.9, 0.62], [0.9, 0.25, 0.9, 0.62], [0.0, -0.25, 1.15, 0.9], [-0.75, 0.62, 0.5, 0.3], [0.0, 0.62, 0.5, 0.3], [0.75, 0.62, 0.5, 0.3]]:
		var m := MeshInstance3D.new()
		m.mesh = Meshes.ellipsoid(Vector3(p[2], p[3], 0.8), 18, 12)
		m.position = Vector3(p[0], -p[1], 0)
		m.material_override = Mats.toon(c2 if p[1] < 0 else c1, true, 0.9, 0.5, 0.025)
		puff.add_child(m)
	var face := Node3D.new()
	face.name = "face"
	face.position = Vector3(0, 0.15, 0.78)
	body.add_child(face)
	for sx in [-1, 1]:
		var eye := Actors._part(face, Meshes.ellipsoid(Vector3(0.2, 0.22, 0.06)), Color.WHITE, Vector3(sx * 0.42, 0.08, 0), Vector3.ZERO, false)
		var pupil := Actors._part(eye, Meshes.ellipsoid(Vector3(0.09, 0.1, 0.04)), Color("1a1420"), Vector3(sx * -0.03, -0.04, 0.04), Vector3.ZERO, false)
		pupil.name = "pupil"
		eye.name = "eye" + ("L" if sx < 0 else "R")
		var brow := Actors._part(face, Meshes.capsule(0.05, 0.36), Color("1a1420"), Vector3(sx * 0.42, 0.38, 0.02), Vector3(0, 0, 90 + sx * 25), false)
		brow.name = "brow" + ("L" if sx < 0 else "R")
	var mouth := Actors._part(face, Meshes.ellipsoid(Vector3(0.38, 0.1, 0.05)), Color("2a2a3a"), Vector3(0, -0.4, 0), Vector3.ZERO, false)
	mouth.name = "mouth"
	_crown(body, Vector3(0, 1.05, 0), 0.5)
	return n


func _thundercloud() -> Array:
	var n := Node3D.new()
	var puff := Node3D.new()
	puff.name = "puff"
	n.add_child(puff)
	for p in [[-0.75, 0.12, 0.75, 0.42], [0.75, 0.12, 0.75, 0.42], [0.0, -0.12, 0.9, 0.55]]:
		var m := MeshInstance3D.new()
		m.mesh = Meshes.ellipsoid(Vector3(p[2], p[3], 0.7), 16, 10)
		m.position = Vector3(p[0], -p[1], 0)
		m.material_override = Mats.toon(Color("b0b8cc") if p[1] < 0 else Color("8890a8"), true, 0.9, 0.5, 0.02)
		puff.add_child(m)
	var bolt := Node3D.new()   # the bolt: jagged segments to the floor, rebuilt each strike
	bolt.name = "bolt"
	n.add_child(bolt)
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.albedo_color = Color(1.0, 1.0, 0.85)
	mat.emission_enabled = true
	mat.emission = Color(1.0, 0.95, 0.5)
	mat.emission_energy_multiplier = 3.0
	for k in 12:
		var seg := MeshInstance3D.new()
		seg.mesh = Meshes.cylinder(0.07, 0.07, 1.0, 6)
		seg.material_override = mat
		bolt.add_child(seg)
	var light := OmniLight3D.new()
	light.name = "flash"
	light.light_color = Color(1.0, 0.95, 0.6)
	light.light_energy = 0.0
	light.omni_range = 8.0
	n.add_child(light)
	return [n, bolt]


func _shot(kind: int) -> Node3D:
	match kind:
		Sim.BS_SCISSORS:
			var s := scissors()
			s.scale = Vector3.ONE * 2.0
			return s
		Sim.BS_SEED:
			var n := Node3D.new()
			Actors._part(n, Meshes.ellipsoid(Vector3(0.28, 0.3, 0.26)), Color("7a2a5a"))
			Actors._part(n, Meshes.ellipsoid(Vector3(0.08, 0.08, 0.04)), Color("e080c0"), Vector3(-0.1, 0.12, 0.22), Vector3.ZERO, false)
			return n
		Sim.BS_STING:
			var n := Node3D.new()
			Actors._part(n, Meshes.cylinder(0.0, 0.14, 0.62, 10), Color("ffd23c"), Vector3(0, -0.1, 0), Vector3(180, 0, 0))
			Actors._part(n, Meshes.cylinder(0.14, 0.14, 0.1, 10), Color("2a2a2a"), Vector3(0, 0.22, 0), Vector3.ZERO, false)
			return n
	var z := Node3D.new()   # a zap
	var m := MeshInstance3D.new()
	m.mesh = Meshes.extrude(Meshes.star_poly(0.38, 0.16, 6), 0.1, "zap")
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.albedo_color = Color(1.0, 0.95, 0.5)
	mat.emission_enabled = true
	mat.emission = Color(1.0, 0.9, 0.4)
	mat.emission_energy_multiplier = 3.0
	m.material_override = mat
	z.add_child(m)
	return z


# ---------- every frame ----------
func update(alpha: float, dt: float) -> void:
	if sim == null:
		return
	t += dt
	var X: float = sim.hx / 256.0 + 3.0
	for e in bosses:
		var i: int = e[0]
		var b = sim.bz[i]
		var n: Node3D = e[1]
		var cur := Vector2(b.x, b.y) / 256.0
		var p: Vector2 = prev[i].lerp(cur, alpha)
		if p.distance_to(cur) > 40.0:
			p = cur
		var gone: bool = b.dead == 2 or (b.dead and b.t > 200)
		var blink: bool = b.hurt and (sim.fr & 2)
		n.visible = not gone and not blink
		if b.dead:
			spin[i] += dt
		match b.kind:
			Sim.BOSS_HABERDASHER: _pose_haberdasher(b, n, p, dt)
			Sim.BOSS_BIGWALKER: _pose_bigwalker(b, n, p, dt)
			Sim.BOSS_CLOUDKING: _pose_cloudking(b, n, p, dt)
			Sim.BOSS_MATRIARCH: _pose_matriarch(b, dt)
			_:
				if b.mini == Sim.MINI_WALKER:
					n.position = px(p.x + 8.0, p.y + 16.0, 0)
					var body: Node3D = n.get_node("body")
					var ph := p.x * 0.6
					body.rotation_degrees.z = sin(t * 30.0) * 10.0 if b.s == 1 else sin(ph) * 6.0
					n.get_node("footL").position.y = 0.08 + maxf(0.0, sin(ph)) * 0.1
					n.get_node("footR").position.y = 0.08 + maxf(0.0, -sin(ph)) * 0.1
					n.rotation_degrees.y = lerpf(n.rotation_degrees.y, 50.0 if b.face > 0 else -50.0, 1.0 - exp(-10.0 * dt))
				else:
					n.position = px(p.x + 8.0, p.y + 16.0, 0)
					var body: Node3D = n.get_node("body")
					var f := sin(t * 70.0)
					body.get_node("wingL").rotation_degrees.x = -20.0 + f * 35.0
					body.get_node("wingR").rotation_degrees.x = 20.0 - f * 35.0
					var right: bool = (b.vx > 0) if b.s else X > p.x + 8.0
					n.rotation_degrees.y = lerpf(n.rotation_degrees.y, -20.0 if right else 200.0, 1.0 - exp(-8.0 * dt))
					body.rotation_degrees.z = -25.0 if b.s and b.n < 40 else 0.0
				if b.dead:
					n.rotation_degrees.z = 180.0 * minf(spin[i] * 3.0, 1.0)
	# the hat flying, or on Hatrick's head
	hat.visible = sim.hatst == 1 or (sim.hatst == 2 and (sim.hatt > 90 or (sim.fr & 4)))
	if sim.hatst == 1:
		hat.position = px(sim.hatx, sim.haty + 4.0, 0.3)
		hat.rotation_degrees = Vector3(0, t * 120.0, sin(t * 4.0) * 20.0)
		hat.scale = Vector3.ONE * 0.75
	elif sim.hatst == 2:
		hat.position = px(sim.hx / 256.0 + 3.0, (sim.hy >> 8) + sim.duck + 1.0, 0.0)
		hat.rotation_degrees = Vector3(0, 0, -8)
		hat.scale = Vector3.ONE * 0.62
	for c in clouds.size():
		var cl = sim.bcl[c]
		var node: Node3D = clouds[c][0]
		var king_dead := false
		for e in bosses:
			if sim.bz[e[0]].kind == Sim.BOSS_CLOUDKING and sim.bz[e[0]].dead: king_dead = true
		node.visible = cl.a != 0 and not king_dead
		if not node.visible:
			continue
		var cx: float = (cl.x >> 8) + 12.0
		var cy: float = cl.y >> 8
		node.position = px(cx, cy, 0.0)
		var flash: bool = cl.charge and (sim.fr >> (1 if cl.charge > 30 else 2)) & 1
		var puff: Node3D = node.get_node("puff")
		for m in puff.get_children():
			m.material_override = Mats.toon(Color("fff0a0") if flash else (Color("b0b8cc") if m.position.y > 0 else Color("8890a8")), true, 0.9, 0.5, 0.02)
		puff.scale = Vector3.ONE * (1.0 + (0.06 * sin(t * 30.0) if cl.charge else 0.0))
		var bolt: Node3D = clouds[c][1]
		bolt.visible = cl.strike != 0
		node.get_node("flash").light_energy = 3.0 if cl.strike else (1.0 if flash else 0.0)
		if cl.strike:
			var y0 := cy + 4.0
			var y1: float = sim.floorpx
			var segs := bolt.get_children()
			var k := segs.size()
			var last := Vector3(0, 0, 0.1)
			for j in k:
				var yy: float = y0 + (y1 - y0) * (j + 1) / k
				var jx: float = sin((yy * 37.0 + sim.fr * 50.0) * TAU / 256.0) * 2.0 if j + 1 < k else 0.0
				var pnt := Vector3(jx / 8.0, -(yy - cy) / 8.0, 0.1)
				var seg: MeshInstance3D = segs[j]
				var mid := (last + pnt) * 0.5
				var d := pnt - last
				seg.position = mid
				seg.scale = Vector3(1, d.length(), 1)
				seg.basis = Basis(Vector3.BACK, Vector3.UP.signed_angle_to(d, Vector3.BACK)).scaled(Vector3(1, d.length(), 1))
				last = pnt
	for i in sim.bsh.size():
		var q = sim.bsh[i]
		if not q.a:
			if shots[i]: shots[i].visible = false
			continue
		if shots[i] == null or shot_kind[i] != q.kind:
			if shots[i]: shots[i].queue_free()
			shots[i] = _shot(q.kind)
			shot_kind[i] = q.kind
			add_child(shots[i])
		var s: Node3D = shots[i]
		s.visible = true
		s.position = px((q.x - q.vx * (1.0 - alpha)) / 256.0, (q.y - q.vy * (1.0 - alpha)) / 256.0, 0.3)
		match q.kind:
			Sim.BS_SCISSORS:
				s.rotation.z = -q.t * 24.0 / 256.0 * TAU
				var snip := absf(sin(t * 20.0)) * 25.0
				s.get_child(0).rotation_degrees.z = -snip
				s.get_child(1).rotation_degrees.z = snip
			Sim.BS_SEED: s.rotation.z += dt * 6.0
			Sim.BS_ZAP: s.rotation.z += dt * 12.0


func _pose_haberdasher(b, n: Node3D, p: Vector2, dt: float) -> void:
	var cx := p.x + 5.0
	n.position = px(cx, p.y + 18.0, 0)
	n.scale = Vector3.ONE * 0.95
	var body: Node3D = n.get_node("body")
	var face := 1.0 if b.face > 0 else -1.0
	n.rotation_degrees.y = lerpf(n.rotation_degrees.y, 40.0 * face, 1.0 - exp(-10.0 * dt))
	var red: bool = b.phase == 2
	coat_mats[0].set_shader_parameter("albedo", COAT_RED if red else COAT)
	coat_mats[1].set_shader_parameter("albedo", COAT_RED2 if red else COAT2)
	var walking: bool = b.vx != 0 and b.gnd
	var ph := t * (16.0 if red else 11.0)
	for side in ["L", "R"]:
		var sx := -1.0 if side == "L" else 1.0
		var leg: Node3D = body.get_node("leg" + side)
		leg.rotation_degrees.x = sin(ph) * 32.0 * sx if walking else 0.0
	body.position.y = absf(sin(ph)) * 0.06 if walking else 0.0
	var coat: Node3D = body.get_node("coat")
	var arm_r: Node3D = coat.get_node("armR")
	var arm_l: Node3D = coat.get_node("armL")
	var winding: bool = b.s == 1 and not b.dead
	arm_r.rotation_degrees.x = lerpf(arm_r.rotation_degrees.x, (-150.0 + b.t * 6.0) if winding else (sin(ph) * 25.0 if walking else 0.0), 1.0 - exp(-14.0 * dt))
	arm_l.rotation_degrees.x = -sin(ph) * 25.0 if walking else 0.0
	arm_r.get_node("scissors").visible = winding
	var head: Node3D = body.get_node("head")
	head.get_node("hat").visible = sim.hatst == 0
	var panting: bool = b.s == 3 and not b.dead
	coat.rotation_degrees.x = 18.0 + sin(t * 9.0) * 4.0 if panting else 0.0
	head.position.y = 2.05 - (0.12 if panting else 0.0)
	var sweat: Node3D = head.get_node("sweat")
	sweat.visible = panting
	if panting:
		for k in 3:
			var f := fmod(t * 1.6 + k / 3.0, 1.0)
			sweat.get_node("d%d" % k).position = Vector3(-0.42 - f * 0.4, 0.3 + f * 0.25 - f * f * 0.8, 0.1)
	for side in ["L", "R"]:
		var sx := -1.0 if side == "L" else 1.0
		head.get_node("brows/b" + side).rotation_degrees.z = 90.0 + sx * (-25.0 if red else (12.0 if panting else 0.0))
	if b.dead:
		n.rotation_degrees.z = -face * minf(spin[sim.bz.find(b)] * 300.0, 100.0)


func _pose_bigwalker(b, n: Node3D, p: Vector2, dt: float) -> void:
	var cx := p.x + 12.0
	n.position = px(cx, p.y + 12.0, 0.1)
	var w: Node3D = n.get_node("walker")
	var body: Node3D = w.get_node("body")
	var fire: bool = b.s == 4
	var ang := 0.0
	if b.s <= 2: ang = sim.sawt * 41.0 / 256.0
	elif fire: ang = (b.t * 8) & 255
	n.rotation = Vector3(0, 0, -ang / 256.0 * TAU)
	if not fire:
		w.rotation_degrees.y = lerpf(w.rotation_degrees.y, 50.0 if b.face > 0 else -50.0, 1.0 - exp(-6.0 * dt))
	var ph := p.x * 0.35
	body.rotation_degrees.z = sin(ph) * 5.0 if b.s == 0 else 0.0
	w.get_node("footL").position.y = 0.08 + (maxf(0.0, sin(ph)) * 0.1 if b.s == 0 else 0.0)
	w.get_node("footR").position.y = 0.08 + (maxf(0.0, -sin(ph)) * 0.1 if b.s == 0 else 0.0)
	var mi: MeshInstance3D = body.get_child(0)
	mi.material_override = Mats.toon(FLAME if fire else BIG, true, 0.5, 0.4, 0.02)
	var fl: Node3D = n.get_node("flames")
	fl.visible = fire or b.s == 3
	if fl.visible:
		fl.rotation.z = -n.rotation.z   # upright while he tumbles
		fl.position = Vector3(0, -1.5, 0).rotated(Vector3.BACK, fl.rotation.z) * 0.6
		for c in fl.get_children():
			if c.has_meta("phase"):
				var k: float = 0.7 + 0.5 * absf(sin(t * 14.0 + c.get_meta("phase")))
				c.scale = Vector3(1.0, k * 1.4, 1.0)
				c.position.y = 0.2 + k * 0.35
	if saw:
		var board: Node3D = saw.get_node("board")
		board.position = Vector3(sim.sawx / 8.0, lh - sim.sawy / 8.0, 0.0)
		board.rotation.z = -atan(sim.sawt / 256.0)
	if lava:
		for c in lava.get_children():
			if c.has_meta("phase"):
				var ph2: float = c.get_meta("phase") + t * 2.0
				c.position.y = 1.1 + absf(sin(ph2)) * 0.15
				c.scale = Vector3.ONE * (0.6 + absf(sin(ph2)) * 0.6)


func _pose_cloudking(b, n: Node3D, p: Vector2, dt: float) -> void:
	n.position = px(p.x, p.y, 0.6)
	var body: Node3D = n.get_node("body")
	var angry: bool = b.hp < b.maxhp
	var face: Node3D = body.get_node("face")
	var X: float = sim.hx / 256.0 + 3.0
	var look := clampf((X - p.x) / 60.0, -1.0, 1.0)
	for side in ["L", "R"]:
		var sx := -1.0 if side == "L" else 1.0
		face.get_node("eye" + side).get_node("pupil").position.x = look * 0.08
		face.get_node("brow" + side).rotation_degrees.z = lerpf(face.get_node("brow" + side).rotation_degrees.z, 90.0 + sx * (35.0 if angry else 10.0), 1.0 - exp(-8.0 * dt))
	var puff: Node3D = body.get_node("puff")
	for m in puff.get_children():
		var hi: bool = m.position.y > 0.0
		var c := (Color("7a7a98") if hi else Color("5a5a78")) if angry else (Color("9aa0b8") if hi else Color("7a7e98"))
		m.material_override = Mats.toon(c, true, 0.9, 0.5, 0.025)
	puff.scale = Vector3(1.0 + sin(t * 2.0) * 0.03, 1.0 - sin(t * 2.0) * 0.03, 1.0)
	face.get_node("mouth").scale = Vector3(1.0, 1.0 + (absf(sin(t * 6.0)) * 0.8 if angry else 0.0), 1.0)
	body.rotation_degrees.z = sin(t * 1.3) * 4.0
	if b.dead:
		body.rotation_degrees.z = spin[sim.bz.find(b)] * 200.0


func _pose_matriarch(b, dt: float) -> void:
	var blink: bool = b.hurt and (sim.fr & 2)
	_pose_snapper(mat_root, mat_node, sim.mcur, sim.mofs, 28.0, 1.7, b.s == 2, blink)
	_pose_snapper(decoy_root, decoy_node, sim.dcur, sim.dofs, 16.0, 0.85, sim.dph == 2, false)


func _pose_snapper(root: Node3D, n: Node3D, tube: int, ofs: int, full: float, sc: float, out: bool, blink: bool) -> void:
	root.visible = tube >= 0 and ofs > 0 and not blink
	if not root.visible:
		return
	var sp: Array = sim.mspot(tube)
	root.position = px(sp[0], sp[1], 0)
	root.rotation_degrees.z = 0.0 if sp[2] else 180.0
	n.position.y = (ofs - full) / 8.0 - 0.15 * sc
	var bite := absf(sin(t * 9.0)) * 30.0 if out else 8.0
	n.get_node("head/jaw").rotation_degrees.x = -bite
	n.get_node("stalk").rotation_degrees.z = sin(t * 3.0) * 4.0
