# boss.c, ported line by line: the four bosses and the two mini-bosses. A level names its boss with
# boss= in its header and marks where it lives with N; J marks a mini-boss (mini=walker or
# mini=buzzer). Each one owns an arena: the 32 columns (one screen) around its marker. Walking in
# shuts stone gates on both sides; beating the boss opens them. A death restarts the fight; a
# checkpoint touched past an arena keeps that boss beaten.
#
#   haberdasher  the main villain. Phase 1: scissor boomerangs (the cap knocks them down); after
#                two volleys he pants, and a stomp or the cap hurts him. Phase 2: his top hat flies
#                off; hit it with the cap to wear it, and only then can a stomp hurt him.
#   bigwalker    a giant walker on a seesaw over lava (N is the pivot, on the board's row).
#                Ground pound the side he stands on and he slides off into the lava.
#   cloudking    floats out of reach. Throw the cap into one of his lightning clouds and the
#                cloud zaps him. The clouds can be stood on; they strike the floor below them.
#   matriarch    a giant snapper in a whack-a-mole arena of tubes (the up/down tube mouths in the
#                arena). Only the cap hurts her; later she brings decoys.
#   mini=walker  Chief Walker: a big, fast walker. Stomp it three times; the cap only stuns it.
#   mini=buzzer  Buzz Boss: a big buzzer that swoops and drops stingers. Stomp it or cap it.
#
# This is the last file of the port (sim_core.gd <- sim_ext.gd <- sim_gim.gd <- sim.gd), so this is
# the script the game and the tools load.
extends "res://src/sim/sim_gim.gd"

enum { BOSS_NONE, BOSS_HABERDASHER, BOSS_BIGWALKER, BOSS_CLOUDKING, BOSS_MATRIARCH }
enum { MINI_NONE, MINI_WALKER, MINI_BUZZER }
const BOSSTITLE := ["", "THE HABERDASHER", "BIG WALKER", "CLOUD KING", "SNAPPER MATRIARCH"]
const MINITITLE := ["", "CHIEF WALKER", "BUZZ BOSS"]
enum { BS_NONE, BS_SCISSORS, BS_SEED, BS_STING, BS_ZAP }
const SAWL := 60


class Boss:
	var kind := 0; var mini := 0; var room := 0; var mx := 0; var my := 0   # kind BOSS_* (main) or mini MINI_*; the marker cell
	var on := 0; var ax0 := 0; var ax1 := 0; var ng := 0   # 0 waiting, 1 fighting, 2 beaten; gate columns; gate cells shut
	var x := 0; var y := 0; var vx := 0; var vy := 0; var face := 0; var gnd := 0   # 1/256 px (walkers: top-left of the body)
	var hp := 0; var maxhp := 0; var phase := 0; var s := 0; var t := 0; var hurt := 0; var n := 0
	var intro := 0; var dead := 0; var vol := 0   # s: what it is doing, t: for how long
	var gc: Array = []   # the gate cells shut: [x, y]


class BShot:   # 1/256 px
	var a := 0; var kind := 0; var x := 0; var y := 0; var vx := 0; var vy := 0; var t := 0; var dir := 0


class Cloud:   # the cloud king's lightning clouds (1/256 px); charge counts up to a strike
	var a := 0; var x := 0; var y := 0; var vx := 0; var lane := 0; var t := 0; var charge := 0
	var strike := 0; var back := 0


var bz: Array = []    # Boss, at most 2
var bsh: Array = []   # 24 BShot
var bcl: Array = []   # 3 Cloud
# the seesaw (bigwalker): pivot and tilt (px of drop per 256 px), ground-pound slam timer
var sawx := 0; var sawy := 0; var sawt := 0; var sawslam := 0
var floorpx := 0      # the arena floor under the marker (px)
# the matriarch's tubes, and a decoy snapper
var mtube: Array = []
var mcur := -1; var mofs := 0; var dcur := -1; var dofs := 0; var dph := 0; var dt := 0
# the haberdasher's top hat: 0 on his head, 1 flying, 2 worn by Hatrick (hatt frames left)
var hatst := 0; var hatx := 0; var haty := 0; var hatt := 0
# Hatrick on a boss platform (seesaw, cloud): this frame, last frame; a ground pound landed on one
var bostand := 0; var bowas := 0; var bopound := 0; var bocloud := 0
var bossgrace := 0    # frames bosses can't hurt him


func _init() -> void:
	for i in 24: bsh.append(BShot.new())
	for i in 3: bcl.append(Cloud.new())


static func bsign(v: int) -> int:
	return 1 if v > 0 else (-1 if v < 0 else 0)


func bshot(kind: int, x: int, y: int, vx: int, vy: int, dir: int) -> void:
	for s in bsh:
		if not s.a:
			s.a = 1; s.kind = kind; s.x = x; s.y = y; s.vx = vx; s.vy = vy; s.t = 0; s.dir = dir
			return


func mainboss() -> Boss:
	for b in bz:
		if b.kind: return b
	return null


# Stone gates on the arena's two edge columns, wherever they are empty.
func bossgates(b: Boss, shut: bool) -> void:
	var h: int = LV[lvl].rooms[b.room].h
	if shut:
		b.ng = 0; b.gc = []
		for c in 2:
			var x := b.ax1 if c else b.ax0
			for y in h:
				if not rmget(b.room, x, y):
					rmset(b.room, x, y, 4); b.gc.append([x, y]); b.ng += 1
			for y in range(0, h, 3): draws(3)
		sfx("brick"); kick(8); rumble(3)
		ev.append(["gates", b.room, b.ax0, b.ax1, 1])
	else:
		for i in b.ng:
			var x: int = b.gc[i][0]
			var y: int = b.gc[i][1]
			if rmget(b.room, x, y) == 4: rmset(b.room, x, y, 0)
			if not (i % 3): burst(x * 8 + 4, y * 8 + 4, 0xb0b8c4, 2)
		b.ng = 0
		ev.append(["gates", b.room, b.ax0, b.ax1, 0])


func bosswin(b: Boss, points: int, x: int, y: int) -> void:
	b.on = 2; b.dead = 1; b.t = 0
	bossgates(b, false)
	addscore(points, x, y); sfx("bonus"); sfx("moon"); kick(14); rumble(8)
	for i in 4:
		draws(2)
		burst(x, y, 0xffe066 if i & 1 else 0xffffff, 10)
	for s in bsh:
		if s.a and s.kind != BS_ZAP:
			burst(s.x >> 8, s.y >> 8, 0xffffff, 3); s.a = 0
	ev.append(["bosswin", bz.find(b), x, y])


func bosshurt(b: Boss, x: int, y: int) -> bool:   # one hit; returns true if that was the last
	if b.hurt:
		return false
	b.hp -= 1; b.hurt = 50
	burst(x, y, 0xffffff, 8); sfx("stomp"); kick(9); rumble(4)
	ev.append(["bosshurt", bz.find(b)])
	return b.hp <= 0


# What a body at (x, y, w, h) px does to Hatrick: 1 he lands on top of it, 2 he touches it.
func bosstouch(x: int, y: int, w: int, h: int) -> int:
	var X := hx >> 8
	var Y := (hy >> 8) + duck
	if st >= TUBE or not ov(X, Y, 6, 11 - duck, x, y, w, h):
		return 0
	return 1 if (hvy > 0 or st == GPSLAM) and (hy >> 8) + 11 < y + 7 else 2


func bossbounce() -> void:   # off a boss's head, as off an enemy
	hvy = -1000 if (prevk & 16) else -650; st = NORM; arcg = GRAV; launch = 0; cut = 0
	capok = 1; diveok = 1; stall = 1; spin = 0; throwt = 0


func bosscap(x: int, y: int, w: int, h: int) -> bool:
	return cst != 0 and cst < 3 and capbox(x, y, w, h)


func capclink() -> void:
	burst((cxp >> 8) + 4, (cyp >> 8) + 2, 0xffffff, 4); sfx("cap_bounce"); cst = 3


# Anything a boss does to Hatrick: fatal, unless he wears the haberdasher's hat, which it knocks off.
func bossharm(fromx: int) -> void:
	if st >= TUBE or bossgrace:
		return
	if hatst == 2:
		hatst = 1; hatx = (hx >> 8) + 3; haty = (hy >> 8) - 4; hatt = 0
		hvx = -600 if (hx >> 8) + 3 < fromx else 600; hvy = -700; st = NORM; gnd = 0; spin = 0; throwt = 0; bossgrace = 60
		burst(hatx, haty, 0x40304a, 8); sfx("cap_bounce"); rumble(4); kick(6)
		ev.append(["hatoff"])
		return
	die()


# Walking bosses: gravity and the map, a body w x h px; returns true if it bumped into a wall.
func bossmove(b: Boss, w: int, h: int) -> bool:
	b.vy += GRAV
	if b.vy > 1100: b.vy = 1100
	b.y += b.vy; b.gnd = 0
	var x := b.x >> 8
	var y := b.y >> 8
	var n := 0
	if scan(x, y, w, h, SOLID):
		var s := -1 if b.vy > 0 else 1
		y += s
		while scan(x, y, w, h, SOLID):
			n += 1
			if n >= 40:
				break
			y += s
		b.y = y << 8
		if b.vy > 0: b.gnd = 1
		b.vy = 0
	b.x += b.vx; x = b.x >> 8
	if scan(x, y, w, h, SOLID):
		b.x -= b.vx; b.vx = -b.vx
		return true
	return false


# A boss platform: its top at surf (px) from x0 to x1. Hatrick lands and stands on it.
func bossplat(surf: int, x0: int, x1: int) -> bool:
	var X := hx >> 8
	var feet := (hy >> 8) + 11
	var pfeet := (oldhy >> 8) + 11
	if bostand or st >= TUBE or st == HANG or st == CLIMB or hvy < 0 or X + 5 < x0 or X > x1:
		return false
	if feet < surf - 3 or (pfeet > surf + 2 and not (bowas and feet <= surf + 8)):
		return false
	if scan(X, surf - 11 + duck, 6, 11 - duck, SOLID):
		return false
	hy = (surf - 11) << 8; hvy = 0
	if not bowas:   # landing, as hero() lands on the map
		landt = 0
		if st == GPSLAM:
			st = GPLAND; stt = 0; poundt = 31; kick(10); sfx("gp_land"); rumble(6); bopound = 1
		elif st == DIVE:
			st = SLIDE; posture(5)
		elif st == LONGJ or st == SPINJ:
			st = NORM
	gnd = 1; coy = 0; bostand = 1
	return true


# ---- the haberdasher ----
func _hatoff(b: Boss, cx: int, by: int) -> void:   # phase 2: the hat flies off
	b.phase = 2; b.hp = 3; b.maxhp = 3; hatst = 1; hatx = cx; haty = by - 6; b.hurt = 60
	sfx("reveal"); burst(cx, by - 4, 0x40304a, 12)
	ev.append(["phase2"])


func haberdasher(b: Boss) -> void:
	var bx := b.x >> 8
	var by := b.y >> 8
	var X := hx >> 8
	var cx := bx + 5
	var dx := X + 3 - cx
	var angry := b.phase == 2
	if b.dead:
		b.vy += 20; b.y += b.vy; b.t += 1
		if not (b.t & 7): burst(cx, by + 8, 0xff7a1c if b.t & 8 else 0x40304a, 3)
		return
	if b.intro:
		b.intro -= 1; b.face = bsign(dx) if bsign(dx) else -1; bossmove(b, 10, 18)
	elif b.s == 0:   # walk to a new spot, facing Hatrick
		if not b.t:
			var spot := (b.ax0 + 3) * 8 + rnd((b.ax1 - b.ax0 - 6) * 8)
			b.vx = bsign(spot - cx) * (230 if angry else 130)
			b.t = 50 + rnd(30) if angry else 60 + rnd(40)
		if angry and hatst == 2:   # chasing his hat
			b.vx = bsign(dx) * 260; b.t = b.t if b.t > 2 else 2
		if bossmove(b, 10, 18): b.vx = -b.vx
		if angry and b.gnd and not rnd(50 if hatst == 2 else 110):
			b.vy = -1000; b.vx = bsign(dx) * 260
		b.face = bsign(b.vx) if bsign(b.vx) else b.face
		b.t -= 1
		if b.t <= 0:
			b.s = 1; b.t = 0; b.n = 0; b.vx = 0
	elif b.s == 1:   # throw scissors: a volley of 2 (3 when angry)
		bossmove(b, 10, 18)
		b.face = -1 if dx < 0 else 1
		if hatst == 2:
			b.s = 0; b.t = 0
		else:
			b.t += 1
			if b.t == 22:
				var aim := ((hy >> 8) + 5 - (by + 6)) * 256 / 70
				bshot(BS_SCISSORS, (cx + b.face * 6) << 8, (by + 6) << 8, b.face * (1150 if angry else 950), clampi(aim, -400, 400), b.face)
				sfx("cap_throw")
				b.t = 0
				b.n += 1
				if b.n >= (3 if angry else 2):
					b.t = 0; b.n = 0; b.s = 0 if angry else 2
	elif b.s == 2:   # a volley or two thrown: he waits for them to come back, then pants
		bossmove(b, 10, 18)
		var out := 0
		for s in bsh:
			if s.a and s.kind == BS_SCISSORS: out += 1
		if not out:
			b.vol += 1
			if b.vol >= 2:
				b.s = 3; b.t = 110; b.vol = 0
			else:
				b.s = 0; b.t = 0
	elif b.s == 3:   # panting: open to a stomp or the cap
		bossmove(b, 10, 18)
		b.t -= 1
		if b.t <= 0:
			b.s = 0; b.t = 0
	if b.hurt: b.hurt -= 1
	bx = b.x >> 8; by = b.y >> 8; cx = bx + 5
	var open := b.s == 3 or hatst == 2
	var top := by - 7 if hatst == 0 else by
	var touch := bosstouch(bx, top, 10, 18 + by - top)
	if touch == 1 and open and not b.hurt:
		bossbounce()
		var last := bosshurt(b, cx, by)
		if hatst == 2:   # the hat flies back off
			hatst = 1; hatx = X + 3; haty = (hy >> 8) - 4; hatt = 0
		b.s = 0; b.t = 30; b.vy = -700; b.vx = bsign(-dx) * 200
		if last and b.phase == 1: _hatoff(b, cx, by)
		elif last: bosswin(b, 10000, cx, by)
	elif touch == 1 and not b.hurt:   # the hat's pins: no harm done
		bossbounce(); sfx("cap_bounce"); burst(cx, top, 0xffffff, 4)
	elif touch == 2 and not b.hurt:
		bossharm(cx)
	if bosscap(bx, top, 10, 18 + by - top) and not (hatst == 1 and bosscap(hatx - 5, haty - 4, 10, 8)):   # the hat wins
		if b.s == 3 and not b.hurt:
			cst = 3
			var last := bosshurt(b, cx, by)
			b.s = 0; b.t = 30; b.vy = -600
			if last: _hatoff(b, cx, by)
		else:
			capclink()
	# the top hat: circles high over the arena; the cap catches it and Hatrick wears it a while
	if hatst == 1:
		var mid := (b.ax0 + b.ax1) * 4 + 4
		var tx := mid + SIN[(fr * 2) & 255] * 90 / 256
		var ty := floorpx - 60 + SIN[(fr * 5) & 255] * 12 / 256
		hatx += bsign(tx - hatx) * (2 if absi(tx - hatx) > 2 else 1); haty += bsign(ty - haty)
		if bosscap(hatx - 5, haty - 4, 10, 8):
			hatst = 2; hatt = 420; cst = 3; sfx("cap_catch"); sfx("reveal"); sparkle(hatx, haty, 0xfff0a8, 10)
			ev.append(["haton"])
	elif hatst == 2:
		hatt -= 1
		if hatt <= 0 or st >= TUBE:
			hatst = 1; hatx = X + 3; haty = (hy >> 8) - 4; burst(hatx, haty, 0x40304a, 5)
			ev.append(["hatoff"])


# ---- the big walker on its seesaw ----
func sawsurf(x: int) -> int:
	return sawy + (x - sawx) * sawt / 256


func bigwalker(b: Boss) -> void:
	var cx := (b.x >> 8) + 12
	var off := cx - sawx
	var hoff := (hx >> 8) + 3 - sawx
	# the board: leans under the walker's weight (and Hatrick's), or slammed by a ground pound
	var want := off * 44 / SAWL + (hoff * 20 / SAWL if bostand and not bocloud else 0)
	if sawslam: sawslam -= 1
	else: sawt += (want - sawt) / 8
	if b.dead:
		b.y += 128; b.t += 1
		if not (b.t & 3): draws(2)
		return
	var speed := 90 + (b.maxhp - b.hp) * 55
	if b.intro:
		b.intro -= 1; b.y = (sawsurf(cx) - 24) << 8
	elif b.s == 0:   # walking the board, turning at its ends, now and then a stomp that shakes it
		if absi(off) > SAWL - 14 and bsign(off) == b.face: b.face = -b.face
		b.x += b.face * speed
		b.y = (sawsurf(cx) - 24) << 8
		b.t += 1
		if b.t > 150 - (b.maxhp - b.hp) * 30:
			b.s = 1; b.t = 0; b.vy = -950
	elif b.s == 1:   # jump; landing shakes Hatrick off his feet
		b.vy += GRAV; b.y += b.vy; b.x += b.face * speed / 2
		if absi(cx - sawx) > SAWL - 14: b.x -= b.face * speed / 2
		if b.vy > 0 and (b.y >> 8) + 24 >= sawsurf(cx):
			b.y = (sawsurf(cx) - 24) << 8; b.s = 0; b.t = 0; kick(10); sfx("gp_land"); rumble(5)
			if bostand and not bocloud:
				hvy = -700; gnd = 0; bowas = 0
			burst(cx, sawsurf(cx), 0xc8a070, 6)
	elif b.s == 2:   # the board slammed under him: he slides off its low end
		b.vx += bsign(sawt) * 40; b.x += b.vx
		b.y = (sawsurf((b.x >> 8) + 12) - 24) << 8
		if absi((b.x >> 8) + 12 - sawx) > SAWL + 6:
			b.s = 3; b.vy = -300
	elif b.s == 3:   # falling into the lava
		b.vy += GRAV; b.y += b.vy; b.x += b.vx / 3
		if (b.y >> 8) + 12 > lh * 8 - 8:
			draws(16 * 3)
			ev.append(["lavasplash", cx, lh * 8 - 10])
			kick(12); rumble(6); sfx("stomp")
			if bosshurt(b, cx, lh * 8 - 16):
				bosswin(b, 5000, cx, lh * 8 - 40)
				return
			b.s = 4; b.t = 0; b.n = b.x   # a flaming leap back to the middle of the board
	elif b.s == 4:
		var T := 70
		b.t += 1
		var t := b.t
		var x0 := b.n
		var x1 := (sawx - 12) << 8
		b.x = x0 + (x1 - x0) / T * t
		var top := sawsurf(sawx) - 24
		var base := lh * 8 - 20
		var peak := top - 50
		# a parabola from the lava up over the peak and down onto the board
		var p := t * 256 / T
		var y := base + (top - base) * p / 256 - (4 * (peak - (base + top) / 2) * p / 256 * (256 - p) / 256) * -1
		b.y = y << 8
		if not (t & 1): draws(1)
		if t >= T:
			b.s = 0; b.t = 0; b.y = (sawsurf(sawx) - 24) << 8; kick(10); sfx("gp_land")
			if bostand and not bocloud:
				hvy = -700; gnd = 0; bowas = 0
	if b.hurt: b.hurt -= 1
	# a ground pound on the board: on his side it tips him off; on the other it just rocks it
	if bopound and not bocloud and b.s == 0:
		if bsign(hoff) == bsign(off) and absi(off) > 6:
			sawt = bsign(off) * 120; sawslam = 70; b.s = 2; b.vx = 0; sfx("skid")
		else:
			sawt = bsign(hoff) * 70; sawslam = 20
	var bx := b.x >> 8
	var by := b.y >> 8
	var touch := bosstouch(bx + 2, by + 3, 20, 21)
	if touch == 1 and b.s != 4:
		bossbounce(); sfx("cap_bounce"); burst(cx, by + 2, 0xffffff, 4); b.face = -b.face
	elif touch:
		bossharm(cx)
	if bosscap(bx + 2, by + 3, 20, 21): capclink()


# ---- the cloud king ----
func cloudking(b: Boss) -> void:
	var mid := (b.ax0 + b.ax1) * 4 + 4
	var range_ := (b.ax1 - b.ax0) * 4 - 40
	var lost := b.maxhp - b.hp
	if b.dead:
		b.vy += 12; b.y += b.vy; b.t += 1
		if not (b.t & 3): draws(1)
		return
	b.t += 1
	var sp := 2 + lost   # he drifts across the sky, faster when hurt
	b.x = (mid << 8) + SIN[(b.t * sp / 2) & 255] * range_
	b.y = ((b.my * 8 + 4) << 8) + SIN[(b.t * 5) & 255] * 6
	if b.hurt: b.hurt -= 1
	var kx := b.x >> 8
	var ky := b.y >> 8
	if bosscap(kx - 13, ky - 9, 26, 18): capclink()
	if bosstouch(kx - 12, ky - 8, 24, 16): bossharm(kx)
	if b.intro:
		b.intro -= 1
		return
	# his clouds: puffed out of him, they sink to a lane and drift; they charge, then strike
	for i in 3:
		var c: Cloud = bcl[i]
		var lane := floorpx - 30 - (28 if i == 1 else 0)
		if not c.a:
			if c.back > 0:
				c.back -= 1
				continue
			c.a = 1; c.x = b.x; c.y = b.y; c.vx = (1 if i & 1 else -1) * (90 + lost * 30); c.lane = lane
			c.t = 60 + i * 70; c.charge = 0; c.strike = 0; c.back = 0
			sfx("emerge"); burst(kx, ky + 6, 0xd8e0f0, 6)
		if (c.y >> 8) < c.lane: c.y += 256
		c.x += c.vx
		if (c.x >> 8) < (b.ax0 + 1) * 8 + 2 or (c.x >> 8) + 24 > b.ax1 * 8 - 2:
			c.vx = -c.vx; c.x += c.vx
		if c.strike:
			c.strike -= 1
			if c.strike == 0: c.t = 160 - lost * 35 + rnd(60)
		elif c.charge:
			c.charge += 1
			if c.charge > 50:
				c.charge = 0; c.strike = 22; kick(4); sfx("gp_land"); rumble(2)
		else:
			c.t -= 1
			if c.t <= 0: c.charge = 1
		var cx: int = (c.x >> 8) + 12
		var cy: int = c.y >> 8
		if c.strike and st < TUBE and ov(hx >> 8, (hy >> 8) + duck, 6, 11 - duck, cx - 3, cy + 8, 6, floorpx - cy - 8): bossharm(cx)
		if bosscap(cx - 12, cy - 4, 24, 12):   # the cap in a cloud: it zaps the king
			cst = 3; c.a = 0; c.back = 150
			bshot(BS_ZAP, cx << 8, cy << 8, 0, -900, 0)
			sfx("cap_catch"); sparkle(cx, cy, 0xfff070, 10); burst(cx, cy, 0xd8e0f0, 8)


# ---- the snapper matriarch ----
func mspot(i: int) -> Array:   # [cx, my, up]
	var t: Dictionary = L.tubes[i]
	var up: bool = t.dir == T_UP
	return [t.x * 8 + 8, t.y * 8 if up else t.y * 8 + 8, up]


func mpick(not1: int, not2: int) -> int:   # a tube away from Hatrick
	var best := -1
	var tries := 0
	while mtube.size() and tries < 30:
		tries += 1
		var i: int = mtube[rnd(mtube.size())]
		var sp := mspot(i)
		if i == not1 or i == not2:
			continue
		best = i
		if absi((hx >> 8) + 3 - sp[0]) > 28:
			break
	return best


func mhit(tube: int, ofs: int, half: int, x: int, y: int, w: int, h: int) -> bool:   # the body ofs px out, 2*half wide
	var sp := mspot(tube)
	return ofs > 3 and ov(x, y, w, h, sp[0] - half, sp[1] - ofs + 1 if sp[2] else sp[1], 2 * half, ofs - 1)


func mhitcap(tube: int, ofs: int, half: int) -> bool:
	return mhit(tube, ofs, half, (cxp >> 8) - CAPREACH, (cyp >> 8) - CAPREACH, 8 + 2 * CAPREACH, 5 + 2 * CAPREACH)


func matriarch(b: Boss) -> void:
	var X := hx >> 8
	var Y := (hy >> 8) + duck
	var lost := b.maxhp - b.hp
	if b.hurt: b.hurt -= 1
	if b.dead:
		if mofs > 0: mofs -= 1
		if dofs > 0: dofs -= 1
		return
	if b.intro:
		b.intro -= 1
		return
	if mcur < 0:
		mcur = mpick(-1, dcur)
		if mcur < 0:
			return
		b.s = 0; b.t = 50
	if b.s == 0:
		b.t -= 1
		if b.t <= 0:
			b.s = 1; sfx("emerge")
	elif b.s == 1:
		mofs += 2
		if mofs >= 28:
			mofs = 28; b.s = 2; b.t = 90 - lost * 10
	elif b.s == 2:
		if b.t == 60 - lost * 8 or (lost >= 2 and b.t == 30):   # a fan of seeds at Hatrick
			var sp := mspot(mcur)
			var dx: int = (X + 3 - sp[0]) * 256 / 80
			for kk in range(-1, 2):
				var vx := clampi(dx + kk * 140, -420, 420)
				bshot(BS_SEED, sp[0] << 8, (sp[1] - 26 if sp[2] else sp[1] + 26) << 8, vx, -620 if sp[2] else 0, 0)
			sfx("spit")
		b.t -= 1
		if b.t <= 0: b.s = 3
	elif mofs > 0:
		mofs -= 4 if b.hurt else 2
		if mofs <= 0:
			mofs = 0
			var was := mcur
			mcur = mpick(was, dcur); b.s = 0; b.t = 40 - lost * 5
	if mcur >= 0 and st < TUBE and mhit(mcur, mofs, 6, X, Y, 6, 11 - duck): die()
	if mcur >= 0 and cst and cst < 3 and mhitcap(mcur, mofs, 7) and b.s != 3:
		var sp := mspot(mcur)
		cst = 3
		if bosshurt(b, sp[0], sp[1] - mofs / 2 if sp[2] else sp[1] + mofs / 2):
			bosswin(b, 5000, sp[0], sp[1] - 20 if sp[2] else sp[1] + 20); b.s = 3
			return
		b.s = 3
	# a decoy: an ordinary snapper in another tube, once she's been hurt twice
	if lost >= 2:
		if dcur < 0 and not dofs:
			dt -= 1
			if dt <= 0:
				dcur = mpick(mcur, -1); dph = 1
		elif dph == 1:
			dofs += 1
			if dofs >= 16:
				dph = 2; dt = 80
		elif dph == 2:
			dt -= 1
			if dt <= 0: dph = 3
		elif dph == 3:
			dofs -= 1
			if dofs <= 0:
				dofs = 0; dcur = -1; dph = 0; dt = 60 + rnd(60)
		if dcur >= 0 and st < TUBE and mhit(dcur, dofs, 3, X, Y, 6, 11 - duck): die()
		if dcur >= 0 and cst and cst < 3 and mhitcap(dcur, dofs, 4) and dph != 3:
			var sp := mspot(dcur)
			burst(sp[0], sp[1] - dofs / 2 if sp[2] else sp[1] + dofs / 2, 0x2f8f9a, 8); sfx("stomp")
			addscore(200, sp[0], sp[1] - dofs if sp[2] else sp[1])
			dofs = 0; dcur = -1; dph = 0; dt = 120


# ---- mini-bosses: a buffed walker or buzzer in a locked room ----
func miniwalker(b: Boss) -> void:
	var bx := b.x >> 8
	var by := b.y >> 8
	var dx := (hx >> 8) + 3 - (bx + 8)
	if b.dead:
		b.vy += GRAV; b.y += b.vy
		return
	if b.hurt: b.hurt -= 1
	if b.intro:
		b.intro -= 1; bossmove(b, 16, 16)
	elif b.s == 1:   # stunned by the cap
		b.vx = 0; bossmove(b, 16, 16)
		b.t -= 1
		if b.t <= 0: b.s = 0
	else:
		var speed := 110 + (b.maxhp - b.hp) * 50
		if b.gnd and not (fr & 31): b.face = -1 if dx < 0 else 1
		if b.hurt > 30: b.vx = -bsign(dx) * 200   # backs off after a hit
		else: b.vx = b.face * speed
		if bossmove(b, 16, 16): b.face = -b.face
		if b.gnd:
			b.t += 1
			if b.t > 100 - (b.maxhp - b.hp) * 20:
				b.t = 0; b.vy = -850; kick(2)
	bx = b.x >> 8; by = b.y >> 8
	var touch := bosstouch(bx + 1, by + 2, 14, 14)
	if touch == 1 and not b.hurt:
		bossbounce()
		if bosshurt(b, bx + 8, by): bosswin(b, 1000, bx + 8, by)
	elif touch == 1:
		bossbounce()
	elif touch == 2 and not b.hurt:
		bossharm(bx + 8)
	if bosscap(bx, by, 16, 16):
		capclink()
		if b.s != 1:
			b.s = 1; b.t = 45; sparkle(bx + 8, by, 0xfff0a8, 6)
	if by > lh * 8: bosswin(b, 1000, bx + 8, lh * 8 - 16)


func minibuzzer(b: Boss) -> void:
	var cx := (b.x >> 8) + 8
	var cy := (b.y >> 8) + 8
	var lost := b.maxhp - b.hp
	if b.dead:
		b.vy += GRAV; b.y += b.vy
		return
	if b.hurt: b.hurt -= 1
	b.t += 1
	var hx0 := b.mx * 8 + 4
	var hy0 := b.my * 8 + 4
	if b.intro:
		b.intro -= 1
	elif b.s == 0:   # a figure of eight around its home, dropping stingers
		var a := b.t * (3 + lost) / 2
		var tx := hx0 + SIN[a & 255] * 72 / 256
		var ty := hy0 + SIN[(a * 2) & 255] * 18 / 256
		b.x += (((tx - 8) << 8) - b.x) / 6; b.y += (((ty - 8) << 8) - b.y) / 6
		if not (b.t % (70 - lost * 15)):
			bshot(BS_STING, cx << 8, (cy + 6) << 8, 0, 420, 0); sfx("spit")
		if b.t % 240 == 239:
			b.s = 1; b.n = 0; b.vx = ((hx >> 8) + 3 - cx) * 256 / 40; b.vy = ((hy >> 8) + 2 - cy) * 256 / 40
	else:   # a swoop at where Hatrick was, then back up
		b.x += b.vx; b.y += b.vy
		b.n += 1
		if b.n == 40:
			b.vx = -b.vx; b.vy = -b.vy
		if b.n >= 80: b.s = 0
		if scan(b.x >> 8, b.y >> 8, 16, 16, SOLID) and b.n < 40:
			b.vx = -b.vx; b.vy = -b.vy; b.n = 80 - b.n
	var bx := b.x >> 8
	var by := b.y >> 8
	var touch := bosstouch(bx + 1, by + 2, 14, 12)
	if touch == 1 and not b.hurt:
		bossbounce()
		if bosshurt(b, cx, cy):
			bosswin(b, 1000, cx, cy); b.vy = -400
	elif touch == 1:
		bossbounce()
	elif touch == 2 and not b.hurt:
		bossharm(cx)
	if bosscap(bx, by, 16, 14) and not b.hurt:
		cst = 3
		if bosshurt(b, cx, cy):
			bosswin(b, 1000, cx, cy); b.vy = -400


# ---- boss shots: scissors, seeds, stingers, and the zaps that hit the cloud king ----
func bossshots() -> void:
	var k := mainboss()
	for s in bsh:
		if not s.a:
			continue
		s.t += 1
		if s.kind == BS_SCISSORS:   # out, slowing, then back to the haberdasher
			s.vx -= s.dir * 22
			if k and s.t > 30:
				var ty := ((k.y >> 8) + 6) << 8
				s.vy += bsign(ty - s.y) * 12
				s.vy = clampi(s.vy, -300, 300)
			if k and s.t > 40 and absi(s.x - (k.x + (5 << 8))) < (6 << 8) and absi(s.y - (k.y + (6 << 8))) < (12 << 8):
				s.a = 0
				continue
			if s.t > 260:
				s.a = 0
				continue
		elif s.kind == BS_SEED:
			s.vy += 14
		elif s.kind == BS_ZAP and k:   # homes in on the king
			var dx: int = k.x - s.x
			var dy: int = k.y - s.y
			var d := maxi(absi(dx), absi(dy))
			if d < (10 << 8):
				s.a = 0
				if not k.dead and bosshurt(k, k.x >> 8, k.y >> 8): bosswin(k, 5000, k.x >> 8, k.y >> 8)
				sparkle(k.x >> 8, k.y >> 8, 0xfff070, 12)
				continue
			if d:
				s.vx += dx * 160 / d - s.vx / 8; s.vy += dy * 160 / d - s.vy / 8
		s.x += s.vx; s.y += s.vy
		var sx: int = s.x >> 8
		var sy: int = s.y >> 8
		if sy > lh * 8 + 16 or sy < -64 or sx < -16 or sx > lw * 8 + 16:
			s.a = 0
			continue
		if s.kind == BS_ZAP:
			continue
		if (s.kind == BS_SEED or s.kind == BS_STING) and ((SOLID >> tile(sx >> 3, sy >> 3)) & 1):
			s.a = 0; burst(sx, sy, 0x9a6a3a, 2)
			continue
		if st < TUBE and ov(hx >> 8, (hy >> 8) + duck, 6, 11 - duck, sx - 3, sy - 3, 6, 6):
			bossharm(sx); s.a = 0
			continue
		if cst and cst < 3 and capbox(sx - 3, sy - 3, 6, 6):
			s.a = 0; burst(sx, sy, 0xc0c8d0, 5); addscore(100, sx, sy); sfx("cap_bounce")


# Sets up the bosses of the current level; called whenever Hatrick (re)starts it.
func bossstart() -> void:
	bz = []
	for s in bsh: s.a = 0; s.kind = 0; s.x = 0; s.y = 0; s.vx = 0; s.vy = 0; s.t = 0; s.dir = 0
	for c in bcl: c.a = 0; c.x = 0; c.y = 0; c.vx = 0; c.lane = 0; c.t = 0; c.charge = 0; c.strike = 0; c.back = 0
	bostand = 0; bowas = 0; bopound = 0; bocloud = 0; bossgrace = 0; hatst = 0; hatt = 0; mtube = []
	mcur = -1; dcur = -1; mofs = 0; dofs = 0; dph = 0; dt = 60; sawslam = 0
	for r in L.rooms.size():
		var R: Dictionary = L.rooms[r]
		for y in R.h:
			for x in R.w:
				var c := _grid(R, x, y)
				if ((c != 78 or not L.boss) and (c != 74 or not L.mini)) or bz.size() == 2:   # N J
					continue
				var b := Boss.new()
				bz.append(b)
				b.room = r; b.mx = x; b.my = y; b.face = -1; b.intro = 60
				if c == 78: b.kind = L.boss
				else: b.mini = L.mini
				var w: int = R.w
				b.ax0 = maxi(x - 15, 0); b.ax1 = mini(x + 16, w - 1)
				b.hp = 3; b.maxhp = 3; b.phase = 1
				if b.kind == BOSS_MATRIARCH:
					b.hp = 5; b.maxhp = 5
				b.x = (x * 8 - 1) << 8; b.y = (y * 8 - 10) << 8   # a walking body stands on the marker's floor
				if b.mini == MINI_WALKER:
					b.x = (x * 8 - 4) << 8; b.y = (y * 8 - 8) << 8
				if b.mini == MINI_BUZZER:
					b.x = (x * 8 - 4) << 8; b.y = (y * 8 - 4) << 8
				if b.kind:
					var fy: int = y
					while fy < R.h - 1 and not ((SOLID >> tiletype(_grid(R, x, fy))) & 1): fy += 1
					floorpx = fy * 8
				if b.kind == BOSS_BIGWALKER:
					sawx = x * 8 + 4; sawy = y * 8; sawt = 0; b.x = (sawx - 12) << 8; b.y = (sawy - 24) << 8; b.face = 1
				if b.kind == BOSS_CLOUDKING:
					b.x = (x * 8 + 4) << 8; b.y = (y * 8 + 4) << 8
				if b.kind == BOSS_MATRIARCH:
					for i in L.tubes.size():
						var t: Dictionary = L.tubes[i]
						if mtube.size() < 24 and t.room == r and t.x > b.ax0 and t.x < b.ax1 and (t.dir == T_UP or t.dir == T_DOWN):
							mtube.append(i)
				if haveck and ckroom == r and (ckx >> 11) > b.ax1:   # beaten before the checkpoint
					b.on = 2; b.dead = 2


# The camera settles on the arena during a fight (called at the end of camera()).
func bosscam() -> void:
	for b in bz:
		if b.room == room and b.on == 1 and not b.dead:
			cxf += (((b.ax0 * 8) << 8) - cxf) / 3
			return


func bosstick() -> void:
	bossnear = 0   # the music's danger strings, while a fight is on
	if not bz.size():
		return
	bowas = bostand; bostand = 0
	if bossgrace: bossgrace -= 1
	var X := hx >> 8
	for bi in bz.size():
		var b: Boss = bz[bi]
		if b.room != room or b.dead == 2:
			continue
		if not b.on:   # waiting for Hatrick to walk in
			if st < TUBE and X >= (b.ax0 + 2) * 8 and X + 6 <= (b.ax1 - 1) * 8:
				b.on = 1; bossgates(b, true); sfx("reveal")
				ev.append(["bossstart", bi])
			elif b.kind != BOSS_BIGWALKER and b.kind != BOSS_CLOUDKING:
				continue
		if b.on == 2 and b.dead and b.t > 200:
			continue
		if b.on == 0:
			continue
		match b.kind:
			BOSS_HABERDASHER: haberdasher(b)
			BOSS_BIGWALKER: bigwalker(b)
			BOSS_CLOUDKING: cloudking(b)
			BOSS_MATRIARCH: matriarch(b)
			_:
				if b.mini == MINI_WALKER: miniwalker(b)
				else: minibuzzer(b)
		if b.dead: b.t += 1
		if b.on == 1 and not b.dead: bossnear = 1
	bossshots()
	# platforms: the seesaw and the clouds
	bopound = 0; bocloud = 0
	for b in bz:
		if b.room != room:
			continue
		if b.kind == BOSS_BIGWALKER:
			var x := (hx >> 8) + 3
			if x >= sawx - SAWL and x <= sawx + SAWL: bossplat(sawsurf(x), sawx - SAWL, sawx + SAWL)
			if b.on and st < TUBE and (hy >> 8) + 11 > lh * 8 - 8 and tile(x >> 3, lh - 1) == 0: doom()   # the lava
		if b.kind == BOSS_CLOUDKING and not b.dead:
			for c in bcl:
				if c.a and (c.y >> 8) >= c.lane - 1:
					if bossplat((c.y >> 8) - 4, (c.x >> 8) + 1, (c.x >> 8) + 23):
						hx += c.vx; bocloud = 1


# items.h: is x (px) inside a boss arena of this area? (no flag can be planted there)
func bossinarena(x: int) -> bool:
	for b in bz:
		if b.room == room and x >= b.ax0 * 8 and x < (b.ax1 + 1) * 8:
			return true
	return false
