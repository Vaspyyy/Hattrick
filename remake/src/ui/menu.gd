# Title screen, level select and pause screen: a list of choices driven by the keyboard,
# controller or mouse. The game (main.gd) feeds it input and gets the chosen id back.
extends CanvasLayer

const Fonts := preload("res://src/ui/fonts.gd")

signal chosen(id: String)

var root: Control
var dim: ColorRect
var logo: Label
var logo_sub: Label
var title_l: Label
var hint: Label
var list: VBoxContainer
var items: Array = []     # [id, text, enabled, note]
var sel := 0
var page := ""
var t := 0.0
var scroll := 0
const ROWS := 9


func _ready() -> void:
	layer = 10
	root = Control.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(root)
	dim = ColorRect.new()
	dim.set_anchors_preset(Control.PRESET_FULL_RECT)
	dim.color = Color(0.05, 0.05, 0.1, 0.55)
	dim.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(dim)
	logo = _label(170, root)
	_place(logo, 0.5, 0, -700, 70, 700, 270)
	logo.text = "HATTRICK"
	logo.add_theme_color_override("font_color", Color(1.0, 0.55, 0.15))
	logo.add_theme_constant_override("outline_size", 38)
	logo_sub = _label(44, root)
	_place(logo_sub, 0.5, 0, -700, 262, 700, 322)
	logo_sub.text = "a cap-throwing platformer"
	logo_sub.add_theme_color_override("font_color", Color(1.0, 0.95, 0.85))
	title_l = _label(80, root)
	_place(title_l, 0.5, 0, -700, 110, 700, 220)
	list = VBoxContainer.new()
	_place(list, 0.5, 0.5, -600, -60, 600, 440)
	list.add_theme_constant_override("separation", 6)
	root.add_child(list)
	hint = _label(30, root)
	_place(hint, 0.5, 1, -700, -80, 700, -30)
	hint.add_theme_color_override("font_color", Color(0.85, 0.88, 1.0, 0.85))
	hide_menu()


static func _place(c: Control, ax: float, ay: float, l: float, t: float, r: float, b: float) -> void:
	c.anchor_left = ax
	c.anchor_right = ax
	c.anchor_top = ay
	c.anchor_bottom = ay
	c.offset_left = l
	c.offset_top = t
	c.offset_right = r
	c.offset_bottom = b


func _label(size: int, parent: Control) -> Label:
	var l := Label.new()
	l.add_theme_font_override("font", Fonts.bold())
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_color", Color.WHITE)
	l.add_theme_color_override("font_outline_color", Color(0.08, 0.07, 0.12))
	l.add_theme_constant_override("outline_size", maxi(8, size / 5))
	l.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	l.mouse_filter = Control.MOUSE_FILTER_IGNORE
	parent.add_child(l)
	return l


func open(p: String, title: String, entries: Array, at := 0) -> void:
	page = p
	items = entries
	sel = clampi(at, 0, maxi(entries.size() - 1, 0))
	while sel < items.size() - 1 and not items[sel][2]:
		sel += 1
	scroll = 0
	var is_title := p == "title"
	logo.visible = is_title
	logo_sub.visible = is_title
	title_l.visible = not is_title
	title_l.text = title
	dim.color = Color(0.05, 0.05, 0.1, 0.15 if is_title else 0.6)
	_place(list, 0.5, 0.5, -600, 40 if is_title else -170, 600, 440)
	hint.text = "Jump or Enter: choose      Esc or cap: back" if p != "title" else "Arrows move · Z jump · X / C cap · Esc pause"
	root.visible = true
	_rebuild()


func hide_menu() -> void:
	root.visible = false
	page = ""


func is_open() -> bool:
	return root.visible


func _rebuild() -> void:
	for c in list.get_children():
		c.queue_free()
	if sel < scroll: scroll = sel
	if sel >= scroll + ROWS: scroll = sel - ROWS + 1
	for i in range(scroll, mini(items.size(), scroll + ROWS)):
		var it: Array = items[i]
		var l := Label.new()
		l.add_theme_font_override("font", Fonts.bold())
		var on := i == sel
		l.add_theme_font_size_override("font_size", 58 if on else 48)
		var c := Color(1.0, 0.86, 0.3) if on else (Color.WHITE if it[2] else Color(0.6, 0.62, 0.7))
		l.add_theme_color_override("font_color", c)
		l.add_theme_color_override("font_outline_color", Color(0.08, 0.07, 0.12))
		l.add_theme_constant_override("outline_size", 12)
		l.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		var txt: String = it[1]
		if it.size() > 3 and it[3] != "":
			txt += "   " + it[3]
		l.text = ("▸  " + txt + "  ◂") if on else txt
		l.mouse_filter = Control.MOUSE_FILTER_STOP
		var idx := i
		l.gui_input.connect(func(ev: InputEvent) -> void:
			if ev is InputEventMouseButton and ev.pressed and ev.button_index == MOUSE_BUTTON_LEFT:
				sel = idx
				_choose()
			elif ev is InputEventMouseMotion and sel != idx and items[idx][2]:
				sel = idx
				_rebuild())
		list.add_child(l)


func move(d: int) -> void:
	if items.is_empty():
		return
	var n := sel
	for i in items.size():
		n = (n + d + items.size()) % items.size()
		if items[n][2]:
			break
	sel = n
	_rebuild()


func _choose() -> void:
	if sel < items.size() and items[sel][2]:
		chosen.emit(items[sel][0])


func back() -> void:
	chosen.emit("back")


# Returns true when the menu used the event.
func handle(ev: InputEvent) -> bool:
	if not root.visible:
		return false
	if ev.is_echo() and not (ev is InputEventKey and (ev.keycode == KEY_UP or ev.keycode == KEY_DOWN)):
		return false
	if ev is InputEventKey and ev.pressed:
		match ev.physical_keycode:
			KEY_UP: move(-1); return true
			KEY_DOWN: move(1); return true
			KEY_LEFT: move(-ROWS); return true
			KEY_RIGHT: move(ROWS); return true
			KEY_ENTER, KEY_KP_ENTER, KEY_Z, KEY_SPACE, KEY_Y: _choose(); return true
			KEY_ESCAPE, KEY_X, KEY_C: back(); return true
	if ev is InputEventJoypadButton and ev.pressed:
		match ev.button_index:
			JOY_BUTTON_DPAD_UP: move(-1); return true
			JOY_BUTTON_DPAD_DOWN: move(1); return true
			JOY_BUTTON_A, JOY_BUTTON_START: _choose(); return true
			JOY_BUTTON_B, JOY_BUTTON_X, JOY_BUTTON_Y: back(); return true
	if ev is InputEventJoypadMotion and ev.axis == JOY_AXIS_LEFT_Y and absf(ev.axis_value) > 0.6:
		if t > 0.18:
			move(1 if ev.axis_value > 0 else -1)
			t = 0.0
		return true
	return false


func _process(dt: float) -> void:
	t += dt
	if logo.visible:
		logo.pivot_offset = logo.size * 0.5
		logo.rotation = sin(t * 1.3) * 0.025
		logo.scale = Vector2.ONE * (1.0 + sin(t * 2.6) * 0.02)
