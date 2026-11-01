# The HUD and the screen overlays: coins, score, time and hearts, moon coin slots, the level
# card at the start, the course clear tally, and the iris wipe that opens on Hatrick after a
# (re)start and closes after a death or the flag, as in the C game.
extends CanvasLayer

const Sim := preload("res://src/sim/sim.gd")
const Fonts := preload("res://src/ui/fonts.gd")

const IRIS := """
shader_type canvas_item;
uniform vec2 center = vec2(0.5);
uniform float radius = 2.0;
uniform float aspect = 1.7778;
void fragment() {
	vec2 d = (UV - center) * vec2(aspect, 1.0);
	float r = length(d);
	COLOR = vec4(0.047, 0.055, 0.094, smoothstep(radius - 0.004, radius + 0.004, r));
}
"""

var root: Control
var draw: Control
var coins_l: Label
var score_l: Label
var time_l: Label
var card: Control
var card_l: Label
var card_sub: Label
var clear_l: Label
var tally_l: Label
var iris: ColorRect
var iris_mat: ShaderMaterial
var sim
var card_t := 99.0
var hp_show := 3.0
var heart_pulse := 0.0
var moons_total := 0
var moon_home := 0
var coin_spin := 0.0
var t := 0.0


func _ready() -> void:
	layer = 5
	root = Control.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(root)
	draw = Control.new()
	draw.set_anchors_preset(Control.PRESET_FULL_RECT)
	draw.mouse_filter = Control.MOUSE_FILTER_IGNORE
	draw.draw.connect(_draw_hud)
	root.add_child(draw)
	coins_l = _label(54, HORIZONTAL_ALIGNMENT_LEFT)
	_place(coins_l, 0, 0, 104, 22, 400, 92)
	score_l = _label(54, HORIZONTAL_ALIGNMENT_CENTER)
	_place(score_l, 0.5, 0, -200, 22, 200, 92)
	time_l = _label(54, HORIZONTAL_ALIGNMENT_RIGHT)
	_place(time_l, 1, 0, -300, 22, -40, 92)
	# the level card: a ribbon with the level's name
	card = Control.new()
	card.set_anchors_preset(Control.PRESET_FULL_RECT)
	card.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(card)
	card_sub = _label(38, HORIZONTAL_ALIGNMENT_CENTER, card)
	_place(card_sub, 0.5, 0.5, -700, -128, 700, -78)
	card_l = _label(100, HORIZONTAL_ALIGNMENT_CENTER, card)
	_place(card_l, 0.5, 0.5, -700, -88, 700, 40)
	card_sub.add_theme_color_override("font_color", Color(1.0, 0.86, 0.4))
	clear_l = _label(110, HORIZONTAL_ALIGNMENT_CENTER)
	_place(clear_l, 0.5, 0.5, -700, -260, 700, -120)
	clear_l.text = "COURSE CLEAR!"
	clear_l.add_theme_color_override("font_color", Color(1.0, 0.9, 0.35))
	tally_l = _label(50, HORIZONTAL_ALIGNMENT_CENTER)
	_place(tally_l, 0.5, 0.5, -700, -110, 700, -40)
	iris = ColorRect.new()
	iris.set_anchors_preset(Control.PRESET_FULL_RECT)
	iris.mouse_filter = Control.MOUSE_FILTER_IGNORE
	iris_mat = ShaderMaterial.new()
	var sh := Shader.new()
	sh.code = IRIS
	iris_mat.shader = sh
	iris.material = iris_mat
	add_child(iris)
	iris.visible = false


func _label(size: int, align: HorizontalAlignment, parent: Control = null) -> Label:
	var l := Label.new()
	l.add_theme_font_override("font", Fonts.bold())
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_color", Color.WHITE)
	l.add_theme_color_override("font_outline_color", Color(0.08, 0.07, 0.12))
	l.add_theme_constant_override("outline_size", maxi(8, size / 5))
	l.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.35))
	l.add_theme_constant_override("shadow_offset_y", 4)
	l.add_theme_constant_override("shadow_offset_x", 0)
	l.horizontal_alignment = align
	l.mouse_filter = Control.MOUSE_FILTER_IGNORE
	(parent if parent else root).add_child(l)
	return l


# Anchors a control at (ax, ay) of the screen with its corners at these offsets from there.
static func _place(c: Control, ax: float, ay: float, l: float, t: float, r: float, b: float) -> void:
	c.anchor_left = ax
	c.anchor_right = ax
	c.anchor_top = ay
	c.anchor_bottom = ay
	c.offset_left = l
	c.offset_top = t
	c.offset_right = r
	c.offset_bottom = b


func show_card(title: String, sub := "") -> void:
	card_l.text = title.to_upper()
	card_sub.text = sub
	card_t = 0.0


func set_visible_hud(v: bool) -> void:
	root.visible = v


func update(s, dt: float, screen_pos: Vector2) -> void:
	sim = s
	t += dt
	coin_spin += dt * 4.0
	coins_l.text = "× %02d" % s.coins
	score_l.text = "%06d" % s.score
	var secs: int = (s.left + 59) / 60
	time_l.text = "%03d" % secs
	var hurry: bool = secs <= 100 and s.st != Sim.WIN
	time_l.add_theme_color_override("font_color", Color(1.0, 0.35, 0.3) if hurry and int(t * 4.0) & 1 else (Color(1.0, 0.55, 0.45) if hurry else Color.WHITE))
	moons_total = s.L.moons.size()
	if float(s.hp) != hp_show:
		if float(s.hp) < hp_show:
			heart_pulse = 1.0
		hp_show = s.hp
	heart_pulse = maxf(0.0, heart_pulse - dt * 2.5)
	# the level card
	card_t += dt
	var a := 0.0
	if card_t < 2.6:
		a = clampf(card_t / 0.25, 0.0, 1.0) * clampf((2.6 - card_t) / 0.4, 0.0, 1.0)
	card.modulate.a = a
	card.visible = a > 0.0
	card.position.y = (1.0 - clampf(card_t / 0.35, 0.0, 1.0)) * -40.0
	# the course clear
	var win: bool = s.st == Sim.WIN and s.wphase >= 1
	clear_l.visible = win
	tally_l.visible = win and s.wphase >= 2
	if win:
		var k := clampf(float(s.wt) / 12.0, 0.0, 1.0) if s.wphase == 1 else 1.0
		clear_l.scale = Vector2.ONE * (0.6 + 0.4 * k + 0.05 * sin(t * 6.0))
		clear_l.pivot_offset = clear_l.size * 0.5
		tally_l.text = "TIME BONUS  %d × 50" % s.tally
	# iris: opens on Hatrick after a (re)start, closes before a respawn or the next level
	var f := 256
	if s.fr < 24:
		f = s.fr * 256 / 24
	elif s.st == Sim.DEAD and s.stt > 36:
		f = (60 - s.stt) * 256 / 24
	elif s.st == Sim.WIN and s.wphase == 3 and s.wt > 21:
		f = (45 - s.wt) * 256 / 24
	if f < 256:
		iris.visible = true
		var vs := root.get_viewport_rect().size
		var r := float(f * f) / 65536.0 * 1.25 * (vs.x / vs.y)
		iris_mat.set_shader_parameter("center", screen_pos / vs)
		iris_mat.set_shader_parameter("aspect", vs.x / vs.y)
		iris_mat.set_shader_parameter("radius", maxf(r, 0.0))
	else:
		iris.visible = false
	draw.queue_redraw()


func _heart(c: Vector2, r: float, fill: Color) -> void:
	var pts := PackedVector2Array()
	for i in 40:
		var a := TAU * i / 40.0
		var x := 16.0 * pow(sin(a), 3.0)
		var y := -(13.0 * cos(a) - 5.0 * cos(2.0 * a) - 2.0 * cos(3.0 * a) - cos(4.0 * a))
		pts.append(c + Vector2(x, y) * r / 16.0)
	var out := PackedVector2Array()
	for p in pts:
		out.append(c + (p - c) * 1.22)
	draw.draw_colored_polygon(out, Color(0.08, 0.07, 0.12))
	draw.draw_colored_polygon(pts, fill)
	if fill.a > 0.5 and fill != Color(0.3, 0.28, 0.36):
		draw.draw_circle(c + Vector2(-r * 0.42, -r * 0.35), r * 0.2, Color(1, 1, 1, 0.55))


func _draw_hud() -> void:
	if sim == null:
		return
	# coin icon, turning
	var c := Vector2(66, 60)
	var w := absf(cos(coin_spin)) * 0.75 + 0.25
	draw.draw_set_transform(c, 0.0, Vector2(w, 1.0))
	draw.draw_circle(Vector2.ZERO, 27.0, Color(0.08, 0.07, 0.12))
	draw.draw_circle(Vector2.ZERO, 22.0, Color(0.85, 0.5, 0.08))
	draw.draw_circle(Vector2(1, -1), 18.0, Color(1.0, 0.8, 0.25))
	draw.draw_rect(Rect2(-3, -12, 6, 24), Color(1.0, 0.95, 0.7))
	draw.draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)
	# hearts
	for i in Sim.MAXHP:
		var full: bool = i < sim.hp
		var pulse := heart_pulse if i == sim.hp else 0.0
		var r := 21.0 * (1.0 + pulse * 0.4)
		var hc := Color(1.0, 0.32, 0.4) if full else Color(0.3, 0.28, 0.36)
		if full and sim.healt and int(t * 12.0) & 1:
			hc = Color(1.0, 0.75, 0.8)
		_heart(Vector2(58 + i * 54, 128), r, hc)
	# moon coin slots
	for i in moons_total:
		var p := Vector2(52 + i * 46, 186)
		var got: bool = (sim.wd.moongot >> i) & 1
		var home: bool = (moon_home >> i) & 1
		draw.draw_circle(p, 19.0, Color(0.08, 0.07, 0.12))
		if got:
			draw.draw_circle(p, 15.0, Color(0.8, 0.86, 1.0))
			draw.draw_circle(p + Vector2(-3, 0), 10.0, Color(1.0, 0.82, 0.28))
			draw.draw_circle(p + Vector2(2, -3), 9.0, Color(0.8, 0.86, 1.0))
		elif home:
			draw.draw_circle(p, 15.0, Color(0.5, 0.56, 0.72))
		else:
			draw.draw_circle(p, 15.0, Color(0.22, 0.24, 0.34))
	# the card's ribbon
	if card.visible:
		var vs := root.get_viewport_rect().size
		var y := vs.y * 0.5 - 140 + card.position.y
		var a := card.modulate.a
		var w := vs.x * minf(card_t / 0.3, 1.0)
		draw.draw_rect(Rect2(vs.x * 0.5 - w * 0.5, y, w, 200), Color(0.93, 0.37, 0.11, 0.88 * a))
		draw.draw_rect(Rect2(vs.x * 0.5 - w * 0.5, y + 200, w, 10), Color(0.6, 0.18, 0.05, 0.8 * a))
		draw.draw_rect(Rect2(vs.x * 0.5 - w * 0.5, y - 10, w, 10), Color(1.0, 0.75, 0.3, 0.8 * a))
