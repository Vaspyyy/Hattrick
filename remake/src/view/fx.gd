# Particles and pop-ups from the sim's events: bursts of little cubes, sparkles, dust puffs,
# brick chunks, coins hopping out of found blocks and score numbers drifting up.
extends Node3D

const Meshes := preload("res://src/view/meshes.gd")
const Mats := preload("res://src/view/mats.gd")
const Fonts := preload("res://src/ui/fonts.gd")
const LevelView := preload("res://src/view/level_view.gd")

var lh := 32
var parts: Array = []      # [node, vel, life, max, kind, spin]
var level                  # the level view, for brick and coin materials
var _star_mesh: Mesh
var _puff_mesh: Mesh
var _cube_mesh: Mesh
var _chunk_mesh: Mesh
var _add_mats := {}


func _ready() -> void:
	_star_mesh = Meshes.extrude(Meshes.star_poly(0.16, 0.07, 4), 0.02, "sparkle")
	_puff_mesh = Meshes.ellipsoid(Vector3(0.16, 0.16, 0.16), 10, 6)
	_cube_mesh = Meshes.rounded_box(Vector3.ONE * 0.16, 0.03, 1)
	_chunk_mesh = Meshes.rounded_box(Vector3(0.42, 0.34, 0.42), 0.05, 1)


func clear() -> void:
	for p in parts:
		p[0].queue_free()
	parts.clear()


static func col(c: int) -> Color:
	return Color.hex((c << 8) | 0xff)


func _px(x: float, y: float, z := 0.3) -> Vector3:
	return Vector3(x / 8.0, lh - y / 8.0, z)


func _glow(c: Color) -> StandardMaterial3D:
	var k := c.to_html()
	if _add_mats.has(k):
		return _add_mats[k]
	var m := StandardMaterial3D.new()
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	m.albedo_color = c
	m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	m.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
	m.billboard_mode = BaseMaterial3D.BILLBOARD_ENABLED
	m.cull_mode = BaseMaterial3D.CULL_DISABLED
	_add_mats[k] = m
	return m


func _add(mesh: Mesh, mat: Material, pos: Vector3, vel: Vector3, life: float, kind: String, spin := Vector3.ZERO) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.material_override = mat
	mi.position = pos
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(mi)
	parts.append([mi, vel, life, life, kind, spin])
	return mi


func on_event(e: Array) -> void:
	match e[0]:
		"burst":
			var c := col(e[3])
			var m := Mats.toon(c, false, 0.5, 0.3)
			for i in e[4]:
				var a: float = TAU * i / float(e[4]) + randf() * 0.5
				var v := Vector3(cos(a) * randf_range(3.0, 6.0), sin(a) * randf_range(3.0, 6.0) + 4.0, randf_range(-2.0, 3.0))
				_add(_cube_mesh, m, _px(e[1], e[2]), v, randf_range(0.4, 0.65), "cube", Vector3(randf(), randf(), randf()) * 12.0)
		"sparkle":
			var c := col(e[3])
			for i in e[4]:
				var a := randf() * TAU
				var v := Vector3(cos(a), sin(a) + 0.6, 0) * randf_range(1.5, 4.0)
				_add(_star_mesh, _glow(c), _px(e[1], e[2], 0.6), v, randf_range(0.35, 0.6), "spark")
		"dust":
			var m := Mats.unshaded(Color(1, 0.97, 0.9, 0.7))
			for i in e[4]:
				var v := Vector3((-e[3] if e[3] else randf_range(-1.0, 1.0)) * randf_range(1.0, 2.5), randf_range(0.6, 1.6), randf_range(-0.8, 0.8))
				_add(_puff_mesh, m, _px(e[1] + randf_range(-2, 2), e[2] - 1, randf_range(-0.2, 0.4)), v, randf_range(0.3, 0.5), "puff")
		"smash":
			if level and e[1] == level.room:
				var base: Vector3 = level.cell_pos(e[2], e[3])
				for i in 4:
					var sx := -1.0 if i & 1 == 0 else 1.0
					var sy := 1.0 if i < 2 else -0.2
					var v := Vector3(sx * randf_range(3.0, 4.5), 9.0 + sy * 3.0, randf_range(0.5, 3.0))
					_add(_chunk_mesh, level.M.brick, base + Vector3(sx * 0.22, sy * 0.2, 0), v, 1.2, "chunk", Vector3(randf_range(-9, 9), randf_range(-9, 9), randf_range(-9, 9)))
		"reveal":
			if level and e[1] == level.room:
				coin_pop(level.cell_pos(e[2], e[3]) + Vector3(0, 0.8, 0))
		"coin":
			if level and e[1] == level.room:
				var p: Vector3 = level.cell_pos(e[2], e[3])
				for i in 6:
					var a := TAU * i / 6.0
					_add(_star_mesh, _glow(Color(1.0, 0.9, 0.4)), p + Vector3(0, 0, 0.5), Vector3(cos(a), sin(a), 0) * 3.0, 0.35, "spark")
		"score":
			score_pop(e[1], e[2], e[3])
		"splash":
			for i in 10:
				var v := Vector3(randf_range(-2.5, 2.5), randf_range(4.0, 8.0), randf_range(-1.0, 1.0))
				_add(_puff_mesh, Mats.unshaded(Color(0.75, 0.9, 1.0, 0.8)), _px(e[1], e[2]), v, 0.6, "drop")


func coin_pop(pos: Vector3) -> void:
	var n := Node3D.new()
	var ms: Array = LevelView.coin_meshes()
	for j in 2:
		var mi := MeshInstance3D.new()
		mi.mesh = ms[j]
		mi.material_override = level.M.coin if j == 0 else level.M.coin_star
		n.add_child(mi)
	n.position = pos
	add_child(n)
	parts.append([n, Vector3(0, 13.0, 0), 0.55, 0.55, "coin", Vector3(0, 25.0, 0)])


func score_pop(x: float, y: float, v: int) -> void:
	var l := Label3D.new()
	l.text = str(v)
	l.font = Fonts.bold()
	l.font_size = 64
	l.pixel_size = 0.0075
	l.outline_size = 14
	l.outline_modulate = Color(0.1, 0.12, 0.2)
	l.modulate = Color.WHITE
	l.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	l.no_depth_test = true
	l.position = _px(x, y - 6, 1.5)
	add_child(l)
	parts.append([l, Vector3(0, 2.2, 0), 0.75, 0.75, "score", Vector3.ZERO])


func update(dt: float) -> void:
	for p in parts.duplicate():
		var n: Node3D = p[0]
		p[2] -= dt
		var k: float = p[2] / p[3]   # 1 -> 0
		var v: Vector3 = p[1]
		match p[4]:
			"cube", "chunk", "drop":
				v.y -= (30.0 if p[4] == "chunk" else 22.0) * dt
				n.rotation += p[5] * dt
				n.scale = Vector3.ONE * clampf(k * 2.0, 0.0, 1.0) if p[4] != "chunk" else Vector3.ONE
			"spark":
				v *= exp(-3.0 * dt)
				n.scale = Vector3.ONE * (0.5 + sin(k * PI) * 1.2)
				n.rotation.z += dt * 6.0
			"puff":
				v *= exp(-4.0 * dt)
				n.scale = Vector3.ONE * (1.6 - k)
				(n as MeshInstance3D).transparency = 1.0 - k
			"coin":
				v.y -= 40.0 * dt
				n.rotation.y += p[5].y * dt
				if p[2] <= 0.0:
					on_event(["sparkle", n.position.x * 8.0, (lh - n.position.y) * 8.0, 0xfff0a0, 6])
			"score":
				v *= exp(-2.5 * dt)
				var l := n as Label3D
				l.modulate.a = clampf(k * 3.0, 0.0, 1.0)
				l.outline_modulate.a = l.modulate.a
				l.modulate = Color(1, 1, 1, l.modulate.a) if (p[2] > 0.18 or int(p[2] * 30) & 1) else Color(1, 0.85, 0.3, l.modulate.a)
		p[1] = v
		n.position += v * dt
		if p[2] <= 0.0:
			n.queue_free()
			parts.erase(p)
