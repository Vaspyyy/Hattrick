# What lies behind the play plane: rolling hills in two layers, lollipop trees, bushes along the
# back of the ground, and slow clouds for sky areas; a rock wall, stalactites and glowing
# crystals for caves. Real depth does the parallax. Seeded per level, so it's the same each time.
extends Node3D

const Meshes := preload("res://src/view/meshes.gd")
const Mats := preload("res://src/view/mats.gd")
const TERRAIN := preload("res://shaders/terrain.gdshader")

var clouds: Array = []
var rng := RandomNumberGenerator.new()
var lw := 0
var lh := 0
var t := 0.0


func build(level_view, seed_name: String) -> void:
	for c in get_children():
		c.queue_free()
	clouds.clear()
	lw = level_view.lw
	lh = level_view.lh
	rng.seed = hash(seed_name)
	if level_view.cave:
		_cave(level_view)
	else:
		_sky(level_view)


func _mat(c: Color, outline := false) -> Material:
	return Mats.toon(c, outline, 0.9, 0.25, 0.03)


# Distant things: plain soft lambert, no highlights, so the fog layers them.
func _soft(c: Color) -> Material:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = 1.0
	m.metallic_specular = 0.0
	return m


func _sky(lv) -> void:
	var x0 := -60.0
	var x1 := lw + 60.0
	# a meadow out to the horizon, below the level's ground
	var meadow := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(x1 - x0 + 200.0, 300.0)
	meadow.mesh = pm
	meadow.position = Vector3((x0 + x1) * 0.5, 0.0, -153.5)
	meadow.material_override = _soft(Color(0.36, 0.62, 0.3))
	add_child(meadow)
	# far mountains: hazy blue-green domes on the horizon
	var x := x0 - 40.0
	while x < x1 + 60.0:
		var r := rng.randf_range(22.0, 38.0)
		var h := rng.randf_range(16.0, 30.0)
		var hill := MeshInstance3D.new()
		hill.mesh = Meshes.ellipsoid(Vector3(r, h, r * 0.6), 28, 14)
		hill.position = Vector3(x, -h * 0.25, -150.0 + rng.randf_range(-10, 10))
		hill.material_override = _soft(Color(0.42, 0.62, 0.66).lerp(Color(0.5, 0.66, 0.78), rng.randf()))
		hill.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		add_child(hill)
		x += r * rng.randf_range(1.0, 1.5)
	# rolling middle hills, sunk into the meadow, with a few trees
	x = x0
	while x < x1 + 30.0:
		var r := rng.randf_range(9.0, 16.0)
		var h := rng.randf_range(7.0, 12.0)
		var z := -62.0 + rng.randf_range(-8, 8)
		var hill := MeshInstance3D.new()
		hill.mesh = Meshes.ellipsoid(Vector3(r, h, r * 0.7), 28, 14)
		var y := -h * rng.randf_range(0.35, 0.55)
		hill.position = Vector3(x, y, z)
		hill.material_override = _soft(Color(0.38, 0.66, 0.32).lerp(Color(0.52, 0.72, 0.36), rng.randf()))
		hill.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		add_child(hill)
		for i in rng.randi_range(0, 4):
			var a := rng.randf_range(-0.8, 0.8)
			_tree(Vector3(x + sin(a) * r * 0.85, y + cos(a) * h * 0.97 - 0.4, z + r * 0.45), rng.randf_range(2.5, 3.8), false, true)
		x += r * rng.randf_range(1.1, 1.7)
	# a loose wood on the meadow between the level and the hills
	x = x0
	while x < x1:
		var z := rng.randf_range(-24.0, -10.0)
		if rng.randf() < 0.75:
			_tree(Vector3(x, 0.0, z), rng.randf_range(3.0, 5.5), false, true)
		else:
			_bush(Vector3(x, 0.0, z), rng.randf_range(1.6, 2.6), false)
		x += rng.randf_range(2.0, 6.0)
	# bushes, trees and flowers along the back of the ground's grass
	for tx in range(-10, lw + 10):
		var ty := _top(lv, tx)
		if ty < 0:
			continue
		var roll := rng.randf()
		var y := float(lh - ty)
		if roll < 0.14:
			_bush(Vector3(tx + rng.randf(), y, rng.randf_range(-2.6, -2.0)), rng.randf_range(0.7, 1.2), true)
		elif roll < 0.2:
			_tree(Vector3(tx + 0.5, y, rng.randf_range(-2.6, -2.2)), rng.randf_range(2.6, 3.6), true, false)
		elif roll < 0.32:
			_flowers(Vector3(tx + rng.randf(), y, rng.randf_range(-1.8, -0.9)))
	# clouds
	x = x0
	while x < x1:
		var c := _cloud(rng.randf_range(1.6, 3.0))
		c.position = Vector3(x, lh + rng.randf_range(-3.0, 8.0), rng.randf_range(-90.0, -50.0))
		add_child(c)
		clouds.append([c, rng.randf_range(0.2, 0.5)])
		x += rng.randf_range(14.0, 30.0)


# The top cell of ground in column tx (an open grass top with nothing standing on it), or -1.
func _top(lv, tx: int) -> int:
	for ty in lh:
		var t: int = lv._t(tx, ty)
		if t == 0 or t == 6 or t == 14:
			continue
		return ty if t == 1 else -1
	return -1


func _tree(p: Vector3, h: float, outline: bool, soft := false) -> void:
	var n := Node3D.new()
	n.position = p
	add_child(n)
	var trunk := MeshInstance3D.new()
	trunk.mesh = Meshes.cylinder(0.12 * h / 2.0, 0.18 * h / 2.0, h * 0.6, 10)
	trunk.position.y = h * 0.3
	trunk.material_override = _soft(Color(0.45, 0.3, 0.2)) if soft else _mat(Color(0.48, 0.3, 0.17), outline)
	n.add_child(trunk)
	var green := Color(0.26, 0.62, 0.25).lerp(Color(0.42, 0.74, 0.28), rng.randf())
	for i in 3:
		var b := MeshInstance3D.new()
		var r := h * rng.randf_range(0.26, 0.34)
		b.mesh = Meshes.ellipsoid(Vector3(r, r * 0.9, r), 16, 10)
		b.position = Vector3(rng.randf_range(-0.2, 0.2) * h, h * (0.68 + 0.12 * i), rng.randf_range(-0.1, 0.1) * h)
		b.material_override = _soft(green.lightened(0.06 * i)) if soft else _mat(green.lightened(0.06 * i), outline)
		if soft:
			b.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		n.add_child(b)


func _bush(p: Vector3, s: float, near := true) -> void:
	var green := Color(0.28, 0.66, 0.27).lerp(Color(0.4, 0.76, 0.3), rng.randf())
	for i in 3:
		var b := MeshInstance3D.new()
		var r := s * (0.5 if i == 1 else 0.38)
		b.mesh = Meshes.ellipsoid(Vector3(r, r * 0.85, r * 0.8), 16, 8)
		b.position = p + Vector3((i - 1) * s * 0.45, r * 0.55, 0)
		b.material_override = _mat(green, true) if near else _soft(green)
		add_child(b)


func _flowers(p: Vector3) -> void:
	var cols := [Color(1, 0.45, 0.55), Color(1, 0.9, 0.35), Color(1, 1, 1), Color(0.65, 0.55, 1)]
	var c: Color = cols[rng.randi() % cols.size()]
	for i in rng.randi_range(1, 3):
		var stem := MeshInstance3D.new()
		stem.mesh = Meshes.cylinder(0.02, 0.02, 0.3, 5)
		var q := p + Vector3(rng.randf_range(-0.3, 0.3), 0.15, rng.randf_range(-0.2, 0.2))
		stem.position = q
		stem.material_override = _mat(Color(0.3, 0.6, 0.25))
		add_child(stem)
		var head := MeshInstance3D.new()
		head.mesh = Meshes.ellipsoid(Vector3(0.09, 0.07, 0.09), 10, 6)
		head.position = q + Vector3(0, 0.17, 0)
		head.material_override = _mat(c)
		add_child(head)


func _cloud(s: float) -> Node3D:
	var n := Node3D.new()
	var m := StandardMaterial3D.new()
	m.albedo_color = Color(1, 1, 1)
	m.roughness = 1.0
	m.metallic_specular = 0.0
	m.emission_enabled = true
	m.emission = Color(0.45, 0.5, 0.6)
	var k := rng.randi_range(3, 5)
	for i in k:
		var b := MeshInstance3D.new()
		var r := s * rng.randf_range(1.2, 2.0) * (1.25 if i == k / 2 else 1.0)
		b.mesh = Meshes.ellipsoid(Vector3(r, r * 0.75, r * 0.8), 16, 10)
		b.position = Vector3((i - k * 0.5) * s * 1.7, rng.randf_range(-0.3, 0.5) * s, 0)
		b.material_override = m
		b.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		n.add_child(b)
	return n


func _cave(lv) -> void:
	var wall := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.orientation = PlaneMesh.FACE_Z
	pm.size = Vector2(lw + 80, lh + 40)
	wall.mesh = pm
	wall.position = Vector3(lw * 0.5, lh * 0.5, -1.6)
	var m := ShaderMaterial.new()
	m.shader = TERRAIN
	m.set_shader_parameter("grass_on", 0.0)
	m.set_shader_parameter("dirt_hi", Color(0.33, 0.27, 0.27))
	m.set_shader_parameter("dirt_lo", Color(0.18, 0.15, 0.16))
	m.set_shader_parameter("pebble", Color(0.4, 0.35, 0.34))
	wall.material_override = m
	add_child(wall)
	# stalactites under the ceiling, stalagmites on the floor
	var cone := Meshes.cylinder(0.0, 0.35, 1.6, 8)
	var rock := _mat(Color(0.42, 0.36, 0.36), true)
	for tx in range(0, lw):
		if rng.randf() > 0.22:
			continue
		for ty in range(0, lh - 1):
			if lv._solid(lv._t(tx, ty)) and lv._t(tx, ty + 1) == 0:
				var s := MeshInstance3D.new()
				s.mesh = cone
				var h := rng.randf_range(0.5, 1.2)
				s.scale = Vector3(h, h, h)
				s.rotation_degrees.x = 180
				s.position = Vector3(tx + rng.randf(), lh - ty - 1 - 0.8 * h, rng.randf_range(-1.3, -0.8))
				s.material_override = rock
				add_child(s)
				break
	# crystals glowing in the dark
	for i in int(lw / 9.0):
		var p := Vector3(rng.randf_range(0, lw), rng.randf_range(2, lh - 4), -1.45)
		var c := Color(0.35, 0.85, 1.0) if rng.randf() < 0.6 else Color(0.85, 0.45, 1.0)
		for j in 3:
			var cr := MeshInstance3D.new()
			cr.mesh = Meshes.cylinder(0.0, 0.16, 0.7, 6)
			cr.position = p + Vector3(j * 0.18 - 0.18, 0.2, 0.05 * j)
			cr.rotation_degrees.z = (j - 1) * 25.0
			var cm := StandardMaterial3D.new()
			cm.albedo_color = c
			cm.emission_enabled = true
			cm.emission = c
			cm.emission_energy_multiplier = 1.6
			cr.material_override = cm
			add_child(cr)
		var l := OmniLight3D.new()
		l.light_color = c
		l.light_energy = 0.6
		l.omni_range = 4.0
		l.position = p + Vector3(0, 0.4, 0.6)
		add_child(l)


func update(dt: float) -> void:
	t += dt
	for c in clouds:
		c[0].position.x += c[1] * dt
