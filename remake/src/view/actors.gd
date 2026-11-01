# The enemies and the thrown cap, drawn where the sim has them (interpolated between steps).
# Walkers are grumpy purple blobs that waddle and hop on the music's "bah"; buzzers are bees.
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
	for i in s.wd.en.size():
		var e = s.wd.en[i]
		var n: Node3D = null
		if e.r == s.room:
			n = walker() if e.t == 1 else buzzer()
			add_child(n)
		nodes.append(n)
		prev.append(Vector2(e.x, e.y) / 256.0)
		yaw.append(-50.0)
	cap_prev = Vector2(s.cxp, s.cyp) / 256.0


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


static func walker() -> Node3D:
	var n := Node3D.new()
	var body := Node3D.new()
	body.name = "body"
	n.add_child(body)
	_part(body, Meshes.ellipsoid(Vector3(0.46, 0.4, 0.42)), PURPLE, Vector3(0, 0.44, 0))
	_part(body, Meshes.ellipsoid(Vector3(0.3, 0.18, 0.3)), PURPLE.lightened(0.25), Vector3(0, 0.62, 0.12), Vector3(-30, 0, 0), false)
	for sx in [-1, 1]:
		var eye := _part(body, Meshes.ellipsoid(Vector3(0.12, 0.15, 0.06)), Color.WHITE, Vector3(sx * 0.15, 0.5, 0.37), Vector3(-5, sx * 18, 0))
		_part(eye, Meshes.ellipsoid(Vector3(0.06, 0.08, 0.04)), Color("301040"), Vector3(sx * -0.015, -0.02, 0.04), Vector3.ZERO, false)
		# angry brows sloping in
		_part(body, Meshes.capsule(0.035, 0.22), Color("301040"), Vector3(sx * 0.15, 0.68, 0.36), Vector3(-10, sx * 18, 90 - sx * 25), false)
		var foot := _part(n, Meshes.ellipsoid(Vector3(0.15, 0.1, 0.2)), PURPLE_DARK, Vector3(sx * 0.2, 0.08, 0.05))
		foot.name = "foot" + ("L" if sx < 0 else "R")
	# a frown with two little fangs
	_part(body, Meshes.capsule(0.03, 0.2), Color("301040"), Vector3(0, 0.32, 0.4), Vector3(0, 0, 90), false)
	for sx in [-1, 1]:
		_part(body, Meshes.cylinder(0.0, 0.035, 0.07, 6), Color.WHITE, Vector3(sx * 0.06, 0.29, 0.405), Vector3(180, 0, 0), false)
	return n


static func buzzer() -> Node3D:
	var n := Node3D.new()
	var body := Node3D.new()
	body.name = "body"
	body.position.y = 0.5
	n.add_child(body)
	_part(body, Meshes.ellipsoid(Vector3(0.36, 0.32, 0.32)), BEE)
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


func update(alpha: float, dt: float) -> void:
	t += dt
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
