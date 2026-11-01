# Procedural meshes: everything in the remake is modelled in code, so there are no binary assets
# to import or keep in sync. Smooth normals everywhere, so the outline pass can grow along them.
extends RefCounted

static var _cache := {}


static func _key(parts: Array) -> String:
	return str(parts)


# An ellipsoid with radii r, built at its real size (no node scaling, so outlines stay even).
static func ellipsoid(r: Vector3, seg := 20, rings := 12) -> ArrayMesh:
	var k := _key(["ell", r, seg, rings])
	if _cache.has(k):
		return _cache[k]
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	for j in rings + 1:
		var v := float(j) / rings
		var phi := PI * v
		for i in seg + 1:
			var u := float(i) / seg
			var th := TAU * u
			var n := Vector3(sin(phi) * sin(th), cos(phi), sin(phi) * cos(th))
			var p := Vector3(n.x * r.x, n.y * r.y, n.z * r.z)
			var nn := Vector3(p.x / (r.x * r.x), p.y / (r.y * r.y), p.z / (r.z * r.z)).normalized()
			st.set_normal(nn)
			st.set_uv(Vector2(u, v))
			st.add_vertex(p)
	for j in rings:
		for i in seg:
			var a := j * (seg + 1) + i
			var b := a + seg + 1
			st.add_index(a); st.add_index(a + 1); st.add_index(b)
			st.add_index(a + 1); st.add_index(b + 1); st.add_index(b)
	var m := st.commit()
	_cache[k] = m
	return m


# Revolves a profile of (radius, y) points (bottom to top) around the Y axis.
static func lathe(profile: PackedVector2Array, seg := 24, key := "") -> ArrayMesh:
	if key != "" and _cache.has(key):
		return _cache[key]
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var n := profile.size()
	# profile normals: perpendicular to the tangent, pointing outward
	var pn: Array[Vector2] = []
	for i in n:
		var a := profile[maxi(i - 1, 0)]
		var b := profile[mini(i + 1, n - 1)]
		var t := (b - a).normalized()
		pn.append(Vector2(t.y, -t.x))
	var total := 0.0
	var acc := [0.0]
	for i in range(1, n):
		total += profile[i].distance_to(profile[i - 1])
		acc.append(total)
	for j in n:
		for i in seg + 1:
			var th := TAU * i / seg
			var c := cos(th)
			var s := sin(th)
			var p := profile[j]
			st.set_normal(Vector3(pn[j].x * s, pn[j].y, pn[j].x * c).normalized())
			st.set_uv(Vector2(float(i) / seg, acc[j] / maxf(total, 0.0001)))
			st.add_vertex(Vector3(p.x * s, p.y, p.x * c))
	for j in n - 1:
		for i in seg:
			var a := j * (seg + 1) + i
			var b := a + seg + 1
			st.add_index(a); st.add_index(b); st.add_index(a + 1)
			st.add_index(a + 1); st.add_index(b); st.add_index(b + 1)
	var m := st.commit()
	if key != "":
		_cache[key] = m
	return m


# A box with rounded edges: every vertex of a cube whose faces are subdivided mostly near the
# edges is pushed out from the inner box, so the bevel is round with few vertices.
# UVs run 0..1 across each face (u along the face's first axis, v downward on side faces).
static func rounded_box(size: Vector3, radius: float, mid := 2) -> ArrayMesh:
	var k := _key(["rbox", size, radius, mid])
	if _cache.has(k):
		return _cache[k]
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var h := size * 0.5
	var inner := h - Vector3.ONE * radius
	var t15 := tan(deg_to_rad(15.0))
	var t30 := tan(deg_to_rad(30.0))
	# per axis: the sample coordinates across a face, as fractions of the face (-1..1 scaled later)
	var coords := func(hh: float, ii: float) -> PackedFloat32Array:
		var r := hh - ii
		var c := PackedFloat32Array([-hh, -(ii + r * t30), -(ii + r * t15)])
		for m in mid + 1:
			c.append(-ii + 2.0 * ii * m / mid)
		c.append_array([ii + r * t15, ii + r * t30, hh])
		return c
	var faces := [
		[Vector3(0, 0, 1), Vector3(1, 0, 0), Vector3(0, -1, 0)],
		[Vector3(0, 0, -1), Vector3(-1, 0, 0), Vector3(0, -1, 0)],
		[Vector3(1, 0, 0), Vector3(0, 0, -1), Vector3(0, -1, 0)],
		[Vector3(-1, 0, 0), Vector3(0, 0, 1), Vector3(0, -1, 0)],
		[Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1)],
		[Vector3(0, -1, 0), Vector3(1, 0, 0), Vector3(0, 0, -1)],
	]
	var base := 0
	for f in faces:
		var nrm: Vector3 = f[0]
		var ua: Vector3 = f[1]
		var va: Vector3 = f[2]
		var hu := absf(ua.dot(h))
		var hv := absf(va.dot(h))
		var cu: PackedFloat32Array = coords.call(hu, absf(ua.dot(inner)))
		var cv: PackedFloat32Array = coords.call(hv, absf(va.dot(inner)))
		var nu := cu.size()
		var nv := cv.size()
		for j in nv:
			for i in nu:
				var p := Vector3(nrm.x * h.x, nrm.y * h.y, nrm.z * h.z) + ua * cu[i] + va * cv[j]
				var c := Vector3(clampf(p.x, -inner.x, inner.x), clampf(p.y, -inner.y, inner.y), clampf(p.z, -inner.z, inner.z))
				var d := p - c
				var nn := d.normalized() if d.length() > 0.00001 else nrm
				st.set_normal(nn)
				st.set_uv(Vector2((cu[i] + hu) / (2.0 * hu), (cv[j] + hv) / (2.0 * hv)))
				st.add_vertex(c + nn * radius)
		for j in nv - 1:
			for i in nu - 1:
				var a := base + j * nu + i
				var b := a + nu
				st.add_index(a); st.add_index(a + 1); st.add_index(b)
				st.add_index(a + 1); st.add_index(b + 1); st.add_index(b)
		base += nu * nv
	var m := st.commit()
	_cache[k] = m
	return m


# Edge-concentrated subdivision makes the rounded box's bevel smooth with few vertices.
static func block(size := 1.0, radius := 0.09) -> ArrayMesh:
	return rounded_box(Vector3.ONE * size, radius, 2)


static func capsule(radius: float, height: float) -> CapsuleMesh:
	var k := _key(["cap", radius, height])
	if _cache.has(k):
		return _cache[k]
	var c := CapsuleMesh.new()
	c.radius = radius
	c.height = maxf(height, radius * 2.0)
	c.radial_segments = 16
	c.rings = 6
	_cache[k] = c
	return c


static func cylinder(r_top: float, r_bottom: float, height: float, seg := 24) -> CylinderMesh:
	var k := _key(["cyl", r_top, r_bottom, height, seg])
	if _cache.has(k):
		return _cache[k]
	var c := CylinderMesh.new()
	c.top_radius = r_top
	c.bottom_radius = r_bottom
	c.height = height
	c.radial_segments = seg
	c.rings = 1
	_cache[k] = c
	return c


static func torus(inner: float, outer: float, seg := 24, rings := 10) -> TorusMesh:
	var k := _key(["tor", inner, outer, seg, rings])
	if _cache.has(k):
		return _cache[k]
	var t := TorusMesh.new()
	t.inner_radius = inner
	t.outer_radius = outer
	t.rings = seg
	t.ring_segments = rings
	_cache[k] = t
	return t


# A flat polygon (counter-clockwise, XY plane) extruded along Z by depth, centred on z = 0.
static func extrude(poly: PackedVector2Array, depth: float, key := "") -> ArrayMesh:
	if key != "" and _cache.has(key):
		return _cache[key]
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var idx := Geometry2D.triangulate_polygon(poly)
	var hz := depth * 0.5
	for side in [1.0, -1.0]:
		for t in range(0, idx.size(), 3):
			# Godot's front faces wind clockwise: the CCW polygon is reversed on the front side
			var tri := [idx[t], idx[t + 2], idx[t + 1]]
			if side < 0:
				tri = [idx[t], idx[t + 1], idx[t + 2]]
			for i in tri:
				st.set_normal(Vector3(0, 0, side))
				st.set_uv(poly[i])
				st.add_vertex(Vector3(poly[i].x, poly[i].y, hz * side))
	var n := poly.size()
	for i in n:
		var a := poly[i]
		var b := poly[(i + 1) % n]
		var e := (b - a).normalized()
		var nrm := Vector3(e.y, -e.x, 0)
		var quad := [Vector3(a.x, a.y, hz), Vector3(b.x, b.y, hz), Vector3(b.x, b.y, -hz), Vector3(a.x, a.y, -hz)]
		for q in [0, 1, 2, 0, 2, 3]:
			st.set_normal(nrm)
			st.set_uv(Vector2(float(q), 0))
			st.add_vertex(quad[q])
	var m := st.commit()
	if key != "":
		_cache[key] = m
	return m


static func circle_poly(r: float, n := 24, cx := 0.0, cy := 0.0) -> PackedVector2Array:
	var p := PackedVector2Array()
	for i in n:
		var a := TAU * i / n
		p.append(Vector2(cx + cos(a) * r, cy + sin(a) * r))
	return p


# A crescent: the big circle minus an offset one.
static func crescent_poly(r: float, n := 32) -> PackedVector2Array:
	var outer := circle_poly(r, n)
	var cut := circle_poly(r * 0.82, n, r * 0.42, r * 0.22)
	var res := Geometry2D.clip_polygons(outer, cut)
	return res[0] if res.size() > 0 else outer


static func star_poly(r: float, inner: float, pts := 5) -> PackedVector2Array:
	var p := PackedVector2Array()
	for i in pts * 2:
		var a := PI / 2 + PI * i / pts
		var rr := r if i % 2 == 0 else inner
		p.append(Vector2(cos(a) * rr, sin(a) * rr))
	return p
