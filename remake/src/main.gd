# The game: runs the sim at 60 steps a second (the C game's logic, ported) and draws it in 3D,
# interpolating between steps. Title screen, level select, pause, the level flow and progress.
#
# Command line (after --):
#   --level N             start straight in level N (0 = the first)
#   --replay FILE.tas     drive Hatrick from a recorded route (tas/, the C simulator's format)
#   --shots A,B,...       with --replay: save a screenshot after these steps
#   --out DIR             where the screenshots go
#   --frames N            with --replay: quit after N steps
#   --skip N              with --replay: run the first N steps without drawing them (fast)
#   --assets DIR          the levels, music and sounds (default: ../assets)
#   --snap SECONDS        save a screenshot (to --out) after this long and quit
#   --boss N              start at boss arena N's entrance (like tools/xtrace.gd's SIM_BOSS)
#   --at ROOM,X,Y         start standing on that cell (tiles; like xtrace.gd's SIM_AT)
extends Node

const Levels := preload("res://src/sim/levels.gd")
const Sim := preload("res://src/sim/sim.gd")
const LevelView := preload("res://src/view/level_view.gd")
const Backdrop := preload("res://src/view/backdrop.gd")
const Hatrick := preload("res://src/view/hatrick.gd")
const Actors := preload("res://src/view/actors.gd")
const Hazards := preload("res://src/view/hazards.gd")
const BossView := preload("res://src/view/boss_view.gd")
const Fx := preload("res://src/view/fx.gd")
const CameraRig := preload("res://src/view/camera_rig.gd")
const Lighting := preload("res://src/view/lighting.gd")
const Hud := preload("res://src/ui/hud.gd")
const Menu := preload("res://src/ui/menu.gd")
const Audio := preload("res://src/audio/audio.gd")
const Pad := preload("res://src/game/pad.gd")
const Fonts := preload("res://src/ui/fonts.gd")

const PROGRESS := "user://progress.json"

var assets := ""
var lv
var sim
var world: Node3D
var lights
var level
var backdrop
var hero
var actors
var hazards
var bosses
var fx
var cam
var hud
var menu
var audio
var pad
var blob: Decal
var mode := "title"        # title, play, pause
var prev_hero := Vector2.ZERO
var prev_cam := Vector2.ZERO
var built_room := -1
var progress := {"cleared": [], "moons": {}, "cards": [], "secrets": []}
var title_t := 0.0
# a recorded route driving the game (tests and screenshots)
var replay := PackedInt32Array()
var shots := {}
var out_dir := "/tmp"
var frames := -1
var skip := 0
var steps := 0
var start_level_arg := -1
var snap := -1.0
var boss_arg := -1
var at_arg := ""


func _ready() -> void:
	_args()
	if assets == "":
		assets = ProjectSettings.globalize_path("res://").path_join("../assets").simplify_path()
	lv = Levels.new()
	if not lv.load_dir(assets) or lv.levels.is_empty():
		_fatal("Couldn't read the levels in %s" % assets)
		return
	for e in lv.problems:   # once each, the way the C game reports them
		printerr("hatrick: ", e)
	sim = Sim.new()
	sim.setup(lv.levels, lv.ncampaign, assets)
	_load_progress()
	world = Node3D.new()
	world.name = "World"
	add_child(world)
	lights = Lighting.new()
	world.add_child(lights)
	backdrop = Backdrop.new()
	world.add_child(backdrop)
	level = LevelView.new()
	world.add_child(level)
	hero = Hatrick.new()
	world.add_child(hero)
	blob = _blob_shadow()
	world.add_child(blob)
	actors = Actors.new()
	world.add_child(actors)
	hazards = Hazards.new()
	world.add_child(hazards)
	bosses = BossView.new()
	world.add_child(bosses)
	fx = Fx.new()
	world.add_child(fx)
	cam = CameraRig.new()
	world.add_child(cam)
	cam.current = true
	hud = Hud.new()
	add_child(hud)
	menu = Menu.new()
	add_child(menu)
	menu.chosen.connect(_on_menu)
	audio = Audio.new()
	add_child(audio)
	audio.setup(assets)
	pad = Pad.new()
	if (not shots.is_empty() or snap >= 0.0) and DisplayServer.get_name() == "headless":
		push_warning("--headless draws nothing, so no screenshots will be saved (xvfb-run -a gives Godot a display)")
	if not replay.is_empty():
		start_level(maxi(start_level_arg, 0))
		if boss_arg >= 0 and boss_arg < sim.bz.size():
			var b = sim.bz[boss_arg]
			sim.spawn(b.room, ((b.ax0 + 4) * 8) << 8, ((b.my - 3) * 8) << 8)
			sim.ev.clear()
			_rebuild()
		if at_arg != "":
			var p := at_arg.split(",")
			sim.spawn(p[0].to_int(), (p[1].to_int() * 8 + 1) << 8, (p[2].to_int() * 8 - 3) << 8)
			sim.ev.clear()
			_rebuild()
		mode = "play"
	elif start_level_arg >= 0:
		start_level(start_level_arg)
		mode = "play"
	else:
		_title()


func _args() -> void:
	var a := OS.get_cmdline_user_args()
	var i := 0
	while i < a.size():
		var v: String = a[i + 1] if i + 1 < a.size() else ""
		match a[i]:
			"--level": start_level_arg = v.to_int(); i += 1
			"--replay": replay = _read_tas(v); i += 1
			"--shots":
				for s in v.split(","): shots[s.to_int()] = true
				i += 1
			"--out": out_dir = v; i += 1
			"--frames": frames = v.to_int(); i += 1
			"--skip": skip = v.to_int(); i += 1
			"--assets": assets = v; i += 1
			"--snap": snap = v.to_float(); i += 1
			"--boss": boss_arg = v.to_int(); i += 1
			"--at": at_arg = v; i += 1
		i += 1


static func _read_tas(path: String) -> PackedInt32Array:
	var out := PackedInt32Array()
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		push_error("can't read %s" % path)
		return out
	for line in f.get_as_text().split("\n"):
		var p := line.strip_edges().split(" ", false)
		if p.size() < 2:
			continue
		var k := 0
		for c in p[1]:
			match c:
				"L": k |= 1
				"R": k |= 2
				"U": k |= 4
				"D": k |= 8
				"J": k |= 16
				"C": k |= 32
				"V": k |= Sim.CAP2
		for j in p[0].to_int():
			out.append(k)
	return out


# A soft round shadow straight under Hatrick, so jumps read against the ground and blocks.
static func _blob_shadow() -> Decal:
	var d := Decal.new()
	d.size = Vector3(0.95, 12.0, 0.95)
	var g := Gradient.new()
	g.set_color(0, Color(0.05, 0.05, 0.12, 0.6))
	g.set_color(1, Color(0.05, 0.05, 0.12, 0.0))
	var t := GradientTexture2D.new()
	t.gradient = g
	t.fill = GradientTexture2D.FILL_RADIAL
	t.fill_from = Vector2(0.5, 0.5)
	t.fill_to = Vector2(1.0, 0.5)
	t.width = 64
	t.height = 64
	d.texture_albedo = t
	d.upper_fade = 0.05
	d.lower_fade = 0.6
	return d


func _fatal(msg: String) -> void:
	push_error(msg)
	var l := Label.new()
	l.text = msg
	l.add_theme_font_size_override("font_size", 32)
	l.position = Vector2(40, 40)
	add_child(l)


# ---------- progress ----------
func _load_progress() -> void:
	if FileAccess.file_exists(PROGRESS):
		var d = JSON.parse_string(FileAccess.get_file_as_string(PROGRESS))
		if d is Dictionary:
			progress.merge(d, true)


func _save_progress() -> void:
	if not replay.is_empty():
		return
	var f := FileAccess.open(PROGRESS, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(progress))


func _moon_home(name: String) -> int:
	return int(progress.moons.get(name, 0))


func _cleared(name: String) -> bool:
	return name in progress.cleared


# ---------- flow ----------
func start_level(l: int) -> void:
	sim.cardgot = lv.levels[l].name in progress.cards
	secret_exit = false
	sim.load_level(l)
	sim.ev.clear()
	_rebuild()
	var nm: String = sim.L.name
	var sub := ""
	if l < sim.NLV: sub = "WORLD %d" % (l + 1)
	elif sim.L.lab == 2: sub = "SECRET"
	elif sim.L.lab == 1: sub = "LAB"
	hud.show_card(nm, sub)
	audio.moon_home = _moon_home(nm)
	hud.moon_home = audio.moon_home


func _rebuild() -> void:
	level.moon_home = _moon_home(sim.L.name)
	level.build(sim)
	backdrop.build(level, "%s/%d" % [sim.L.name, sim.room])
	actors.build(sim)
	hazards.build(sim)
	bosses.build(sim)
	fx.clear()
	fx.lh = sim.lh
	fx.level = level
	var night: bool = sim.L.gimroom[sim.room] == Sim.GR_NIGHT if sim.room < sim.L.gimroom.size() else false
	lights.set_mode("night" if night and level.cave == 0 else ["sky", "cave", "dark"][clampi(level.cave, 0, 2)])
	built_room = sim.room
	sim.dirty_all = false
	sim.dirty.clear()
	_snap()


func _snap() -> void:   # no interpolation across a cut
	prev_hero = Vector2(sim.hx, sim.hy) / 256.0
	prev_cam = Vector2(sim.cxf, sim.cyf) / 256.0
	actors.snapshot()
	bosses.snapshot()


func _title() -> void:
	mode = "title"
	start_level(_next_level())
	for i in 24:   # let Hatrick land
		sim.tick(0)
		_after_tick(true)
	hud.card_t = 99.0
	hud.set_visible_hud(false)
	menu.open("title", "", [["play", "PLAY", true, ""], ["levels", "LEVEL SELECT", true, ""], ["quit", "QUIT", true, ""]])


func _next_level() -> int:
	for i in sim.NLV:
		if not _cleared(lv.levels[i].name):
			return i
	return 0


func _level_select() -> void:
	var entries := []
	var open := true
	for i in sim.NLV:
		var L: Dictionary = lv.levels[i]
		var bits := _moon_home(L.name)
		var got := 0
		for j in 3:
			got += (bits >> j) & 1
		var note := ""
		if _cleared(L.name): note = "★"
		if L.moons.size(): note += "  ☾ %d/%d" % [got, L.moons.size()]
		if L.cl.sg and L.name in progress.secrets: note += "  ⚑"
		if L.cl.pc and L.name in progress.cards: note += "  ✉"
		entries.append(["level:%d" % i, "%d  %s" % [i + 1, L.name.to_upper()], open, note])
		open = open and _cleared(L.name)
	for i in range(sim.NLV, lv.levels.size()):   # labs, and secret levels once their exit is found
		var L: Dictionary = lv.levels[i]
		if L.lab == 2:
			var p := _cl_parent(i)
			if p < 0 or not (lv.levels[p].name in progress.secrets):
				continue
		var tag := "SECRET" if L.lab == 2 else "LAB"
		entries.append(["level:%d" % i, "%s  %s" % [tag, L.name.to_upper()], true, "★" if _cleared(L.name) else ""])
	menu.open("levels", "LEVEL SELECT", entries, _next_level())


func _cl_parent(l: int) -> int:   # collect_ui.h: the level whose secret exit leads to secret level l
	var p: String = lv.levels[l].cl.secretof
	if p.length() and p[0] >= "1" and p[0] <= "9":
		var i := p.to_int() - 1
		return i if i < sim.NLV else -1
	for i in sim.NLV:
		if lv.levels[i].name.to_lower() == p.to_lower():
			return i
	return -1


func _pause() -> void:
	mode = "pause"
	audio.play("pause")
	menu.open("pause", "PAUSED", [["continue", "CONTINUE", true, ""], ["restart", "RESTART LEVEL", true, ""], ["title", "EXIT TO TITLE", true, ""]])


func _on_menu(id: String) -> void:
	audio.play("menu_ok" if id != "back" else "menu_back")
	match id:
		"play":
			menu.hide_menu()
			hud.set_visible_hud(true)
			start_level(_next_level())
			mode = "play"
		"levels":
			_level_select()
		"quit":
			get_tree().quit()
		"continue":
			menu.hide_menu()
			mode = "play"
		"restart":
			menu.hide_menu()
			start_level(sim.lvl)
			mode = "play"
		"title":
			menu.hide_menu()
			_title()
		"back":
			if menu.page == "pause":
				menu.hide_menu()
				mode = "play"
			elif menu.page == "levels":
				menu.open("title", "", [["play", "PLAY", true, ""], ["levels", "LEVEL SELECT", true, ""], ["quit", "QUIT", true, ""]], 1)
		_:
			if id.begins_with("level:"):
				menu.hide_menu()
				hud.set_visible_hud(true)
				start_level(id.substr(6).to_int())
				mode = "play"


var secret_exit := false


func _secret_next() -> bool:   # a secret exit leads on to its secret level
	for i in range(sim.NLV, lv.levels.size()):
		if lv.levels[i].lab == 2 and _cl_parent(i) == sim.lvl:
			start_level(i)
			return true
	return false


func _finished() -> void:
	var nm: String = sim.L.name
	if not _cleared(nm):
		progress.cleared.append(nm)
	_save_progress()
	if not replay.is_empty():
		return
	if sim.lvl + 1 < sim.NLV and (not secret_exit or not _secret_next()):
		start_level(sim.lvl + 1)
	else:
		_title()


func _unhandled_input(ev: InputEvent) -> void:
	if menu.handle(ev):
		get_viewport().set_input_as_handled()
		return
	if ev is InputEventKey and ev.pressed and not ev.echo:
		match ev.physical_keycode:
			KEY_ESCAPE:
				if mode == "play": _pause()
			KEY_M:
				audio.muted = not audio.muted
			KEY_F11:
				var w := get_window()
				w.mode = Window.MODE_WINDOWED if w.mode == Window.MODE_FULLSCREEN else Window.MODE_FULLSCREEN
			KEY_EQUAL, KEY_KP_ADD:
				audio.volume = minf(audio.volume + 0.1, 1.0)
			KEY_MINUS, KEY_KP_SUBTRACT:
				audio.volume = maxf(audio.volume - 0.1, 0.0)
	if ev is InputEventJoypadButton and ev.pressed and ev.button_index == JOY_BUTTON_START and mode == "play":
		_pause()


# ---------- the loop ----------
func _physics_process(_dt: float) -> void:
	if sim == null or not replay.is_empty():
		return
	_step()


func _step() -> void:
	if mode != "play":
		audio.director(sim, [], mode, 1.0 / 60.0)
		return
	actors.snapshot()
	bosses.snapshot()
	prev_hero = Vector2(sim.hx, sim.hy) / 256.0
	prev_cam = Vector2(sim.cxf, sim.cyf) / 256.0
	var k := 0
	if not replay.is_empty():
		k = replay[steps] if steps < replay.size() else 0
	else:
		k = pad.keys()
		if Input.is_physical_key_pressed(KEY_R):
			k |= Sim.K_RESTART
	steps += 1
	sim.tick(k)
	_after_tick(false)


func _after_tick(quiet: bool) -> void:
	var evs: Array = sim.ev.duplicate()
	sim.ev.clear()
	var cut := false
	for e in evs:
		match e[0]:
			"room", "cut", "spawn":
				cut = true
			"flag":
				var nm: String = sim.L.name
				progress.moons[nm] = _moon_home(nm) | sim.wd.moongot
				secret_exit = sim.clsecretnow != 0
				if secret_exit and sim.cl_record(sim.lvl) and not (nm in progress.secrets):
					progress.secrets.append(nm)
					hud.show_card("SECRET EXIT FOUND", "")
			"postcard":
				var nm: String = sim.L.name
				if not (nm in progress.cards):
					progress.cards.append(nm)
					_save_progress()
				hud.show_card("POSTCARD!", nm.to_upper())
			"bossstart":
				var b = sim.bz[e[1]]
				hud.show_card(Sim.BOSSTITLE[b.kind] if b.kind else Sim.MINITITLE[b.mini], "BOSS" if b.kind else "MINI-BOSS")
			"bosshurt", "bosswin":
				cam.punch = 1.0
			"pound":
				cam.punch = 1.0
		level.on_event(e)
		actors.on_event(e)
		hazards.on_event(e)
		bosses.on_event(e)
		if not quiet:
			fx.on_event(e)
		hero.on_event(e, sim)
	if sim.dirty_all or sim.room != built_room:
		_rebuild()
		cut = true
	else:
		level.apply()
	if cut:
		_snap()
	if not quiet:
		audio.director(sim, evs, mode, 1.0 / 60.0)
		if sim.rumq:
			pad.rumble(sim.rumq)
	sim.rumq = 0
	if sim.finished and mode == "play":
		_finished()


func _process(dt: float) -> void:
	if sim == null:
		return
	var alpha := Engine.get_physics_interpolation_fraction()
	if not replay.is_empty():
		# one step per drawn frame; --skip runs the first steps (and the views) undrawn
		while steps < skip - 1 and mode == "play":
			_step()
			_frame(1.0, 1.0 / 60.0)
		_step()
		alpha = 1.0
		dt = 1.0 / 60.0
	_frame(alpha, dt)
	if snap >= 0.0:
		snap -= dt
		if snap < 0.0:
			await RenderingServer.frame_post_draw
			DirAccess.make_dir_recursive_absolute(out_dir)
			get_viewport().get_texture().get_image().save_png(out_dir.path_join("snap.png"))
			get_tree().quit()
	if not replay.is_empty():
		if shots.has(steps):
			await RenderingServer.frame_post_draw
			var img := get_viewport().get_texture().get_image()
			DirAccess.make_dir_recursive_absolute(out_dir)
			img.save_png(out_dir.path_join("shot_%05d.png" % steps))
		if (frames > 0 and steps >= frames) or steps >= replay.size() + 120:
			print("replay done at step %d: st=%d coins=%d score=%d" % [steps, sim.st, sim.coins, sim.score])
			get_tree().quit()


# Moves every view to the sim's current state (alpha: how far between the last two steps).
func _frame(alpha: float, dt: float) -> void:
	actors.hop = audio.hop()
	var hc := Vector2(sim.hx, sim.hy) / 256.0
	var hp := prev_hero.lerp(hc, alpha)
	var lh: int = sim.lh
	hero.animate(sim, Vector3((hp.x + 3.0) / 8.0, lh - (hp.y + 11.0) / 8.0, 0.0), alpha, dt)
	var cc := prev_cam.lerp(Vector2(sim.cxf, sim.cyf) / 256.0, alpha)
	var center := Vector3((cc.x + Sim.W / 2.0) / 8.0, lh - (cc.y + Sim.H / 2.0) / 8.0, 0.0)
	# The view is a little closer than the C game's (17 tiles tall instead of 18) and keeps its
	# bottom edge, so the front of the ground shows; it gives up a little of the top, where the
	# C camera band never lets Hatrick go.
	center.y -= (18.0 - cam.VIEW_H) * 0.5 + 0.1
	if mode == "title":
		title_t += dt
		center.x = hero.position.x + 8.0 + sin(title_t * 0.3) * 0.5
	cam.follow(center, sim.shake, dt)
	level.update(dt)
	actors.update(alpha, dt)
	hazards.update(alpha, dt)
	bosses.update(alpha, dt)
	fx.update(dt)
	backdrop.update(dt)
	lights.lantern.position = hero.position + Vector3(0, 1.4, 2.2)
	blob.position = hero.position + Vector3(0, 0.08 - blob.size.y * 0.5, 0)
	blob.visible = sim.st != Sim.TUBE and sim.st != Sim.DEAD
	hud.update(sim, dt, cam.unproject_position(hero.global_position + Vector3(0, 0.7, 0)))
