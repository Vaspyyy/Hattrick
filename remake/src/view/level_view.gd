# The level in 3D, built from the sim's live map of the current area. One tile is one unit:
# x = tile column, y = rows counted up from the area's bottom, and the play plane is z = 0.
# Ground is one merged mesh (a deep slab with a rounded grass edge), stone a MultiMesh of
# bevelled blocks, and everything that changes (bricks, coins, springs, found blocks) one node
# per cell, swapped when the sim reports the cell in `dirty`.
extends Node3D

const Sim := preload("res://src/sim/sim.gd")
const Meshes := preload("res://src/view/meshes.gd")
const Mats := preload("res://src/view/mats.gd")
const TERRAIN := preload("res://shaders/terrain.gdshader")
const BLOCK := preload("res://shaders/block.gdshader")
const COIN := preload("res://shaders/coin.gdshader")
const CLOTH := preload("res://shaders/cloth.gdshader")

const ZF := 1.25        # the ground slab's front face
const ZB := -3.0        # and its back
const BEV := 0.28       # radius of the rounded grass edge
const NB := 4           # segments in that edge
const BOTTOM := -14.0   # the bottom row reaches down out of sight
const MARGIN := 18      # ground continues past both ends of the area (the camera sees wider at the back)
const DYN := [2, 5, 6, 15, 16, 24, 25, 26, 27, 28, 29]

var sim
var room := -1
var lw := 0
var lh := 0
var cave := 0
var map: PackedByteArray
var static_root: Node3D
var dyn_root: Node3D
var cells := {}         # cell index -> Node3D
var anims := {}         # cell index -> [kind, time]
var crumbles: Array = []   # [Crumble, Node3D]
var checks: Array = []     # [Check, Node3D post, MeshInstance3D banner, MeshInstance3D knob]
var moons: Array = []      # [index, Node3D]
var flag_root: Node3D
var pennant: Node3D
var moon_home := 0      # moon coins brought home on an earlier visit: drawn pale
var t := 0.0
var M := {}             # materials
var _st: SurfaceTool


func _init() -> void:
	M.ground = _terrain(true)
	M.ground_cave = _terrain(false)
	M.stone = _block(0, Color(0.64, 0.66, 0.72), Color(0.38, 0.40, 0.48), Color(0.86, 0.88, 0.94), 0.85)
	M.stone_cave = _block(0, Color(0.52, 0.47, 0.50), Color(0.27, 0.23, 0.27), Color(0.72, 0.66, 0.66), 0.9)
	M.brick = _block(1, Color(0.86, 0.40, 0.18), Color(0.36, 0.15, 0.08), Color(1.0, 0.62, 0.40), 0.85)
	M.crumble = _block(2, Color(0.90, 0.72, 0.42), Color(0.55, 0.36, 0.17), Color(0.97, 0.87, 0.63), 0.8)
	M.found = _block(3, Color(0.36, 0.62, 0.66), Color(0.17, 0.35, 0.41), Color(0.65, 0.87, 0.89), 0.6)
	M.iron = _block(4, Color(0.30, 0.32, 0.36), Color(0.15, 0.16, 0.20), Color(0.52, 0.55, 0.60), 0.45, 0.5)
	M.red = _block(5, Color(0.92, 0.28, 0.25), Color(0.5, 0.1, 0.1), Color(1, 0.6, 0.55), 0.5)
	M.blue = _block(5, Color(0.25, 0.45, 0.95), Color(0.1, 0.2, 0.5), Color(0.6, 0.75, 1), 0.5)
	M.ghost_red = Mats.unshaded(Color(0.95, 0.35, 0.3, 0.22))
	M.ghost_blue = Mats.unshaded(Color(0.3, 0.5, 1.0, 0.22))
	M.brass = Mats.toon(Color(0.98, 0.72, 0.26), true, 0.3, 0.5, 0.025)
	M.brass_dark = Mats.toon(Color(0.78, 0.5, 0.16), true, 0.35, 0.4, 0.02)
	M.hole = Mats.unshaded(Color(0.05, 0.035, 0.02))
	M.steel = Mats.pbr(Color(0.86, 0.89, 0.93), 0.28, 0.65)
	M.steel_dark = Mats.pbr(Color(0.36, 0.39, 0.44), 0.5, 0.4)
	M.spring_red = Mats.toon(Color(0.93, 0.25, 0.22), true, 0.4, 0.4, 0.02)
	M.coil = Mats.pbr(Color(0.65, 0.69, 0.75), 0.3, 0.8)
	M.pole = Mats.pbr(Color(0.88, 0.9, 0.94), 0.25, 0.6)
	M.gold = Mats.pbr(Color(1.0, 0.8, 0.28), 0.25, 0.85, Color(0.25, 0.16, 0.02))
	M.grey_knob = Mats.pbr(Color(0.7, 0.74, 0.8), 0.35, 0.5)
	M.coin = _coin_mat(Color(1.0, 0.76, 0.2), Color(0.8, 0.45, 0.06), 0.16)
	M.coin_star = _coin_mat(Color(1.0, 0.92, 0.55), Color(0.95, 0.6, 0.1), 0.3)
	M.water = _water_mat()


func _terrain(grass: bool) -> ShaderMaterial:
	var m := ShaderMaterial.new()
	m.shader = TERRAIN
	m.set_shader_parameter("grass_on", 1.0 if grass else 0.0)
	if not grass:   # caves: cool, dark earth
		m.set_shader_parameter("dirt_hi", Color(0.46, 0.36, 0.33))
		m.set_shader_parameter("dirt_lo", Color(0.27, 0.21, 0.21))
		m.set_shader_parameter("pebble", Color(0.58, 0.5, 0.46))
	return m


func _block(pattern: int, base: Color, dark: Color, light: Color, rough := 0.8, metal := 0.0) -> ShaderMaterial:
	var m := ShaderMaterial.new()
	m.shader = BLOCK
	m.set_shader_parameter("pattern", pattern)
	m.set_shader_parameter("base", base)
	m.set_shader_parameter("dark", dark)
	m.set_shader_parameter("light", light)
	m.set_shader_parameter("rough", rough)
	m.set_shader_parameter("metal", metal)
	return m


func _coin_mat(c: Color, e: Color, glow: float) -> ShaderMaterial:
	var m := ShaderMaterial.new()
	m.shader = COIN
	m.set_shader_parameter("albedo", c)
	m.set_shader_parameter("edge", e)
	m.set_shader_parameter("glow", glow)
	return m


func _water_mat() -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = Color(0.25, 0.6, 0.95, 0.42)
	m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	m.roughness = 0.1
	m.metallic_specular = 0.8
	m.cull_mode = BaseMaterial3D.CULL_DISABLED
	return m


# ---------- coordinates ----------
func cell_pos(tx: int, ty: int) -> Vector3:   # the centre of a cell
	return Vector3(tx + 0.5, lh - ty - 0.5, 0.0)


func px(x: float, y: float, z := 0.0) -> Vector3:   # game pixels (y down) to world units
	return Vector3(x / 8.0, lh - y / 8.0, z)


func _t(tx: int, ty: int) -> int:   # the map, with the ends of the area continued sideways
	if ty < 0:
		return 0
	if ty >= lh:
		return 1
	return map[ty * lw + clampi(tx, 0, lw - 1)]


func _raw(tx: int, ty: int) -> int:
	if tx < 0 or tx >= lw or ty < 0 or ty >= lh:
		return 0
	return map[ty * lw + tx]


func _solid(t: int) -> bool:
	return (Sim.SOLID >> t) & 1 == 1


# A slope belongs to the ground (grass, the slab's depth) when it sits on ground; else it's stone.
func _ground_slope(tx: int, ty: int) -> bool:
	var yy := ty + 1
	while yy < lh and (_t(tx, yy) == 8 or _t(tx, yy) == 9):
		yy += 1
	return yy >= lh or _t(tx, yy) == 1


func _groundish(tx: int, ty: int) -> bool:
	var t := _t(tx, ty)
	return t == 1 or ((t == 8 or t == 9) and _ground_slope(tx, ty))


# The grass surface above a cell of ground, at the column's left or right edge.
func _surf(tx: int, ty: int, right: bool) -> float:
	var yy := ty
	while yy - 1 >= 0 and _t(tx, yy - 1) == 1:
		yy -= 1
	if yy - 1 >= 0:
		var a := _t(tx, yy - 1)
		if (a == 8 or a == 9) and _ground_slope(tx, yy - 1):
			var lo := float(lh - yy)
			return lo + (1.0 if right == (a == 8) else 0.0)
	return float(lh - yy)


# ---------- building ----------
func build(s) -> void:
	sim = s
	room = s.room
	lw = s.lw
	lh = s.lh
	map = s.wd.rm[room]
	cave = s.L.rooms[room].cave
	for c in get_children():
		c.queue_free()
	static_root = Node3D.new()
	static_root.name = "Static"
	add_child(static_root)
	dyn_root = Node3D.new()
	dyn_root.name = "Cells"
	add_child(dyn_root)
	cells.clear()
	anims.clear()
	_build_ground()
	_build_slopes()
	_build_stone()
	_build_spikes()
	_build_tubes()
	_build_water()
	for ty in lh:
		for tx in lw:
			if map[ty * lw + tx] in DYN:
				_make_cell(tx, ty)
	_build_crumbles()
	_build_checks()
	_build_moons()
	_build_flag()


func rebuild_static() -> void:
	static_root.queue_free()
	static_root = Node3D.new()
	add_child(static_root)
	_build_ground()
	_build_slopes()
	_build_stone()
	_build_spikes()
	_build_tubes()
	_build_water()


func _v(p: Vector3, n: Vector3, top: float, depth: float) -> Array:
	return [p, n, top, depth]


func _emit(v: Array) -> void:
	_st.set_normal(v[1])
	_st.set_uv(Vector2(v[0].x, v[0].y))
	_st.set_uv2(Vector2(v[2], v[3]))
	_st.add_vertex(v[0])


func _tri(a: Array, b: Array, c: Array) -> void:
	# Godot's front faces wind clockwise as seen from outside: flip the ones that don't
	var cr: Vector3 = (b[0] - a[0]).cross(c[0] - a[0])
	var n: Vector3 = a[1] + b[1] + c[1]
	if cr.dot(n) > 0.0:
		_emit(a); _emit(c); _emit(b)
	else:
		_emit(a); _emit(b); _emit(c)


func _quad(a: Array, b: Array, c: Array, d: Array) -> void:
	_tri(a, b, c)
	_tri(a, c, d)


func _build_ground() -> void:
	_st = SurfaceTool.new()
	_st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var any := false
	for ty in lh:
		for tx in range(-MARGIN, lw + MARGIN):
			if _t(tx, ty) != 1:
				continue
			any = true
			var x0 := float(tx)
			var x1 := float(tx + 1)
			var y1 := float(lh - ty)
			var y0 := y1 - 1.0 if ty < lh - 1 else BOTTOM
			var top_open := not _groundish(tx, ty - 1)
			var sl := _surf(tx, ty, false)
			var sr := _surf(tx, ty, true)
			var ice := 0.0
			if tx >= 0 and tx < lw and sim.mvs[room][ty * lw + tx] == Sim.MV_ICE:
				ice = 1.0
			var topv := 2.0 if ice > 0.0 else 1.0
			var yf := y1 - BEV if top_open else y1   # where the flat front face stops
			# front
			_quad(_v(Vector3(x0, y0, ZF), Vector3.BACK, 0, sl - y0), _v(Vector3(x1, y0, ZF), Vector3.BACK, 0, sr - y0),
				_v(Vector3(x1, yf, ZF), Vector3.BACK, 0, sr - yf), _v(Vector3(x0, yf, ZF), Vector3.BACK, 0, sl - yf))
			var arc: Array = []   # the rounded edge's profile (z, y, normal), from the top to the front
			if top_open:
				for i in NB + 1:
					var a := PI * 0.5 * i / NB
					arc.append([ZF - BEV + BEV * sin(a), y1 - BEV + BEV * cos(a), Vector3(0, cos(a), sin(a))])
				_quad(_v(Vector3(x0, y1, ZB), Vector3.UP, topv, 0), _v(Vector3(x1, y1, ZB), Vector3.UP, topv, 0),
					_v(Vector3(x1, y1, ZF - BEV), Vector3.UP, topv, 0), _v(Vector3(x0, y1, ZF - BEV), Vector3.UP, topv, 0))
				for i in NB:
					var p: Array = arc[i]
					var q: Array = arc[i + 1]
					var tp := topv if i < NB / 2 else 0.0
					var tq := topv if i + 1 < NB / 2 else 0.0
					_quad(_v(Vector3(x0, p[1], p[0]), p[2], tp, y1 - p[1]), _v(Vector3(x1, p[1], p[0]), p[2], tp, y1 - p[1]),
						_v(Vector3(x1, q[1], q[0]), q[2], tq, y1 - q[1]), _v(Vector3(x0, q[1], q[0]), q[2], tq, y1 - q[1]))
			# the sides, where the ground ends (a cliff or a pit)
			for side in [-1, 1]:
				if _groundish(tx + side, ty) or _solid(_t(tx + side, ty)) and _t(tx + side, ty) != 4:
					continue
				var x := x0 if side < 0 else x1
				var s := sl if side < 0 else sr
				var n := Vector3(side, 0, 0)
				var poly: Array = [[ZB, y0], [ZB, y1 if top_open else y1]]
				if top_open:
					for p in arc:
						poly.append([p[0], p[1]])
				else:
					poly.append([ZF, y1])
				poly.append([ZF, y0])
				for i in range(1, poly.size() - 1):
					_tri(_v(Vector3(x, poly[0][1], poly[0][0]), n, 0, s - poly[0][1]),
						_v(Vector3(x, poly[i][1], poly[i][0]), n, 0, s - poly[i][1]),
						_v(Vector3(x, poly[i + 1][1], poly[i + 1][0]), n, 0, s - poly[i + 1][1]))
			# underneath an overhang
			if ty < lh - 1 and not _groundish(tx, ty + 1):
				_quad(_v(Vector3(x0, y0, ZB), Vector3.DOWN, 0, sl - y0), _v(Vector3(x1, y0, ZB), Vector3.DOWN, 0, sr - y0),
					_v(Vector3(x1, y0, ZF), Vector3.DOWN, 0, sr - y0), _v(Vector3(x0, y0, ZF), Vector3.DOWN, 0, sl - y0))
	if not any:
		return
	var mi := MeshInstance3D.new()
	mi.mesh = _st.commit()
	mi.material_override = M.ground_cave if cave else M.ground
	static_root.add_child(mi)


func _build_slopes() -> void:
	_st = SurfaceTool.new()
	_st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var stone_slopes: Array = []
	var any := false
	for ty in lh:
		for tx in lw:
			var t := _raw(tx, ty)
			if t != 8 and t != 9:
				continue
			if not _ground_slope(tx, ty):
				stone_slopes.append([tx, ty, t])
				continue
			any = true
			var x0 := float(tx)
			var x1 := float(tx + 1)
			var yb := float(lh - ty - 1)
			var yt := yb + 1.0
			var up := t == 8   # '/' rises to the right
			var xl := x0 if up else x1   # the low end
			var xh := x1 if up else x0   # the high end
			var n := Vector3(-1 if up else 1, 1, 0).normalized()
			_tri(_v(Vector3(xl, yb, ZF), Vector3.BACK, 0, 0), _v(Vector3(xh, yb, ZF), Vector3.BACK, 0, 1),
				_v(Vector3(xh, yt, ZF), Vector3.BACK, 0, 0))
			_quad(_v(Vector3(xl, yb, ZB), n, 1, 0), _v(Vector3(xl, yb, ZF), n, 1, 0),
				_v(Vector3(xh, yt, ZF), n, 1, 0), _v(Vector3(xh, yt, ZB), n, 1, 0))
			var side := 1 if up else -1
			if not _groundish(tx + side, ty) and not _solid(_t(tx + side, ty)):
				var sn := Vector3(side, 0, 0)
				_quad(_v(Vector3(xh, yb, ZB), sn, 0, 1), _v(Vector3(xh, yb, ZF), sn, 0, 1),
					_v(Vector3(xh, yt, ZF), sn, 0, 0), _v(Vector3(xh, yt, ZB), sn, 0, 0))
	if any:
		var mi := MeshInstance3D.new()
		mi.mesh = _st.commit()
		mi.material_override = M.ground_cave if cave else M.ground
		static_root.add_child(mi)
	if stone_slopes.is_empty():
		return
	# slopes in stone: wedges as deep as a block
	_st = SurfaceTool.new()
	_st.begin(Mesh.PRIMITIVE_TRIANGLES)
	for s in stone_slopes:
		var x0 := float(s[0])
		var x1 := x0 + 1.0
		var yb := float(lh - s[1] - 1)
		var yt := yb + 1.0
		var up: bool = s[2] == 8
		var xl := x0 if up else x1
		var xh := x1 if up else x0
		var n := Vector3(-1 if up else 1, 1, 0).normalized()
		for z in [0.5, -0.5]:
			var fn := Vector3(0, 0, signf(z))
			_tri(_v(Vector3(xl, yb, z), fn, 0, 0), _v(Vector3(xh, yb, z), fn, 1, 0), _v(Vector3(xh, yt, z), fn, 1, 1))
		_quad(_v(Vector3(xl, yb, -0.5), n, 0, 0), _v(Vector3(xl, yb, 0.5), n, 0, 1),
			_v(Vector3(xh, yt, 0.5), n, 1, 1), _v(Vector3(xh, yt, -0.5), n, 1, 0))
		var sn := Vector3(1 if up else -1, 0, 0)
		_quad(_v(Vector3(xh, yb, -0.5), sn, 0, 0), _v(Vector3(xh, yb, 0.5), sn, 1, 0),
			_v(Vector3(xh, yt, 0.5), sn, 1, 1), _v(Vector3(xh, yt, -0.5), sn, 0, 1))
	# UV2 carries the face coordinates here; the block shader reads UV, so copy them over
	var arr := _st.commit_to_arrays()
	arr[Mesh.ARRAY_TEX_UV] = arr[Mesh.ARRAY_TEX_UV2]
	var am := ArrayMesh.new()
	am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	var mi2 := MeshInstance3D.new()
	mi2.mesh = am
	mi2.material_override = M.stone_cave if cave else M.stone
	static_root.add_child(mi2)


func _build_stone() -> void:
	var pts: Array = []
	for ty in lh:
		for tx in lw:
			if map[ty * lw + tx] == 4:
				pts.append(Vector2i(tx, ty))
	if pts.is_empty():
		return
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.use_custom_data = true
	mm.mesh = Meshes.rounded_box(Vector3.ONE, 0.08, 2)
	mm.instance_count = pts.size()
	for i in pts.size():
		var p: Vector2i = pts[i]
		mm.set_instance_transform(i, Transform3D(Basis(), cell_pos(p.x, p.y)))
		var h := fposmod(sin(p.x * 12.9898 + p.y * 78.233) * 43758.5453, 1.0)
		mm.set_instance_custom_data(i, Color(h, 0, 0, 0))
	var mmi := MultiMeshInstance3D.new()
	mmi.multimesh = mm
	mmi.material_override = M.stone_cave if cave else M.stone
	static_root.add_child(mmi)


func _build_spikes() -> void:
	var cone := Meshes.cylinder(0.0, 0.2, 0.62, 12)
	var plate := Meshes.rounded_box(Vector3(0.96, 0.14, 0.92), 0.04, 1)
	for ty in lh:
		for tx in lw:
			if map[ty * lw + tx] != 3:
				continue
			var down := _solid(_t(tx, ty - 1)) and not _solid(_t(tx, ty + 1))
			var n := Node3D.new()
			n.position = cell_pos(tx, ty)
			if down:
				n.rotation_degrees.z = 180
			static_root.add_child(n)
			var p := MeshInstance3D.new()
			p.mesh = plate
			p.position.y = -0.43
			p.material_override = M.steel_dark
			n.add_child(p)
			for i in 4:
				var c := MeshInstance3D.new()
				c.mesh = cone
				c.position = Vector3(-0.24 + 0.48 * (i & 1), -0.05, -0.22 + 0.44 * (i >> 1))
				c.material_override = M.steel
				n.add_child(c)


# Tubes: a brass cylinder two tiles wide through each run of body cells, with a collar and a
# dark opening at each mouth. The mouth's opening is a solid disc, so Hatrick going in is hidden.
func _build_tubes() -> void:
	var used := {}
	for tb in sim.L.tubes:
		if tb.room != room:
			continue
		var x: int = tb.x
		var y: int = tb.y
		var dir: int = tb.dir
		var vertical := dir == Sim.T_UP or dir == Sim.T_DOWN
		var step := 1 if dir == Sim.T_UP or dir == Sim.T_LEFT else -1   # from the mouth into the body
		var n := 0
		if vertical:
			while _raw(x, y + step * (n + 1)) == 10:
				used[(y + step * (n + 1)) * lw + x] = true
				used[(y + step * (n + 1)) * lw + x + 1] = true
				n += 1
			var cx := float(x + 1)
			var my := float(lh - y) - 0.5
			_mouth(Vector3(cx, my, 0), Vector3(0, 1 if dir == Sim.T_UP else -1, 0))
			if n > 0:
				var far := y + step * n
				var y_a := float(lh - y) - 0.5 - 0.45 * step
				var y_b := float(lh - far) - 0.5 - 0.5 * step
				if far >= lh - 1 and step > 0:
					y_b = BOTTOM
				_body(Vector3(cx, (y_a + y_b) * 0.5, 0), absf(y_a - y_b), true)
		else:
			while _raw(x + step * (n + 1), y) == 11:
				used[y * lw + x + step * (n + 1)] = true
				used[(y + 1) * lw + x + step * (n + 1)] = true
				n += 1
			var cy := float(lh - y - 1)
			var mx := x + 0.5
			_mouth(Vector3(mx, cy, 0), Vector3(-1 if dir == Sim.T_LEFT else 1, 0, 0))
			if n > 0:
				var x_a := mx + 0.45 * step
				var x_b := mx + (n + 0.5) * step
				_body(Vector3((x_a + x_b) * 0.5, cy, 0), absf(x_b - x_a), false)
	# body cells that no mouth reaches: short sections in pairs
	for ty in lh:
		for tx in lw:
			var i := ty * lw + tx
			if used.has(i):
				continue
			if map[i] == 10 and _raw(tx + 1, ty) == 10:
				used[i] = true; used[i + 1] = true
				_body(Vector3(tx + 1, lh - ty - 0.5, 0), 1.0, true)
			elif map[i] == 11 and _raw(tx, ty + 1) == 11:
				used[i] = true; used[i + lw] = true
				_body(Vector3(tx + 0.5, lh - ty - 1, 0), 1.0, false)


func _body(c: Vector3, length: float, vertical: bool) -> void:
	var n := Node3D.new()
	n.position = c
	if not vertical:
		n.rotation_degrees.z = 90
	static_root.add_child(n)
	var b := MeshInstance3D.new()
	b.mesh = Meshes.cylinder(0.86, 0.86, length, 28)
	b.material_override = M.brass
	n.add_child(b)
	var bands := int(length / 2.0)
	for i in bands:   # brass bands with rivets, every two tiles as in the C art
		var r := MeshInstance3D.new()
		r.mesh = Meshes.torus(0.84, 0.92, 28, 8)
		r.position.y = -length * 0.5 + 1.0 + i * 2.0
		r.material_override = M.brass_dark
		n.add_child(r)


func _mouth(c: Vector3, out: Vector3) -> void:
	var n := Node3D.new()
	n.position = c
	if out.y < 0:
		n.rotation_degrees.z = 180
	elif out.x != 0:
		n.rotation_degrees.z = -90 * out.x
	static_root.add_child(n)
	var prof := PackedVector2Array([Vector2(0.86, -0.46), Vector2(0.99, -0.42), Vector2(1.02, -0.3),
		Vector2(1.02, 0.36), Vector2(0.98, 0.46), Vector2(0.84, 0.48), Vector2(0.76, 0.42), Vector2(0.74, 0.2)])
	var col := MeshInstance3D.new()
	col.mesh = Meshes.lathe(prof, 32, "tubemouth")
	col.material_override = M.brass
	n.add_child(col)
	var hole := MeshInstance3D.new()
	hole.mesh = Meshes.cylinder(0.75, 0.75, 0.9, 28)
	hole.position.y = -0.24
	hole.material_override = M.hole
	n.add_child(hole)


func _build_water() -> void:
	var mv: PackedByteArray = sim.mvs[room]
	_st = SurfaceTool.new()
	_st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var any := false
	for ty in lh:
		for tx in lw:
			var i := ty * lw + tx
			if mv[i] == Sim.MV_POLE:
				var p := MeshInstance3D.new()
				p.mesh = Meshes.cylinder(0.09, 0.09, 1.6, 12)
				p.rotation_degrees.x = 90
				p.position = cell_pos(tx, ty)
				p.material_override = M.pole
				static_root.add_child(p)
			if mv[i] != Sim.MV_WATER:
				continue
			any = true
			var x0 := float(tx)
			var x1 := x0 + 1.0
			var y1 := float(lh - ty)
			if ty > 0 and mv[i - lw] != Sim.MV_WATER:
				y1 -= 0.15
			var y0 := float(lh - ty - 1)
			_quad(_v(Vector3(x0, y0, 1.0), Vector3.BACK, 0, 0), _v(Vector3(x1, y0, 1.0), Vector3.BACK, 0, 0),
				_v(Vector3(x1, y1, 1.0), Vector3.BACK, 0, 0), _v(Vector3(x0, y1, 1.0), Vector3.BACK, 0, 0))
			if ty == 0 or mv[i - lw] != Sim.MV_WATER:
				_quad(_v(Vector3(x0, y1, -1.5), Vector3.UP, 0, 0), _v(Vector3(x1, y1, -1.5), Vector3.UP, 0, 0),
					_v(Vector3(x1, y1, 1.0), Vector3.UP, 0, 0), _v(Vector3(x0, y1, 1.0), Vector3.UP, 0, 0))
	if any:
		var mi := MeshInstance3D.new()
		mi.mesh = _st.commit()
		mi.material_override = M.water
		mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		static_root.add_child(mi)


# ---------- cells that change ----------
func _make_cell(tx: int, ty: int) -> void:
	var i := ty * lw + tx
	var t: int = map[i]
	var n: Node3D
	match t:
		2: n = _box(M.brick, 0.06)
		15: n = _box(M.found, 0.08)
		16: n = _box(M.iron, 0.06)
		28: n = _box(M.red, 0.1)
		29: n = _box(M.blue, 0.1)
		24: n = _box(M.ghost_red, 0.1)
		25: n = _box(M.ghost_blue, 0.1)
		6: n = _coin()
		5: n = _spring()
		26: n = _post()
		27: n = _switch()
	if n == null:
		return
	n.position = cell_pos(tx, ty)
	dyn_root.add_child(n)
	cells[i] = n


func _box(m: Material, r: float) -> Node3D:
	var mi := MeshInstance3D.new()
	mi.mesh = Meshes.rounded_box(Vector3.ONE, r, 2)
	mi.material_override = m
	return mi


static func coin_meshes() -> Array:
	var oval := PackedVector2Array()
	for i in 28:
		var a := TAU * i / 28.0
		oval.append(Vector2(cos(a) * 0.3, sin(a) * 0.38))
	var inner := PackedVector2Array()
	for i in 28:
		var a := TAU * i / 28.0
		inner.append(Vector2(cos(a) * 0.2, sin(a) * 0.27))
	return [Meshes.extrude(oval, 0.1, "coin"), Meshes.extrude(inner, 0.13, "coin_in")]


func _coin() -> Node3D:
	var n := Node3D.new()
	var ms := coin_meshes()
	var a := MeshInstance3D.new()
	a.mesh = ms[0]
	a.material_override = M.coin
	n.add_child(a)
	var b := MeshInstance3D.new()
	b.mesh = ms[1]
	b.material_override = M.coin_star
	n.add_child(b)
	return n


func _spring() -> Node3D:
	var n := Node3D.new()
	var base := MeshInstance3D.new()
	base.mesh = Meshes.rounded_box(Vector3(0.9, 0.14, 0.9), 0.04, 1)
	base.position.y = -0.43
	base.material_override = M.steel_dark
	n.add_child(base)
	var coil := Node3D.new()
	coil.name = "coil"
	coil.position.y = -0.36
	n.add_child(coil)
	for i in 3:
		var r := MeshInstance3D.new()
		r.mesh = Meshes.torus(0.2, 0.3, 20, 8)
		r.position.y = 0.06 + i * 0.13
		r.material_override = M.coil
		coil.add_child(r)
	var top := MeshInstance3D.new()
	top.name = "top"
	top.mesh = Meshes.rounded_box(Vector3(0.94, 0.3, 0.94), 0.1, 2)
	top.position.y = 0.3
	top.material_override = M.spring_red
	n.add_child(top)
	return n


func _post() -> Node3D:   # a cap post: Hatrick's cap hangs on it
	var n := Node3D.new()
	var p := MeshInstance3D.new()
	p.mesh = Meshes.cylinder(0.08, 0.1, 1.0, 12)
	p.material_override = M.pole
	n.add_child(p)
	var k := MeshInstance3D.new()
	k.mesh = Meshes.ellipsoid(Vector3(0.16, 0.16, 0.16))
	k.position.y = 0.5
	k.material_override = M.gold
	n.add_child(k)
	return n


func _switch() -> Node3D:
	var n := _box(M.iron, 0.06)
	var b := MeshInstance3D.new()
	b.mesh = Meshes.cylinder(0.3, 0.34, 0.16, 20)
	b.position.y = 0.55
	b.material_override = M.red
	n.add_child(b)
	return n


func _build_crumbles() -> void:
	crumbles.clear()
	for c in sim.wd.cr:
		if c.room != room:
			continue
		var n := _box(M.crumble, 0.08)
		n.position = cell_pos(c.x, c.y)
		dyn_root.add_child(n)
		crumbles.append([c, n])


func _build_checks() -> void:
	checks.clear()
	for c in sim.wd.ck:
		if c.room != room:
			continue
		var n := Node3D.new()
		n.position = Vector3(c.x + 0.5, lh - c.y - 1, -0.35)
		dyn_root.add_child(n)
		var base := MeshInstance3D.new()
		base.mesh = Meshes.cylinder(0.2, 0.26, 0.14, 16)
		base.position.y = 0.07
		base.material_override = M.steel_dark
		n.add_child(base)
		var pole := MeshInstance3D.new()
		pole.mesh = Meshes.cylinder(0.065, 0.065, 2.0, 12)
		pole.position.y = 1.0
		pole.material_override = M.pole
		n.add_child(pole)
		var knob := MeshInstance3D.new()
		knob.mesh = Meshes.ellipsoid(Vector3(0.13, 0.13, 0.13))
		knob.position.y = 2.05
		knob.material_override = M.grey_knob
		n.add_child(knob)
		var banner := _cloth(Vector2(0.85, 0.55), 0.0, Color(0.49, 0.53, 0.59))
		banner.position = Vector3(0.06, 0.45, 0)
		n.add_child(banner)
		checks.append([c, n, banner, knob])


func _cloth(size: Vector2, tri: float, c: Color) -> MeshInstance3D:
	var pm := PlaneMesh.new()
	pm.size = size
	pm.subdivide_width = 10
	pm.subdivide_depth = 4
	pm.orientation = PlaneMesh.FACE_Z
	pm.center_offset = Vector3(size.x * 0.5, 0, 0)
	var mi := MeshInstance3D.new()
	mi.mesh = pm
	var m := ShaderMaterial.new()
	m.shader = CLOTH
	m.set_shader_parameter("albedo", c)
	m.set_shader_parameter("tri", tri)
	mi.material_override = m
	return mi


func _build_moons() -> void:
	moons.clear()
	for i in sim.L.moons.size():
		var m: Dictionary = sim.L.moons[i]
		if m.room != room:
			continue
		var n := moon_node((moon_home >> i) & 1 == 1)
		n.position = cell_pos(m.x, m.y)
		dyn_root.add_child(n)
		moons.append([i, n])


static func moon_node(ghost: bool) -> Node3D:
	var n := Node3D.new()
	var spin := Node3D.new()
	spin.name = "spin"
	n.add_child(spin)
	var a := 0.5 if ghost else 1.0
	var disc := MeshInstance3D.new()
	disc.mesh = Meshes.extrude(Meshes.circle_poly(0.78, 36), 0.16, "moondisc")
	var dm := StandardMaterial3D.new()
	dm.albedo_color = Color(0.8, 0.86, 1.0, a)
	dm.metallic = 0.6
	dm.roughness = 0.25
	dm.emission_enabled = true
	dm.emission = Color(0.25, 0.32, 0.55)
	if ghost:
		dm.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	disc.material_override = dm
	spin.add_child(disc)
	var rim := MeshInstance3D.new()
	rim.mesh = Meshes.torus(0.7, 0.84, 36, 8)
	rim.rotation_degrees.x = 90
	var rm := StandardMaterial3D.new()
	rm.albedo_color = Color(0.45, 0.56, 0.85, a)
	rm.metallic = 0.7
	rm.roughness = 0.3
	if ghost:
		rm.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	rim.material_override = rm
	spin.add_child(rim)
	var cres := MeshInstance3D.new()
	cres.mesh = Meshes.extrude(Meshes.crescent_poly(0.52, 40), 0.26, "mooncrescent")
	cres.rotation_degrees.z = 30
	var cm := StandardMaterial3D.new()
	cm.albedo_color = Color(1.0, 0.82, 0.28, a)
	cm.metallic = 0.85
	cm.roughness = 0.22
	cm.emission_enabled = true
	cm.emission = Color(0.45, 0.3, 0.02)
	if ghost:
		cm.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	cres.material_override = cm
	spin.add_child(cres)
	if not ghost:
		var glow := OmniLight3D.new()
		glow.light_color = Color(0.75, 0.85, 1.0)
		glow.light_energy = 0.8
		glow.omni_range = 3.0
		glow.shadow_enabled = false
		n.add_child(glow)
	return n


func _build_flag() -> void:
	flag_root = null
	pennant = null
	if room != 0:
		return
	flag_root = Node3D.new()
	dyn_root.add_child(flag_root)
	var x: float = sim.gx + 0.5
	var top := float(lh - sim.gy)
	var bottom: float = float(lh) - sim.gb / 8.0
	var pole := MeshInstance3D.new()
	pole.mesh = Meshes.cylinder(0.085, 0.085, top - bottom, 12)
	pole.position = Vector3(x, (top + bottom) * 0.5, -0.2)
	pole.material_override = M.pole
	flag_root.add_child(pole)
	var ball := MeshInstance3D.new()
	ball.mesh = Meshes.ellipsoid(Vector3(0.24, 0.24, 0.24))
	ball.position = Vector3(x, top + 0.18, -0.2)
	ball.material_override = M.gold
	flag_root.add_child(ball)
	var base := MeshInstance3D.new()
	base.mesh = Meshes.rounded_box(Vector3(0.8, 0.5, 0.8), 0.08, 2)
	base.position = Vector3(x, bottom + 0.25, -0.2)
	base.material_override = M.stone
	flag_root.add_child(base)
	pennant = Node3D.new()
	pennant.position = Vector3(x, top, -0.2)
	flag_root.add_child(pennant)
	var cloth := _cloth(Vector2(1.5, 1.05), 1.0, Color(0.18, 0.78, 0.36))
	cloth.rotation_degrees.y = 180   # pointing left, away from the pole
	cloth.position.y = -0.55
	pennant.add_child(cloth)


# ---------- per step: the sim's changes ----------
func apply() -> void:
	var restatic := false
	for d in sim.dirty:
		if d[0] != room:
			continue
		var i: int = d[2] * lw + d[1]
		if cells.has(i):
			cells[i].queue_free()
			cells.erase(i)
		var t: int = map[i]
		if t in DYN:
			_make_cell(d[1], d[2])
		elif t != 0 and t != 13 and t != 14:
			restatic = true
	sim.dirty.clear()
	if restatic:
		rebuild_static()


func on_event(e: Array) -> void:
	match e[0]:
		"spring":
			if e[1] == room:
				anims[e[3] * lw + e[2]] = ["spring", 0.0]
		"reveal":
			if e[1] == room:
				anims[e[3] * lw + e[2]] = ["bump", 0.0]


# ---------- per frame ----------
func update(dt: float) -> void:
	t += dt
	for i in anims.keys():
		var a: Array = anims[i]
		a[1] += dt
		var n: Node3D = cells.get(i)
		if n == null or not is_instance_valid(n):
			anims.erase(i)
			continue
		var base := cell_pos(i % lw, i / lw)
		if a[0] == "bump":
			n.position = base + Vector3(0, sin(minf(a[1] / 0.2, 1.0) * PI) * 0.35, 0)
			if a[1] > 0.2:
				anims.erase(i)
		elif a[0] == "spring":
			var k: float = a[1]
			var s := 1.0 - 0.55 * exp(-k * 9.0) * cos(k * 26.0) if k > 0.04 else 0.45
			var coil: Node3D = n.get_node("coil")
			var top: Node3D = n.get_node("top")
			coil.scale.y = clampf(s, 0.35, 1.6)
			top.position.y = -0.36 + 0.06 + 0.39 * coil.scale.y + 0.27
			if k > 0.8:
				coil.scale.y = 1.0
				top.position.y = 0.3
				anims.erase(i)
	for c in crumbles:
		var cr = c[0]
		var n: Node3D = c[1]
		n.visible = cr.state != 2
		if cr.state == 0:
			var p := cell_pos(cr.x, cr.y)
			if cr.t:
				p.x += (0.06 if (sim.fr >> 1) & 1 else -0.06) * minf(cr.t / 10.0, 1.0)
				p.y -= 0.06 if cr.t > 20 and (sim.fr & 2) else 0.0
			n.position = p
			n.rotation = Vector3.ZERO
		elif cr.state == 1:
			n.position = Vector3(cr.x + 0.5, lh - (cr.fy / 256.0) / 8.0 - 0.5, 0)
			n.rotation.z += dt * 1.5 * (1 if cr.x & 1 else -1)
	for c in checks:
		var ck = c[0]
		var banner: MeshInstance3D = c[2]
		var up: float = 0.0 if not ck.up else minf(ck.up / 20.0, 1.0)
		banner.position.y = lerpf(0.45, 1.42, up)
		var col := Color(0.18, 0.82, 0.71) if ck.up else Color(0.49, 0.53, 0.59)
		banner.material_override.set_shader_parameter("albedo", col)
		banner.material_override.set_shader_parameter("amp", 0.1 if ck.up else 0.02)
		c[3].material_override = M.gold if ck.up else M.grey_knob
	for m in moons:
		var n: Node3D = m[1]
		n.visible = not ((sim.wd.moongot >> m[0]) & 1)
		var mm: Dictionary = sim.L.moons[m[0]]
		n.position = cell_pos(mm.x, mm.y) + Vector3(0, sin(t * 2.2 + m[0]) * 0.1, 0)
		n.get_node("spin").rotation.y = sin(t * 1.4 + m[0]) * 0.7
	if pennant:
		var py: float = sim.flagy if sim.st == Sim.WIN else sim.gy * 8 + 1
		pennant.position.y = lh - py / 8.0
