# gimmicks.h's game logic (part 2), ported line by line: seesaws, scale lifts, the gold block, hat
# flip flowers (reversed gravity, double speed, bricks <-> coins), doors that turn their room a
# quarter turn, cannons, fans, the rising lava and the auto-scroll. The F2 gallery and the F3 coin
# rush are menus of the C game and are not part of the remake.
extends "res://src/sim/sim_ext.gd"

enum { GO_RISE, GO_RISEV, GO_SCROLL, GO_NIGHT, GO_FLIP }   # L.gim[]
enum { GR_NONE, GR_8BIT, GR_NIGHT }                        # L.gimroom[]
enum { GF_GRAVITY, GF_SPEED, GF_SWAP }
enum { XF_FLIP, XF_ROT }   # upside down; a quarter turn clockwise
const GIM_NP := 32
const GIM_NO := 96
const GIM_NB := 12
const GIM_FLIPTIME := 30 * 60
const MW := 2560
const MH := 320
const NOPLAT := -(1 << 40)

# The level's own tubes, fire bars and moon coins turned to match GW (gim_lvsync). These live
# outside wd on purpose: a death restores wd, and the level data then follows it back.
var gim_lvl := -1
var gim_lvroom := 0
var gim_lvrot := 0
var gim_lvflip := 0
var _gim_told := false


func setup(levels: Array, ncampaign: int, assets_dir := "") -> void:
	super.setup(levels, ncampaign, assets_dir)
	gim_lvl = -1; gim_lvrot = 0; gim_lvflip = 0


# ---------- turning and flipping an area ----------
# Only one area is ever turned (GW.xroom), and only while Hatrick is in it: leaving puts it back.
# The live map, its enemies, crumble blocks, checkpoints and gimmicks turn with it (GW), and so
# do the level's own tubes, fire bars and moon coins (LV), which gim_lvsync keeps in step.
static func gim_cell(op: int, h: int, x: int, y: int) -> Array:
	if op == XF_FLIP:
		return [x, h - 1 - y]
	return [h - 1 - y, x]


static func gim_box(op: int, h: int, x: int, y: int, bw: int, bh: int) -> Array:   # an upright bw x bh px box, 1/256 px
	var H8 := (h * 8) << 8
	if op == XF_FLIP:
		return [x, H8 - y - (bh << 8)]
	var cx := x + (bw << 7)
	var cy := y + (bh << 7)
	return [H8 - cy - (bw << 7), cx - (bh << 7)]


func gim_dims(l: int, r: int, rot: int) -> Array:
	var R: Dictionary = LV[l].rooms[r]
	if rot & 1:
		return [R.h, R.w]
	return [R.w, R.h]


static func gim_state(rot: int, flip: int, op: int) -> Array:   # [rot, flip] after op
	if op == XF_FLIP:
		return [(4 - rot) & 3, flip ^ 1]
	return [(rot + 1) & 3, flip]


func gim_lvop(l: int, r: int, op: int, h: int) -> void:
	var ROT := [T_RIGHT, T_LEFT, T_UP, T_DOWN]   # T_UP, T_DOWN, T_LEFT, T_RIGHT turned clockwise
	var LL: Dictionary = LV[l]
	for t in LL.tubes:
		if t.room != r:
			continue
		var vert: bool = t.dir == T_UP or t.dir == T_DOWN
		var x: int = t.x
		var y: int = t.y
		if op == XF_FLIP:
			if vert:
				t.y = h - 1 - y; t.dir = T_DOWN if t.dir == T_UP else T_UP
			else:
				t.y = h - 2 - y
		else:
			t.x = h - 1 - y if vert else h - 2 - y; t.y = x; t.dir = ROT[t.dir]
	for b in LL.bars:
		if b.room != r:
			continue
		var c := gim_cell(op, h, b.x, b.y)
		b.x = c[0]; b.y = c[1]
		if op == XF_FLIP:
			b.a0 = (256 - b.a0) & 255; b.speed = -b.speed
		else:
			b.a0 = (b.a0 + 64) & 255
	for m in LL.moons:
		if m.room == r:
			var c := gim_cell(op, h, m.x, m.y)
			m.x = c[0]; m.y = c[1]


func gim_lvstep(op: int) -> void:
	var d := gim_dims(gim_lvl, gim_lvroom, gim_lvrot)
	gim_lvop(gim_lvl, gim_lvroom, op, d[1])
	var s := gim_state(gim_lvrot, gim_lvflip, op)
	gim_lvrot = s[0]; gim_lvflip = s[1]


func gim_lvsync() -> void:   # the level's objects follow GW's orientation (and only the current level's)
	var GW: GimWorld = wd.gw
	if gim_lvl >= LV.size():
		gim_lvl = -1; gim_lvrot = 0; gim_lvflip = 0
	var want := GW.rot != 0 or GW.flip != 0
	if gim_lvl >= 0 and (gim_lvrot or gim_lvflip) and (not want or gim_lvl != lvl or gim_lvroom != GW.xroom or gim_lvrot != GW.rot or gim_lvflip != GW.flip):
		while gim_lvrot: gim_lvstep(XF_ROT)
		if gim_lvflip: gim_lvstep(XF_FLIP)
	if not want or gim_lvrot or gim_lvflip:
		return
	gim_lvl = lvl; gim_lvroom = GW.xroom
	if GW.flip: gim_lvstep(XF_FLIP)
	while gim_lvrot != GW.rot: gim_lvstep(XF_ROT)


func gim_wdop(r: int, op: int, w: int, h: int) -> void:   # the live area r, w x h tiles before the change
	var GW: GimWorld = wd.gw
	var nw := h if op == XF_ROT else w
	var nh := w if op == XF_ROT else h
	var mw := maxi(w, nw)
	var mh := maxi(h, nh)
	var stride: int = rs[r]
	var m: PackedByteArray = wd.rm[r]
	var tmp := PackedByteArray()
	tmp.resize(nw * nh)
	for y in h:
		for x in w:
			var t: int = m[y * stride + x]
			var c := gim_cell(op, h, x, y)
			if op == XF_ROT and (t == 10 or t == 11): t ^= 1   # tube bodies: upright <-> sideways
			tmp[c[1] * nw + c[0]] = t
	for y in mh:
		for x in mw:
			m[y * stride + x] = 0
	for y in nh:
		for x in nw:
			m[y * stride + x] = tmp[y * nw + x]
	for e in wd.en:
		if e.r != r:
			continue
		if op == XF_FLIP: e.h = ((h * 8 - 8) << 8) - e.h
		var b := gim_box(op, h, e.x, e.y, 8, 8)
		e.x = b[0]; e.y = b[1]; e.vy = 0
		if op == XF_ROT: e.h = e.y
	for c in wd.cr:
		if c.room != r:
			continue
		var p := gim_cell(op, h, c.x, c.y)
		c.x = p[0]; c.y = p[1]
		if c.state == 1:
			c.state = 2; c.t = 60
	for c in wd.ck:
		if c.room == r:
			var p := gim_cell(op, h, c.x, c.y)
			c.x = p[0]; c.y = p[1]
	for s in wd.sh:
		if s.room == r: s.a = 0
	for o in GW.ob:
		if o.room == r:
			var p := gim_cell(op, h, o.x, o.y)
			o.x = p[0]; o.y = p[1]
	for p in GW.pl:   # they keep level and stop moving
		if p.room != r:
			continue
		var bw: int = p.w if p.kind == 1 else 2 * p.w
		if p.kind == 2: p.x -= p.w << 8
		var b := gim_box(op, h, p.x, p.y, bw, 4)
		p.x = b[0]; p.y = b[1]
		if p.kind == 2: p.x += p.w << 8
		p.vy = 0; p.tilt = 0; p.lo = p.y; p.hi = p.y; p.py = p.y - (16 << 8)
	for b in GW.ball:
		if b.room == r: b.a = 0
	dirty_all = true
	ev.append(["turn", r, op])
	if r != room:
		return
	var hb := gim_box(op, h, hx, hy, 6, 11)
	hx = hb[0]; hy = hb[1]
	if op == XF_FLIP:
		hvy = -hvy
	else:
		var t := hvx
		hvx = -hvy; hvy = t
	if st == HANG or st == CLIMB or st == GPLAND or st == GSPIN or st == GPWIND or st == GPSLAM: st = NORM
	gnd = 0; cst = 0; GW.ride = 0; lw = nw; lh = nh
	if op == XF_FLIP:   # the mirrored view looks the same
		cyf = ((nh * 8 - H) << 8) - cyf; camgy = hy


func gim_op(r: int, op: int) -> void:   # r: the turned area, or one that isn't turned
	var GW: GimWorld = wd.gw
	gim_lvsync()
	var d := gim_dims(lvl, r, GW.rot if r == GW.xroom else 0)
	gim_wdop(r, op, d[0], d[1])
	GW.xroom = r
	var s := gim_state(GW.rot, GW.flip, op)
	GW.rot = s[0]; GW.flip = s[1]
	gim_lvsync()


func gim_unturn() -> void:   # the turned area back the way the file has it
	var GW: GimWorld = wd.gw
	var r := GW.xroom
	while GW.rot: gim_op(r, XF_ROT)
	if GW.flip: gim_op(r, XF_FLIP)


# ---------- building and restarting ----------
func _plat(r: int, kind: int, x: int, y: int, w: int, link: int) -> GimPlat:
	var p := GimPlat.new()
	p.room = r; p.kind = kind; p.x = x; p.y = y; p.w = w; p.link = link
	return p


func gim_build() -> void:   # from build(): wd is fresh, so nothing is turned
	var GW: GimWorld = wd.gw
	gim_lvsync()
	for r in L.rooms.size():
		var R: Dictionary = L.rooms[r]
		for y in R.h:
			for x in R.w:
				var c := _grid(R, x, y)
				if (c == 79 or c == 65 or c == 68 or c == 69 or c == 87) and GW.ob.size() < GIM_NO:   # O A D E W
					var o := GimObj.new()
					o.room = r; o.kind = c; o.x = x; o.y = y; o.t = x * 37 % 140 if c == 69 else 0
					GW.ob.append(o)
				if c == 86 and GW.pl.size() < GIM_NP:   # V: a seesaw, the plank its _ cells
					var a: int = x
					var b: int = x
					while a > 0 and _grid(R, a - 1, y) == 95: a -= 1
					while b + 1 < R.w and _grid(R, b + 1, y) == 95: b += 1
					var cx: int = x * 8 + 4
					var hl := maxi(cx - a * 8, (b + 1) * 8 - cx)
					GW.pl.append(_plat(r, 2, cx << 8, (y * 8 + 2) << 8, hl, -1))
				if c == 123 and (x == 0 or _grid(R, x - 1, y) != 123) and GW.pl.size() + 1 < GIM_NP:   # { and the next } run: a scale lift
					var a: int = x
					while a < R.w and _grid(R, a, y) == 123: a += 1
					var b := a
					while b < R.w and _grid(R, b, y) != 125: b += 1
					if b == R.w:
						continue
					var e := b
					while e < R.w and _grid(R, e, y) == 125: e += 1
					var i := GW.pl.size()
					GW.pl.append(_plat(r, 1, (x * 8) << 8, (y * 8) << 8, (a - x) * 8, i + 1))
					GW.pl.append(_plat(r, 1, (b * 8) << 8, (y * 8) << 8, (e - b) * 8, i))
	for i in GW.pl.size():   # a lift pair's reach: up to below the pulley, down to the ground
		var p: GimPlat = GW.pl[i]
		if p.kind != 1 or p.link < i:
			continue
		var q: GimPlat = GW.pl[p.link]
		var R: Dictionary = L.rooms[p.room]
		var bot := [0, 0]
		for k in 2:
			var s: GimPlat = q if k else p
			var tx0: int = s.x >> 11
			var tx1: int = ((s.x >> 8) + s.w - 1) >> 3
			var ty: int = (s.y >> 11) + 1
			var hit := false
			while ty < R.h and not hit:
				for tx in range(tx0, tx1 + 1):
					if (SOLID >> rmget(p.room, tx, ty)) & 1: hit = true
				ty += 1
			bot[k] = (((ty - 1) * 8 - 4) << 8) if hit else ((R.h * 8 + 32) << 8)
		var py: int = mini(p.y, q.y) - (40 << 8)
		var top := py + (8 << 8)
		if py < 0:
			py = 0; top = 8 << 8
		p.py = py; q.py = py; p.tilt = p.y + q.y
		p.lo = maxi(top, p.tilt - bot[1])
		p.hi = mini(bot[0], p.tilt - top)
	if L.gim[GO_NIGHT]:   # night: the walkers take wing and the bobbing buzzers walk
		for e in wd.en:
			if e.t == 1:
				e.t = 2; e.y -= 20 << 8; e.h = e.y
			elif e.t == 2:
				e.t = 1
	GW.rise = (L.rooms[0].h * 8 + 24) << 8; GW.risedelay = 120
	GW.sx = (startx * 8 - 48) << 8 if startx * 8 > 48 else 0


func gim_restart() -> void:   # from spawn(): the level's start, a checkpoint or a death
	var GW: GimWorld = wd.gw
	gim_lvsync()
	if (GW.rot or GW.flip) and GW.xroom == room:
		var d := gim_dims(lvl, room, GW.rot)
		lw = d[0]; lh = d[1]
	GW.ride = 0; GW.rott = 0; GW.poldy = hy
	if L.gim[GO_RISE] and room == 0:   # some room to breathe after a checkpoint
		var floor_ := hy + (11 << 8) + (40 << 8)
		if GW.rise < floor_: GW.rise = floor_
		GW.risedelay = 90
	if L.gim[GO_SCROLL] and room == 0 and (hx >> 8) < (GW.sx >> 8) + 24:
		GW.sx = hx - (48 << 8) if hx > (48 << 8) else 0


# ---------- every frame ----------
func gim_plattop(p: GimPlat, X: int) -> int:   # the platform's surface under feet X..X+5, 1/256 px, or NOPLAT
	if p.kind == 1:
		var px: int = p.x >> 8
		if X + 6 <= px or X >= px + p.w:
			return NOPLAT
		return p.y
	var cx: int = p.x >> 8
	var fx := X + 3
	if fx < cx - p.w or fx > cx + p.w:
		return NOPLAT
	return p.y + (fx - cx) * p.tilt


# Before objects() and hero(): platforms move and carry Hatrick. Returns true to skip the frame.
func gim_pre(_k: int, _pr: int) -> bool:
	var GW: GimWorld = wd.gw
	if GW.rott:   # a door is turning the room
		GW.rott -= 1
		return true
	if (GW.rot or GW.flip) and GW.xroom != room:
		if GW.flipk == GF_GRAVITY: GW.flipt = 0
		gim_unturn()
	for i in GW.pl.size():
		var p: GimPlat = GW.pl[i]
		if p.room != room:
			continue
		var riding := GW.ride == i + 1
		if p.kind == 1 and p.link > i:   # a scale lift pair, driven from its first half
			var q: GimPlat = GW.pl[p.link]
			var push := 1 if riding else (-1 if GW.ride == p.link + 1 else 0)
			var y0 := p.y
			if push: p.vy += push * 6
			else: p.vy = brake(p.vy, 4)
			p.vy = clampi(p.vy, -200, 200)
			p.y += p.vy
			if p.y < p.lo:
				p.y = p.lo; p.vy = 0
			if p.y > p.hi:
				p.y = p.hi; p.vy = 0
			q.y = p.tilt - p.y   # one rope: their heights add up
			if riding: hy += p.y - y0
			if GW.ride == p.link + 1: hy -= p.y - y0
		elif p.kind == 2:   # a seesaw leans toward the weight on it
			var want := 0
			var t0 := p.tilt
			var fx: int = (hx >> 8) + 3 - (p.x >> 8)
			if riding: want = clampi(fx * 72 / p.w, -72, 72)
			var step := 2 if riding else 1
			p.tilt += step if want > p.tilt + step else (-step if want < p.tilt - step else want - p.tilt)
			if riding: hy += fx * (p.tilt - t0)
	GW.pvy = hvy; GW.poldy = hy
	return false


func gim_flipswap(r: int) -> void:   # bricks become coins and coins bricks (never inside Hatrick)
	var GW: GimWorld = wd.gw
	var d := gim_dims(lvl, r, GW.rot if r == GW.xroom else 0)
	for y in d[1]:
		for x in d[0]:
			var t := rmget(r, x, y)
			if t == 2: rmset(r, x, y, 6)
			elif t == 6 and not (r == room and ov(hx >> 8, hy >> 8, 6, 11, x * 8, y * 8, 8, 8)): rmset(r, x, y, 2)


func gim_flipend() -> void:
	var GW: GimWorld = wd.gw
	GW.flipt = 0
	if GW.flipk == GF_GRAVITY and GW.flip and GW.xroom == room: gim_op(room, XF_FLIP)
	if GW.flipk == GF_SWAP: gim_flipswap(GW.fliproom)
	sfx("tube"); kick(6)
	ev.append(["flipend"])


func gim_flipstart(x: int, y: int) -> void:
	var GW: GimWorld = wd.gw
	var k: int = L.gim[GO_FLIP]
	if k == GF_GRAVITY and room == 0: k = GF_SPEED   # the main area never turns over: its flag stays put
	if GW.flipt: gim_flipend()
	GW.flipk = k; GW.flipt = GIM_FLIPTIME; GW.fliproom = room
	for c in [0xff80e0, 0xffd84a, 0x60d8ff, 0xffffff]: burst(x, y, c, 8)
	sfx("reveal"); sfx("moon"); kick(10); rumble(5); addscore(1000, x, y - 6)
	ev.append(["flip", k])
	if k == GF_GRAVITY: gim_op(room, XF_FLIP)
	if k == GF_SWAP: gim_flipswap(room)


func gim_door(i: int) -> void:   # a quarter turn clockwise
	var GW: GimWorld = wd.gw
	var d := gim_dims(lvl, room, GW.rot if room == GW.xroom else 0)
	if d[0] > MH or d[1] > MW:
		if not _gim_told:
			_gim_told = true
			printerr("hatrick: a door can only turn a room up to %d columns wide" % MH)
		return
	gim_op(room, XF_ROT)   # Hatrick turns with the room, then steps out of a door with floor under it: this one if it has
	var to: GimObj = null
	var best := 1 << 30
	for j in GW.ob.size():
		var o: GimObj = GW.ob[j]
		if o.kind != 68 or o.room != room or scan(o.x * 8 + 1, o.y * 8 - 3, 6, 11, SOLID) or not scan(o.x * 8 + 1, o.y * 8 + 8, 6, 9, SOLID):
			continue   # floor at most a tile below
		var dx := o.x * 8 + 1 - (hx >> 8)
		var dy := o.y * 8 - 3 - (hy >> 8)
		var dist := -1 if j == i else dx * dx + dy * dy
		if dist < best:
			best = dist; to = o
	if to:
		hx = (to.x * 8 + 1) << 8; hy = (to.y * 8 - 3) << 8
	hvx = 0; hvy = 0; st = NORM; gnd = 0; face = 1
	var n := 1
	while n < 48 and scan(hx >> 8, hy >> 8, 6, 11, SOLID):   # nearest free spot, up or down
		hy += (n if n & 1 else -n) * 256
		n += 1
	GW.poldy = hy; GW.rott = 24
	cxf = hx - ((W / 2 - 3) << 8); cyf = hy - ((H * 5 / 8) << 8); camgy = hy
	sfx("tube"); sfx("gp_land"); kick(8); rumble(6)
	ev.append(["door"])


func gim_landed(p: GimPlat) -> void:   # what landing on the ground does, for a platform
	var GW: GimWorld = wd.gw
	landt = 0
	if st == GPSLAM:
		st = GPLAND; stt = 0; poundt = 31; kick(10); sfx("gp_land"); rumble(6)
		if p.kind == 2: p.tilt = 72 if (hx >> 8) + 3 > (p.x >> 8) else -72   # a pound slams the seesaw down
	elif st == DIVE:
		st = SLIDE; posture(5)
	elif st == LONGJ or st == SPINJ:
		st = NORM
	if GW.pvy > 900:
		sfx("land"); rumble(2)


# After hazards(): landing on platforms, and every other gimmick's rules.
func gim_post(k: int, pr: int) -> void:
	var GW: GimWorld = wd.gw
	var X := hx >> 8
	var Y := hy >> 8
	var alive := st < TUBE
	if (GW.rot or GW.flip) and GW.xroom == room:   # tubes and spawns read the file's size
		var dd := gim_dims(lvl, room, GW.rot)
		lw = dd[0]; lh = dd[1]
	# platforms: one-way, from above
	var was := GW.ride
	GW.ride = 0
	if alive and st != HANG and st != CLIMB and hvy >= 0 and not gnd:
		for i in GW.pl.size():
			var p: GimPlat = GW.pl[i]
			if p.room != room:
				continue
			var sy := gim_plattop(p, X)
			if sy == NOPLAT:
				continue
			var feet := hy + (11 << 8)
			var pfeet := GW.poldy + (11 << 8)
			if feet >= sy - 256 and pfeet <= sy + ((5 if was == i + 1 else 3) << 8):
				hy = sy - (11 << 8); Y = hy >> 8; hvy = 0; gnd = 1; GW.ride = i + 1
				if was != i + 1: gim_landed(p)
				break
	# gold block bumped from below
	if GW.pvy < 0 and hvy == 0 and alive:
		for o in GW.ob:
			if o.kind == 65 and not o.t and o.room == room and Y + duck == o.y * 8 + 8 and o.x * 8 < X + 6 and o.x * 8 + 8 > X:
				o.t = 1; setmap(o.x, o.y, 15)
				ev.append(["reveal", room, o.x, o.y])
				GW.gold = 20 * 60; GW.goldn = 0; GW.goldrun = 0; sfx("reveal"); sfx("bonus"); sparkle(o.x * 8 + 4, o.y * 8, 0xffe066, 10)
				break
	if GW.gold:   # running spills coins from it: faster running, more coins
		GW.gold -= 1
		if alive:
			GW.goldrun += absi(hvx)
			if GW.goldrun >= (6 << 8):
				GW.goldrun -= 6 << 8; GW.goldn += 1; coins += 1; score += 100; sfx("coin")
				ev.append(["coinpop", X - 1, Y - 12])
		elif st == DEAD:
			GW.gold = 0
		if GW.goldn >= 100: GW.gold = 0
		if not GW.gold and alive:
			burst(X + 3, Y - 4, 0xffd84a, 10); sfx("brick")
	for j in GW.ob.size():
		var o: GimObj = GW.ob[j]
		if o.room != room:
			continue
		var cx := o.x * 8
		var cy := o.y * 8
		if o.kind == 79 and not o.t and alive and ov(X, Y + duck, 6, 11 - duck, cx, cy, 8, 8):   # O
			o.t = 1; gim_flipstart(cx + 4, cy + 4); X = hx >> 8; Y = hy >> 8
		elif o.kind == 68 and room and alive and (pr & 4) and gnd and (st == NORM or st == GSPIN) and ov(X, Y, 6, 11, cx, cy - 6, 8, 14):   # D
			gim_door(j)
			return
		elif o.kind == 69:   # E: a cannon fires toward Hatrick when he's near
			o.t += 1
			if o.t >= 140:
				var dx := X + 3 - (cx + 4)
				var dir := 1 if dx > 0 else -1
				o.t = 0
				if alive and absi(dx) > 12 and absi(dx) < 220 and absi(Y - cy) < 120 and not scan(cx + dir * 8, cy, 8, 8, SOLID):
					for b in GW.ball:
						if not b.a:
							b.room = room; b.x = (cx + dir * 8) << 8; b.y = cy << 8; b.vx = dir * 352; b.a = 1
							sfx("spit"); kick(2); burst(cx + 4 + dir * 6, cy + 4, 0xc8c8c8, 4)
							break
		elif o.kind == 87:   # W: a fan's updraft, up to 10 tiles while nothing solid is in the way
			var top := o.y
			while top > 0 and o.y - top < 10 and not ((SOLID >> tile(o.x, top - 1)) & 1): top -= 1
			if alive and st != HANG and st != CLIMB and st != GPWIND and ov(X, Y, 6, 11, cx - 1, top * 8, 10, cy - top * 8):
				var pull := 60 + 60 * (Y + 11 - top * 8) / (cy - top * 8 + 1)
				hvy -= pull
				if hvy < -680: hvy = -680
				if st == GPSLAM or st == DIVE: st = NORM
				capok = 1
			if not (fr & 7): draws(2)
	for b in GW.ball:   # cannonballs: stomp them, or the cap
		if not b.a:
			continue
		if b.room != room:
			b.a = 0
			continue
		b.x += b.vx
		var bx: int = b.x >> 8
		var by: int = b.y >> 8
		if scan(bx + 1, by + 1, 6, 6, SOLID) or absi(bx - X) > 400:
			b.a = 0; burst(bx + 4, by + 4, 0x505060, 5)
			continue
		if alive and ov(X, Y + duck, 6, 11 - duck, bx + 1, by + 1, 6, 6):
			if (hvy > 0 or st == GPSLAM) and Y + 11 < by + 6:
				b.a = 0; burst(bx + 4, by + 4, 0x505060, 8); sfx("stomp"); rumble(4); kick(5); addscore(200, bx + 4, by)
				hvy = -1000 if (k & 16) else -650; st = NORM; arcg = GRAV; launch = 0; cut = 0
				capok = 1; diveok = 1; stall = 1; spin = 0; throwt = 0
			else:
				die()
		if b.a and cst and cst < 3 and capbox(bx, by, 8, 8):
			b.a = 0; burst(bx + 4, by + 4, 0x505060, 8); sfx("stomp"); addscore(200, bx + 4, by)
	if L.gim[GO_RISE] and room == 0 and st != WIN and not done:   # the rising kill line
		if GW.risedelay:
			GW.risedelay -= 1
		elif st < DEAD:
			GW.rise -= L.gim[GO_RISEV] * 256 / 60
			if GW.rise < 0: GW.rise = 0
		if st < TUBE and hy + (9 << 8) > GW.rise: doom()
		for e in wd.en:
			if e.a and e.r == 0 and e.y > GW.rise: e.a = 0
	if L.gim[GO_SCROLL] and room == 0:   # the auto-scroll: the screen moves on, and Hatrick can't leave it
		var maxsx := (lw * 8 - W) << 8 if lw * 8 > W else 0
		if st < DEAD and not done and fr > 60:
			GW.sx += L.gim[GO_SCROLL] * 256 / 60
			if GW.sx > maxsx: GW.sx = maxsx
		if alive:
			var sl := GW.sx >> 8
			if (hx >> 8) < sl:
				hx = sl << 8
				if hvx < 0: hvx = 0
				if scan(sl, (hy >> 8) + duck, 6, 11 - duck, SOLID): doom()   # squeezed against a wall
			if (hx >> 8) + 6 > sl + W:
				hx = (sl + W - 6) << 8
				if hvx > 0: hvx = 0
	if GW.flipt:
		if GW.flipt <= 180 and not (GW.flipt % 30): sfx("tick")
		GW.flipt -= 1
		if not GW.flipt: gim_flipend()


func gim_again(k: int) -> void:   # the speed flip: a second step of the whole game each frame (but not of the clocks)
	var GW: GimWorld = wd.gw
	if gim_again_on or not GW.flipt or GW.flipk != GF_SPEED or st >= DEAD:
		return
	var l0 := left
	var t0 := tim
	var f0 := fr
	gim_again_on = true
	tick(k)
	gim_again_on = false
	if left == l0 - 1: left = l0
	if tim == t0 + 1: tim = t0
	if fr == f0 + 1: fr = f0   # the frame clock too: beat hazards follow it and must keep time with the music


func gim_camera() -> void:   # presentation: the auto-scroll owns the camera's x
	if L.gim[GO_SCROLL] and room == 0: cxf = wd.gw.sx
