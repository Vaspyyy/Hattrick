# Reads assets/levels.txt (and assets/worlds.txt after it), the same files the C game reads.
# The format is described in MODDING.md. Each level becomes a Dictionary:
#   name, music, time, card, theme, lab (0 campaign, 1 lab, 2 secret), before, boss, mini, star
#   rooms: Array of { w, h, cave, grid: PackedByteArray (w*h map characters, 0 = empty) }
#   tubes: Array of { room, x, y, dir, id, link }
#   moons: Array of { room, x, y }
# Mistakes are reported with push_warning and the object (or level) is dropped, like the C game.
extends RefCounted

const MH := 320
const MW := 2560
const MINH := 32
const NROOM := 4
enum { T_UP, T_DOWN, T_LEFT, T_RIGHT }
# every map character the C game knows, besides space
const KNOWN := "#B^STo/\\|-M?C*%~:!0123456789@FKgbhnm(GPNJHXRUI<>=wcsqjf[]ukvLxpQYZ$&OAV_{}DEW"

var levels: Array = []      # campaign first, then labs, then secret levels
var ncampaign := 0
var problems: PackedStringArray = []


func load_dir(assets: String) -> bool:
	var text := ""
	var f := FileAccess.open(assets.path_join("levels.txt"), FileAccess.READ)
	if f == null:
		problems.append("cannot open %s" % assets.path_join("levels.txt"))
		return false
	text = f.get_as_text()
	var w := FileAccess.open(assets.path_join("worlds.txt"), FileAccess.READ)
	if w != null:
		if not text.ends_with("\n"):
			text += "\n"
		text += w.get_as_text()
	return parse(text)


func _err(line: int, msg: String) -> void:
	var s := "levels.txt:%d: %s" % [line, msg]
	problems.append(s)
	push_warning(s)


func parse(text: String) -> bool:
	var ln := text.split("\n")
	for i in ln.size():
		ln[i] = ln[i].rstrip(" \t\r")
	var out: Array = []
	var h := 0
	while h < ln.size():
		if not ln[h].begins_with("="):
			h += 1
			continue
		var L := {
			"name": "", "music": "overworld", "time": 500, "card": "hills", "theme": "",
			"lab": 0, "before": "", "boss": "", "mini": "", "star": 0, "copy": "",
			"rooms": [], "tubes": [], "moons": [],
		}
		_header(L, null, ln[h].substr(1), h + 1)
		var end := h + 1
		while end < ln.size() and not ln[end].begins_with("="):
			end += 1
		var ok := true
		var s := h
		while s < end and ok:
			var nxt := s + 1
			while nxt < end and not ln[nxt].begins_with("+"):
				nxt += 1
			var r: int = L.rooms.size()
			if r == NROOM:
				_err(s + 1, "level \"%s\" has more than %d bonus rooms; the rest are ignored" % [L.name, NROOM - 1])
				break
			var R := {"w": 0, "h": 0, "cave": 0, "grid": PackedByteArray()}
			if r > 0:
				R.cave = 1
				_header(L, R, ln[s].substr(1), s + 1)
			var rows: Array = []
			var last := -1
			for i in range(s + 1, nxt):
				if not ln[i].begins_with(";") and ln[i] != "":
					last = i
			for i in range(s + 1, last + 1):
				if not ln[i].begins_with(";"):
					rows.append(i)
			ok = _rows(L, R, ln, rows, s + 1)
			L.rooms.append(R)
			s = nxt
		if ok:
			var has_start := false
			var has_flag := false
			for c in L.rooms[0].grid:
				if c == 64: has_start = true   # @
				elif c == 70: has_flag = true  # F
			if not has_start:
				_err(h + 1, "level \"%s\" has no start (@), skipped" % L.name)
				ok = false
			elif not has_flag:
				_err(h + 1, "level \"%s\" has no flag (F), skipped" % L.name)
				ok = false
		if ok:
			for r in L.rooms.size():
				_objects(L, r, h + 1)
			for id in 10:
				var a := -1
				var b := -1
				for i in L.tubes.size():
					if L.tubes[i].id == id:
						if a < 0: a = i
						elif b < 0: b = i
				if b >= 0:
					L.tubes[a].link = b
					L.tubes[b].link = a
			for r in L.rooms.size():
				var R: Dictionary = L.rooms[r]
				for y in R.h:
					for x in R.w:
						if R.grid[y * R.w + x] == 40:   # (
							if L.moons.size() < 3:
								L.moons.append({"room": r, "x": x, "y": y})
							else:
								R.grid[y * R.w + x] = 0
			out.append(L)
		h = end
	# copy=<level>: the map of another level (gimmicks.h); only used by a lab level
	for L in out:
		if L.copy != "" and L.rooms.size() == 1 and L.rooms[0].grid.count(64) == 0:
			pass
	var camp: Array = []
	var labs: Array = []
	var secrets: Array = []
	for L in out:
		if L.lab == 0: camp.append(L)
		elif L.lab == 1: labs.append(L)
		else: secrets.append(L)
	if camp.is_empty():
		problems.append("no playable level")
		return false
	camp = _order(camp)
	levels = camp + labs + secrets
	ncampaign = camp.size()
	return true


# before=<name>: a level goes in just ahead of the named one (worlds.c worldorder)
func _order(camp: Array) -> Array:
	var target := func(j: int) -> int:
		if camp[j].before == "":
			return -1
		for q in camp.size():
			if q != j and camp[q].name == camp[j].before:
				return q
		return -1
	var used := []
	used.resize(camp.size())
	used.fill(false)
	var out: Array = []
	for i in camp.size():
		if target.call(i) >= 0:
			continue
		for j in camp.size():
			if not used[j] and target.call(j) == i:
				out.append(camp[j])
				used[j] = true
		if not used[i]:
			out.append(camp[i])
			used[i] = true
	for i in camp.size():
		if not used[i]:
			out.append(camp[i])
	return out


func _header(L: Dictionary, R, h: String, line: int) -> void:
	var name := ""
	for w in h.split(" ", false):
		w = w.strip_edges()
		if w == "":
			continue
		var eq := w.find("=")
		if eq < 0:
			name = w if name == "" else name + " " + w
			continue
		var k := w.substr(0, eq)
		var v := w.substr(eq + 1)
		if R != null:
			if k == "bg":
				R.cave = 1 if v == "cave" else 2 if v == "dark" else 0
			continue
		match k:
			"music": L.music = v
			"time": L.time = clampi(v.to_int(), 1, 9999)
			"card": L.card = v
			"theme": L.theme = v
			"before": L.before = v.to_upper()
			"boss": L.boss = v
			"mini": L.mini = v
			"star": L.star = v.to_int()
			"secret": L.lab = 2
			"copy": L.copy = v
			_: pass   # gimmick options this version doesn't use yet
	if R != null:
		return
	if name.begins_with("lab"):
		L.lab = 1 if L.lab == 0 else L.lab
	var p := 0
	while p < name.length() and (name[p] == " " or (name[p] >= "0" and name[p] <= "9")):
		p += 1
	L.name = name.substr(p).to_upper()
	if L.name == "":
		L.name = "LEVEL " + name.strip_edges()


func _rows(L: Dictionary, R: Dictionary, ln: PackedStringArray, rows: Array, head: int) -> bool:
	var nr := rows.size()
	if nr > MH:
		_err(head, "\"%s\" is taller than %d rows, skipped" % [L.name, MH])
		return false
	var w := 1
	for i in rows:
		w = maxi(w, ln[i].length())
	if w > MW:
		_err(head, "\"%s\" is wider than %d columns, skipped" % [L.name, MW])
		return false
	R.w = w
	R.h = maxi(nr, MINH)
	var g := PackedByteArray()
	g.resize(R.w * R.h)
	var is_room: bool = L.rooms.size() > 0
	for i in nr:
		var l: String = ln[rows[i]]
		var y: int = R.h - nr + i
		for x in l.length():
			var c := l[x]
			if c != " " and KNOWN.find(c) < 0:
				_err(rows[i] + 1, "unknown tile '%s' in column %d, left empty" % [c, x + 1])
				c = " "
			if is_room and (c == "@" or c == "F"):
				c = " "
			g[y * R.w + x] = 0 if c == " " else c.unicode_at(0)
	R.grid = g
	return true


func _objects(L: Dictionary, r: int, line: int) -> void:
	var R: Dictionary = L.rooms[r]
	var W: int = R.w
	var Hh: int = R.h
	var seen := PackedByteArray()
	seen.resize(W * Hh)
	var at := func(x: int, y: int) -> int:
		return R.grid[y * W + x] if x >= 0 and x < W and y >= 0 and y < Hh else 0
	for y in Hh:
		for x in W:
			var c: int = R.grid[y * W + x]
			if c == 0 or seen[y * W + x]:
				continue
			if c == 77 or (c >= 48 and c <= 57):   # M or a digit
				var t := {"room": r, "x": x, "y": y, "dir": T_UP, "id": -1 if c == 77 else c - 48, "link": -1}
				if at.call(x + 1, y) == c and not seen[y * W + x + 1]:
					seen[y * W + x + 1] = 1
					t.dir = T_DOWN if at.call(x, y - 1) == 124 and at.call(x, y + 1) != 124 else T_UP
				elif at.call(x, y + 1) == c:
					seen[(y + 1) * W + x] = 1
					t.dir = T_RIGHT if at.call(x - 1, y) == 45 and at.call(x + 1, y) != 45 else T_LEFT
				else:
					_err(line, "tube mouth '%c' in column %d needs a second cell" % [c, x + 1])
					continue
				L.tubes.append(t)
