# Materials: the soft cel look for characters (toon.gdshader with an inverted-hull outline in a
# darker shade of the same colour) and a few shared PBR materials for the world.
extends RefCounted

const TOON := preload("res://shaders/toon.gdshader")
const OUTLINE := preload("res://shaders/outline.gdshader")

static var _cache := {}


static func outline_color(c: Color) -> Color:
	var h := c.h
	var s := minf(c.s * 1.15 + 0.1, 1.0)
	var v := c.v * 0.32
	return Color.from_hsv(h, s, v)


# unique: a fresh material (Hatrick flashes his own materials when hit)
static func toon(c: Color, outline := true, rough := 0.55, rim := 0.35, width := 0.018, unique := false) -> ShaderMaterial:
	var k := str(["toon", c, outline, rough, rim, width])
	if not unique and _cache.has(k):
		return _cache[k]
	var m := ShaderMaterial.new()
	m.shader = TOON
	m.set_shader_parameter("albedo", c)
	m.set_shader_parameter("roughness", rough)
	m.set_shader_parameter("rim", rim)
	if outline:
		var o := ShaderMaterial.new()
		o.shader = OUTLINE
		o.set_shader_parameter("color", outline_color(c))
		o.set_shader_parameter("width", width)
		m.next_pass = o
	if not unique:
		_cache[k] = m
	return m


static func pbr(c: Color, rough := 0.5, metal := 0.0, emit := Color(0, 0, 0)) -> StandardMaterial3D:
	var k := str(["pbr", c, rough, metal, emit])
	if _cache.has(k):
		return _cache[k]
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = rough
	m.metallic = metal
	if emit != Color(0, 0, 0):
		m.emission_enabled = true
		m.emission = emit
	_cache[k] = m
	return m


static func unshaded(c: Color, alpha := false) -> StandardMaterial3D:
	var k := str(["unshaded", c, alpha])
	if _cache.has(k):
		return _cache[k]
	var m := StandardMaterial3D.new()
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	m.albedo_color = c
	if alpha or c.a < 1.0:
		m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	_cache[k] = m
	return m
