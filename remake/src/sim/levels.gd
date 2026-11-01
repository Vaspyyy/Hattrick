# Reads assets/levels.txt (and assets/worlds.txt after it), the same files the C game reads, the
# same way: hatrick.c parselevels() with the options of gimmicks.h, boss.c, worlds.c and collect.h.
# The format is described in MODDING.md. Each level becomes a Dictionary:
#   name, music, time, card (index into CARDNAME), lab (0 campaign, 1 lab, 2 secret), boss, mini,
#   theme (index into THEMENAME), avalanche, before, gim (GO_* options), gimroom (GR_* per area),
#   cl: { star, sg, gr, gx, gy, pc, pr, px, py, secretof }
#   rooms: Array of { w, h, cave, grid: PackedByteArray (w*h map characters, 0 = empty) }
#   tubes: Array of { room, x, y, dir, id, link }
#   bars:  Array of { room, x, y, len, speed, a0 }   fire bars (speed +-1: beat bars)
#   homes: Array of { room, tube, spit }             tube dwellers
#   moons: Array of { room, x, y }
# Mistakes are collected in `problems` (file and line, as the C game prints them) and the object
# (or level) is dropped, like the C game.
extends RefCounted

const MH := 320
const MW := 2560
const MINH := 32
const NROOM := 4
const MAXTUBE := 128
const MAXBAR := 240
const MAXHOME := 240
const MAXEN := 480
const MAXCR := 960
const MAXCK := 64
enum { T_UP, T_DOWN, T_LEFT, T_RIGHT }
# every map character the C game knows, besides space
const KNOWN := "#B^STo/\\|-M?C*%~:!0123456789@FKgbhnm(GPNJHXRUI<>=wcsqjf[]ukvLxpQYZ$&OAV_{}DEW"
const CARDNAME := ["hills", "bricks", "spikes", "sky", "castle", "playground", "beach", "peak", "woods", "clock"]
const THEMENAME := ["none", "beach", "frost", "crayon", "clock", "desert"]
const BOSSKIND := ["", "haberdasher", "bigwalker", "cloudking", "matriarch"]
const MINIKIND := ["", "walker", "buzzer"]
enum { GO_RISE, GO_RISEV, GO_SCROLL, GO_NIGHT, GO_FLIP }
enum { GR_NONE, GR_8BIT, GR_NIGHT }
enum { GF_GRAVITY, GF_SPEED, GF_SWAP }

var levels: Array = []      # campaign first, then labs, then secret levels
var ncampaign := 0
var nsec := 0               # secret levels: the last ones
var play := -1              # the movement playground's index (-1: none)
var problems: PackedStringArray = []
var _split := 0             # lines of levels.txt before worlds.txt starts (0: none)


func load_dir(assets: String) -> bool:
	var f := FileAccess.open(assets.path_join("levels.txt"), FileAccess.READ)
	if f == null:
		problems.append("cannot open %s" % assets.path_join("levels.txt"))
		return false
	var text := f.get_as_text()
	var w := FileAccess.open(assets.path_join("worlds.txt"), FileAccess.READ)
	_split = 0
	if w != null:
		if text != "" and not text.ends_with("\n"):
			text += "\n"
		_split = text.count("\n")
		text += w.get_as_text()
	return parse(text)


func _err(line: int, msg: String) -> void:
	var file := "levels.txt"
	if _split and line > _split:
		file = "worlds.txt"
		line -= _split
	problems.append("%s:%d: %s" % [file, line, msg])


static func _char(s: String, i: int) -> int:
	return s.unicode_at(i) if i < s.length() else 0


# ---------- copy=<level> (gimmicks.h gim_expand) ----------
# A header's level name as copy= names it: no number, no options, lower case, _ as space.
static func _gim_name(h: String) -> String:
	var out := ""
	for t in h.replace("\t", " ").replace("\r", " ").split(" ", false):
		if t.find("=") < 0:
			out = t if out == "" else out + " " + t
	var p := 0
	while p < out.length() and (out[p] == " " or (out[p] >= "0" and out[p] <= "9")):
		p += 1
	return out.substr(p).to_lower().replace("_", " ")


# The level's header is followed by every row and bonus room of the named level, then by anything
# of its own (more bonus rooms). Returns the lines and, for each, its line number in the file.
func _expand(ln: PackedStringArray) -> Array:
	var out := PackedStringArray()
	var src := PackedInt32Array()
	var has := "\n".join(ln).find("copy=") >= 0
	for i in ln.size():
		out.append(ln[i])
		src.append(i + 1)
		if not has or not ln[i].begins_with("="):
			continue
		var cp := ln[i].find("copy=")
		if cp < 0 or (cp > 0 and ln[i][cp - 1] != " " and ln[i][cp - 1] != "\t"):
			continue
		var want := ""
		var k := cp + 5
		while k < ln[i].length() and ln[i][k] != " " and ln[i][k] != "\t" and ln[i][k] != "\r" and want.length() < 63:
			want += ln[i][k]
			k += 1
		want = _gim_name(want)
		var from := -1
		for j in ln.size():
			if j != i and ln[j].begins_with("=") and ln[j].find("copy=") < 0 and _gim_name(ln[j].substr(1)) == want:
				from = j
				break
		if from < 0:
			_err(i + 1, "copy=%s: no level of that name" % want)
			continue
		var j := from + 1
		while j < ln.size() and not ln[j].begins_with("="):
			out.append(ln[j])
			src.append(j + 1)
			j += 1
	return [out, src]


# ---------- the whole file ----------
func parse(text: String) -> bool:
	var ex := _expand(text.split("\n"))
	var ln := PackedStringArray()
	var srcline: PackedInt32Array = ex[1]   # copy= lines name the line they were copied from
	for l in ex[0]:
		ln.append(l.rstrip(" \t\r"))
	var out: Array = []
	var h := 0
	while h < ln.size():
		if not ln[h].begins_with("="):
			h += 1
			continue
		var L := {
			"name": "", "music": "overworld", "time": 500, "card": 0, "lab": 0, "boss": 0, "mini": 0,
			"theme": 0, "avalanche": 0, "before": "", "gim": [0, 0, 0, 0, 0], "gimroom": [0, 0, 0, 0],
			"cl": {"star": 0, "sg": 0, "gr": 0, "gx": 0, "gy": 0, "pc": 0, "pr": 0, "px": 0, "py": 0, "secretof": ""},
			"rooms": [], "tubes": [], "bars": [], "homes": [], "moons": [],
		}
		_header(L, -1, ln[h].substr(1), srcline[h])
		var end := h + 1
		while end < ln.size() and not ln[end].begins_with("="):
			end += 1
		var ok := true
		var rowline: Array = []   # per area: the source line of each grid row
		var s := h
		while s < end and ok:
			var nxt := s + 1
			while nxt < end and not ln[nxt].begins_with("+"):
				nxt += 1
			var r: int = L.rooms.size()
			if r == NROOM:
				_err(srcline[s], "level \"%s\" has more than %d bonus rooms; the rest are ignored" % [L.name, NROOM - 1])
				break
			var R := {"w": 0, "h": 0, "cave": 0, "grid": PackedByteArray()}
			L.rooms.append(R)
			if r > 0:
				R.cave = 1   # rooms default to a cave
				_header(L, r, ln[s].substr(1), srcline[s])
			var rows: Array = []
			var last := -1
			for i in range(s + 1, nxt):
				if not ln[i].begins_with(";") and ln[i] != "":
					last = i
			for i in range(s + 1, last + 1):
				if not ln[i].begins_with(";"):
					rows.append(i)
			ok = _rows(L, r, ln, srcline, rows, srcline[s])
			var rl := PackedInt32Array()
			for y in maxi(R.h, 1):
				var i: int = y - (R.h - rows.size())
				rl.append(srcline[rows[i]] if ok and i >= 0 and i < rows.size() else srcline[s])
			rowline.append(rl)
			if not ok:
				L.rooms.pop_back()
			s = nxt
		var sx := -1
		var fx := -1
		var nen := 0
		if ok:
			for R in L.rooms:
				for c in R.grid:
					if c == 64: sx = 1        # @
					elif c == 70: fx = 1      # F
					elif c == 103 or c == 98 or c == 104 or c == 99: nen += 1   # g b h c
		if ok and sx < 0:
			_err(srcline[h], "level \"%s\" has no start (@), skipped" % L.name); ok = false
		if ok and fx < 0:
			_err(srcline[h], "level \"%s\" has no flag (F), skipped" % L.name); ok = false
		if ok and nen > MAXEN:
			_err(srcline[h], "level \"%s\" has too many enemies (at most %d), skipped" % [L.name, MAXEN]); ok = false
		if ok:
			for r in L.rooms.size():
				_objects(L, r, rowline[r])
			for id in 10:   # each digit links the two mouths that carry it
				var a := -1
				var b := -1
				var more := false
				for i in L.tubes.size():
					if L.tubes[i].id == id:
						if a < 0: a = i
						elif b < 0: b = i
						else: more = true
				if b >= 0:
					L.tubes[a].link = b
					L.tubes[b].link = a
				elif a >= 0:
					_err(srcline[h], "level \"%s\": tube %d has no partner (give a second tube mouth the same digit); it can't be entered" % [L.name, id])
				if more:
					_err(srcline[h], "level \"%s\": more than two tube mouths are numbered %d; only the first two are linked" % [L.name, id])
			for r in L.rooms.size():
				var R: Dictionary = L.rooms[r]
				for y in R.h:
					for x in R.w:
						if R.grid[y * R.w + x] == 40:   # (
							if L.moons.size() < 3:
								L.moons.append({"room": r, "x": x, "y": y})
							else:
								_err(srcline[h], "level \"%s\" has more than 3 moon coins; the rest are ignored" % L.name)
								R.grid[y * R.w + x] = 0
			_cl_parse(L, srcline[h])
			# a boss arena (boss.c: 15 columns left, 16 right of N or J) can't hold a checkpoint
			for r in L.rooms.size():
				var R: Dictionary = L.rooms[r]
				for y in R.h:
					for x in R.w:
						var c: int = R.grid[y * R.w + x]
						if not ((c == 78 and L.boss) or (c == 74 and L.mini)):   # N J
							continue
						for ky in R.h:
							for kx in range(maxi(x - 15, 0), mini(x + 16, R.w - 1) + 1):
								if R.grid[ky * R.w + kx] == 75:   # K
									_err(rowline[r][ky], "level \"%s\": a checkpoint inside a boss arena would trap Hatrick behind its gates; removed" % L.name)
									R.grid[ky * R.w + kx] = 0
			var nck := 0
			var ncr := 0
			for R in L.rooms:
				nck += R.grid.count(75)
				ncr += R.grid.count(67)
			if nck > MAXCK:
				_err(srcline[h], "level \"%s\" has more than %d checkpoints; the rest are ignored" % [L.name, MAXCK])
			if ncr > MAXCR:
				_err(srcline[h], "level \"%s\" has more than %d crumble blocks; the rest stay solid" % [L.name, MAXCR])
		if ok and not L.lab:   # progress is saved by name
			for o in out:
				if not o.lab and o.name == L.name:
					_err(srcline[h], "another level is also named \"%s\"; they share saved progress, so give one another name" % L.name)
					break
		if ok:
			out.append(L)
		h = end
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
	nsec = secrets.size()
	play = levels.size() - nsec - 1 if levels.size() - nsec > ncampaign else -1
	return true


# before=<name>: a level goes in just ahead of the named one (worlds.c worldorder)
func _order(camp: Array) -> Array:
	var target := func(j: int) -> int:
		var t := -1
		if camp[j].before != "":
			for q in camp.size():
				if q != j and camp[q].name == camp[j].before:
					t = q
		return t
	var used := []
	used.resize(camp.size())
	used.fill(false)
	var out: Array = []
	for i in camp.size():
		if target.call(i) >= 0:
			continue   # goes in with its target
		for j in camp.size():
			if not used[j] and target.call(j) == i:
				out.append(camp[j])
				used[j] = true
		if not used[i]:
			out.append(camp[i])
			used[i] = true
	for i in camp.size():   # chains of before= end up last
		if not used[i]:
			out.append(camp[i])
	return out


# A level header "= 1 hills music=overworld card=hills" (r = -1) or a room header "+ cellar bg=sky".
func _header(L: Dictionary, r: int, h: String, line: int) -> void:
	var name := ""
	for w in h.replace("\t", " ").split(" ", false):
		var eq := w.find("=")
		if eq < 0:
			if name.length() + w.length() + 2 < 40:
				name = w if name == "" else name + " " + w
			continue
		var k := w.substr(0, eq)
		var v := w.substr(eq + 1)
		if _gim_opt(L, r, k, v, line):
			continue
		if r >= 0:
			var R: Dictionary = L.rooms[r]
			if k == "bg" and (v == "cave" or v == "sky" or v == "dark"):
				R.cave = 1 if v == "cave" else (2 if v == "dark" else 0)   # dark: the lantern cap
			else:
				_err(line, "unknown room option \"%s=%s\" (use bg=cave or bg=sky)" % [k, v])
		elif k == "boss":
			var i := BOSSKIND.find(v)
			if i > 0: L.boss = i
			else: _err(line, "unknown boss \"%s\" (use haberdasher, bigwalker, cloudking or matriarch)" % v)
		elif k == "mini":
			var i := MINIKIND.find(v)
			if i > 0: L.mini = i
			else: _err(line, "unknown mini-boss \"%s\" (use walker or buzzer)" % v)
		elif k == "music":
			L.music = v.substr(0, 63)
		elif k == "bg" and v == "dark":
			L.dark0 = true   # a dark main area (set on the room once it exists)
		elif k == "time":
			L.time = v.to_int()
			if L.time < 1 or L.time > 9999:
				_err(line, "time needs 1 to 9999 seconds")
				L.time = 500
		elif k == "par":
			_err(line, "par= is no longer used: every level has a timer (time=, 500 s by default)")
		elif k == "card":
			var c := CARDNAME.find(v)
			if c >= 0: L.card = c
			else: _err(line, "unknown card \"%s\" (use hills, bricks, spikes, sky, castle, playground, beach, peak, woods or clock)" % v)
		elif k == "theme":
			var t := THEMENAME.find(v)
			if t >= 0: L.theme = t
			else: _err(line, "unknown theme \"%s\" (use beach, frost, crayon, clock, desert or none)" % v)
		elif k == "avalanche":
			L.avalanche = v.to_int()
			if L.avalanche < 1 or L.avalanche > 200:
				_err(line, "avalanche needs a speed of 1 to 200 px/s")
				L.avalanche = 0
		elif k == "before":
			L.before = v.substr(0, 39).replace("_", " ").to_upper()
		elif k == "star":
			L.cl.star = v.to_int()
			if L.cl.star < 1 or L.cl.star > 9999:
				_err(line, "star needs 1 to 9999 seconds")
				L.cl.star = 0
		elif k == "secret":
			L.cl.secretof = v.substr(0, 39)
		else:
			_err(line, "unknown option \"%s\" (use music=, card= or time=)" % k)
	if r >= 0:
		return
	L.lab = 1 if name.begins_with("lab") else 0
	var p := 0
	while p < name.length() and (name[p] == " " or (name[p] >= "0" and name[p] <= "9")):
		p += 1
	L.name = name.substr(p).substr(0, 39).to_upper()   # "1 hills" -> "HILLS"
	if L.name == "":
		var d := 0
		while d < name.length() and name[d] >= "0" and name[d] <= "9":
			d += 1
		L.name = "LEVEL " + name.substr(0, d)   # "= 3": saved progress needs a name


# gimmicks.h gim_opt: rise=, scroll=, sky=, flip=, copy= and the rooms' bg=8bit / bg=night
func _gim_opt(L: Dictionary, r: int, k: String, v: String, line: int) -> bool:
	if r >= 0:
		if k != "bg":
			return false
		if v == "8bit":
			L.gimroom[r] = GR_8BIT; L.rooms[r].cave = 0
			return true
		if v == "night":
			L.gimroom[r] = GR_NIGHT; L.rooms[r].cave = 0
			return true
		return false
	if k == "rise":
		var c := v.find(":")
		var speed := v.substr(c + 1).to_int() if c >= 0 else 10
		var kind := v.substr(0, c) if c >= 0 else v
		if kind == "lava": L.gim[GO_RISE] = 1
		elif kind == "water": L.gim[GO_RISE] = 2
		else:
			_err(line, "rise= needs lava or water, e.g. rise=lava:12")
			return true
		if speed < 1 or speed > 120:
			_err(line, "rise speed needs 1 to 120 px a second")
			speed = 10
		L.gim[GO_RISEV] = speed
		return true
	if k == "scroll":
		var speed := v.to_int()
		if speed < 1 or speed > 240:
			_err(line, "scroll= needs 1 to 240 px a second")
			speed = 0
		L.gim[GO_SCROLL] = speed
		return true
	if k == "sky":
		if v == "night":
			L.gim[GO_NIGHT] = 1; L.gimroom[0] = GR_NIGHT
		elif v != "day":
			_err(line, "sky= needs day or night")
		return true
	if k == "flip":
		var f := GF_GRAVITY if v == "gravity" else (GF_SPEED if v == "speed" else (GF_SWAP if v == "swap" else -1))
		if f < 0:
			_err(line, "flip= needs gravity, speed or swap")
			f = 0
		L.gim[GO_FLIP] = f
		return true
	return k == "copy"   # done by _expand


# One area's grid from its rows: as tall as its rows (at least MINH), the rows at the bottom.
func _rows(L: Dictionary, r: int, ln: PackedStringArray, srcline: PackedInt32Array, rows: Array, head: int) -> bool:
	var R: Dictionary = L.rooms[r]
	var nr := rows.size()
	if nr > MH:
		_err(head, "%s \"%s\" is taller than %d rows, skipped" % ["bonus room of level" if r else "level", L.name, MH])
		return false
	var w := 0
	for i in rows:
		if ln[i].length() > MW:
			_err(srcline[i], "level \"%s\" is wider than %d columns, skipped" % [L.name, MW])
			return false
		w = maxi(w, ln[i].length())
	R.w = maxi(w, 1)
	R.h = maxi(nr, MINH)
	if r == 0 and L.get("dark0", false):
		R.cave = 2
	var g := PackedByteArray()
	g.resize(R.w * R.h)
	for i in nr:
		var l: String = ln[rows[i]]
		var y: int = R.h - nr + i
		for x in l.length():
			var c := l.unicode_at(x)
			if c != 32 and KNOWN.find(l[x]) < 0:
				_err(srcline[rows[i]], "unknown tile '%s' in column %d, left empty" % [l[x], x + 1])
				c = 32
			if r and (c == 64 or c == 70):
				_err(srcline[rows[i]], "'%s' belongs in the level's main area, not a bonus room; left empty" % l[x])
				c = 32
			g[y * R.w + x] = 0 if c == 32 else c
	R.grid = g
	return true


static func _isarm(c: int) -> bool:
	return c == 126 or c == 58 or c == 33   # ~ : !


# Tubes, fire bars and tube dwellers of one area; mistakes are reported and the object dropped.
func _objects(L: Dictionary, r: int, line: PackedInt32Array) -> void:
	var R: Dictionary = L.rooms[r]
	var W: int = R.w
	var Hh: int = R.h
	var g: PackedByteArray = R.grid
	var seen := PackedByteArray()
	seen.resize(W * Hh)
	var at := func(x: int, y: int) -> int:
		return g[y * W + x] if x >= 0 and x < W and y >= 0 and y < Hh else 0
	for y in Hh:
		for x in W:
			var c: int = g[y * W + x]
			if c == 0 or seen[y * W + x]:
				continue
			if c == 77 or (c >= 48 and c <= 57):   # M or a digit
				var t := {"room": r, "x": x, "y": y, "dir": T_UP, "id": -1 if c == 77 else c - 48, "link": -1}
				var ok := true
				if at.call(x + 1, y) == c and not seen[y * W + x + 1]:
					seen[y * W + x + 1] = 1
					t.dir = T_DOWN if at.call(x, y - 1) == 124 and at.call(x, y + 1) != 124 else T_UP
				elif at.call(x, y + 1) == c:
					seen[(y + 1) * W + x] = 1
					t.dir = T_RIGHT if at.call(x - 1, y) == 45 and at.call(x + 1, y) != 45 else T_LEFT
				else:
					_err(line[y], "tube mouth '%s' in column %d needs a second cell: side by side for a tube opening up or down, stacked for one opening sideways" % [char(c), x + 1])
					ok = false
				if ok:
					if L.tubes.size() < MAXTUBE: L.tubes.append(t)
					else: _err(line[y], "too many tube mouths (at most %d)" % MAXTUBE)
			if c == 42 or c == 37 or c == 36 or c == 38:   # * % $ &
				var DX := [1, 0, -1, 0]
				var DY := [0, 1, 0, -1]
				var best := 0
				var dir := 0
				var kind := 126
				for d in 4:
					var n := 1
					while _isarm(at.call(x + n * DX[d], y + n * DY[d])):
						n += 1
					if n - 1 > best:
						best = n - 1; dir = d; kind = at.call(x + DX[d], y + DY[d])
				var speed := 182 if kind == 58 else (437 if kind == 33 else 273)   # a turn in 6, 4 or 2.5 s
				var b := {"room": r, "x": x, "y": y, "len": best + 1 if best else 5, "speed": speed if c == 42 else -speed, "a0": dir * 64}
				if c == 36 or c == 38:
					b.speed = 1 if c == 36 else -1   # a beat bar (enemies.h)
				if L.bars.size() < MAXBAR: L.bars.append(b)
				else: _err(line[y], "too many fire bars (at most %d)" % MAXBAR)
	for y in Hh:
		for x in W:
			var c: int = g[y * W + x]
			if c != 110 and c != 109:   # n m
				continue
			var home := -1
			for i in L.tubes.size():
				var t: Dictionary = L.tubes[i]
				if t.room == r and (t.x == x or t.x + 1 == x) and ((t.dir == T_UP and t.y == y + 1) or (t.dir == T_DOWN and t.y == y - 1)):
					home = i
			if home < 0:
				_err(line[y], "tube dweller '%s' in column %d must sit right above a tube opening up, or right below one opening down" % [char(c), x + 1])
			elif L.homes.size() < MAXHOME:
				L.homes.append({"room": r, "tube": home, "spit": 1 if c == 109 else 0})
			else:
				_err(line[y], "too many tube dwellers (at most %d)" % MAXHOME)


# collect.h cl_parse: the secret exit and postcard, and whether it is a secret level.
func _cl_parse(L: Dictionary, line: int) -> void:
	var cl: Dictionary = L.cl
	for r in L.rooms.size():
		var R: Dictionary = L.rooms[r]
		for y in R.h:
			for x in R.w:
				var c: int = R.grid[y * R.w + x]
				if c == 71:   # G
					if cl.sg:
						_err(line, "level \"%s\" has more than one secret exit; the rest are ignored" % L.name)
						R.grid[y * R.w + x] = 0
					else:
						cl.sg = 1; cl.gr = r; cl.gx = x; cl.gy = y
				if c == 80:   # P
					if cl.pc:
						_err(line, "level \"%s\" has more than one postcard; the rest are ignored" % L.name)
						R.grid[y * R.w + x] = 0
					else:
						cl.pc = 1; cl.pr = r; cl.px = x; cl.py = y
	if cl.secretof != "":
		L.lab = 2   # off the main path: sorted last, after the labs
	if not cl.star:
		cl.star = L.rooms[0].w * 8 / 55 + 15   # a brisk run, when the level names no time
