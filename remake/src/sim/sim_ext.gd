# enemies.h's extra enemies, worlds.c (snow, quicksand, jelly, flowers, clock blocks, crabs, the
# avalanche) and the level side of collect.h (the secret exit and the postcard), ported line by
# line like sim_core.gd, whose hooks this file fills in.
extends "res://src/sim/sim_core.gd"

const EXTCHARS := "ukvLxpQYZ"   # in T_ order
const MAXTICK := 960

# worlds.c
var tick_: Array = []      # clock blocks: [room, x, y, kind]
var tickwarn := 0          # the clock blocks blink: they swap within half a beat
var flowertotal := 0
var flowerleft := 0
var jelx := 0; var jely := 0; var jelt := 0   # the jelly block last bounced on, for its squash
var avx := 0               # the avalanche's front, 1/256 px
var insand := 0
# collect.h
var cardgot := false       # the level's postcard is already at home (main sets it from the progress file)
var clsecretnow := 0       # the flag being touched is the secret exit


func _grid(R: Dictionary, x: int, y: int) -> int:
	return R.grid[y * R.w + x]


func _empty(R: Dictionary, x: int, y: int) -> bool:
	if x < 0 or y < 0 or x >= R.w or y >= R.h:
		return false
	var c := _grid(R, x, y)
	return c == 0 or c == 32


# ---------- enemies.h ----------
# The level file's character c at cell (x, y) of area r: one of ours?
func extspawn(r: int, x: int, y: int, c: int) -> void:
	if c == 0:
		return
	var p := EXTCHARS.find(char(c))
	if p < 0 or wd.en.size() >= MAXEN:
		return
	var R: Dictionary = L.rooms[r]
	var t := T_SHY + p
	if t == T_BEATPLAT and x > 0 and _grid(R, x - 1, y) == 89:   # one entry per run of Y blocks
		return
	var e := En.new()
	wd.en.append(e)
	e.t = t; e.a = 1; e.r = r; e.x = x << 11; e.y = y << 11; e.h = y << 11
	if t == T_SHY or t == T_SHELL: e.vx = -100
	if t == T_THIEF: e.w = e.x   # its perch
	if t == T_CHOMP:   # the ball rests beside its stake
		e.h = (x << 9) | y; e.x = (x * 8 - 10) << 8; e.y = (y * 8 - 2) << 8
	if t == T_BEATPLAT:
		while x + e.w < R.w and _grid(R, x + e.w, y) == 89: e.w += 1
		e.vx = 1 if _empty(R, x + e.w, y) and _empty(R, x + e.w + 1, y) else (-1 if _empty(R, x - 1, y) and _empty(R, x - 2, y) else 0)
	if t == T_PISTON:   # spikes go out of the first free side: up, left, right, down
		var DX := [0, -1, 1, 0]
		var DY := [-1, 0, 0, 1]
		var d := 0
		while d < 3 and not _empty(R, x + DX[d], y + DY[d]): d += 1
		e.vx = DX[d]; e.vy = DY[d]


# Standing on a thwomp (called by hero() when no tile holds Hatrick up).
func extride() -> bool:
	var X := hx >> 8
	var Y := hy >> 8
	exride = 0
	for i in wd.en.size():
		var e: En = wd.en[i]
		if e.a and e.r == room and e.t == T_THWOMP:
			var ex: int = e.x >> 8
			var ey: int = e.y >> 8
			if X + 6 > ex and X < ex + 16 and Y + 11 >= ey - 2 and Y + 11 <= ey + 6 + (hvy >> 8):
				hy = (ey - 11) << 8; hvy = 0; gnd = 1; exride = i + 1
				return true
	return false


func _sgn(v: int) -> int:   # C's (v ? 1 : -1) comparisons, written out
	return 1 if v > 0 else -1


func extenemy(i: int, k: int) -> void:
	var e: En = wd.en[i]
	var me := i + 1
	var X := hx >> 8
	var Y := hy >> 8
	var alive := st < TUBE
	var ex: int = e.x >> 8
	var ey: int = e.y >> 8
	var h := 0
	match e.t:
		T_SHY:   # u: frames until it finishes turning toward the cap
			ewalk(e, true); ex = e.x >> 8; ey = e.y >> 8
			var dir := 1 if e.vx > 0 else -1
			var ccx := (cxp >> 8) + 4
			var ccy := (cyp >> 8) + 2
			var out := cst != 0 and cst < 3 and not capstolen()
			if e.u:
				e.u -= 1
				if not e.u:
					e.vx = -e.vx; dir = -dir
			elif out and (1 if ccx > ex + 4 else -1) != dir and absi(ccx - ex - 4) < 48 and absi(ccy - ey - 4) < 20:
				e.u = 6   # it sees the cap coming
			if ecap(ex, ey, 8, 8):
				if (1 if ccx > ex + 4 else -1) == dir and not e.u:   # the mask blocks it
					ecapback(); sfx("cap_bounce"); burst(ex + 4 + dir * 4, ey + 3, 0xffffff, 4)
					if not e.vy: e.vy = -320
					ev.append(["block", i])
				else:
					ekill(e, 0xd83a3a, 300, "cap")
					return
			h = ehero(ex + 1, ey + 1, 6, 7)
			if h == 1:
				ekill(e, 0xd83a3a, 300); ebounce(k)
			elif h == 2:
				if (1 if X + 3 > ex + 4 else -1) != dir: ekill(e, 0xd83a3a, 300)   # from behind it topples over
				else: die()
		T_SHELL:   # s: 0 walking, 1 a resting shell (w: frames), 2 sliding; u: frames the slide can't hurt
			if e.s == 2:
				e.vy += GRAV
				if e.vy > 900: e.vy = 900
				e.y += e.vy; ey = e.y >> 8
				if scan(ex, ey, 8, 8, SOLID):
					var sg := 1 if e.vy < 0 else -1
					ey += sg
					while scan(ex, ey, 8, 8, SOLID) and absi(ey - (e.y >> 8)) < 16: ey += sg
					e.y = ey << 8; e.vy = 0
				e.x += e.vx; ex = e.x >> 8
				if scan(ex, ey, 8, 8, SOLID):
					var hit := false
					while scan(ex, ey, 8, 8, 4):   # bricks break and it bounces back
						smash(htx, hty); hit = true
					e.x -= e.vx; e.vx = -e.vx; ex = e.x >> 8
					if not hit and absi(ex - X) < 160:
						sfx("land"); kick(2)
				if ey > lh * 8:
					e.a = 0
					return
				esmashall(e, ex, ey, 8, 8)
			else:
				ewalk(e, e.s == 0); ex = e.x >> 8; ey = e.y >> 8
				if not e.a:
					return
				if e.s == 1:
					e.w += 1
					if e.w > 420:   # walks back out
						e.s = 0; e.w = 0; e.vx = -100 if X < ex else 100
			if e.u: e.u -= 1
			if not e.u and ecap(ex, ey, 8, 8):
				if e.s == 0:
					e.s = 1; e.vx = 0; e.w = 0; sfx("stomp"); addscore(100, ex + 4, ey)
				elif e.s == 1:
					e.s = 2; e.vx = 640 if (cxp >> 8) + 4 < ex + 4 else -640; e.u = 14; sfx("stomp")
				e.u = e.u if e.u else 14
			h = ehero(ex + 1, ey + 1, 6, 7)
			if e.s == 0:
				if h == 1:
					e.s = 1; e.vx = 0; e.w = 0; e.u = 10; ebounce(k); sfx("stomp"); rumble(3); addscore(100, ex + 4, ey)
				elif h == 2:
					die()
			elif e.s == 1:
				if h and not e.u:
					var fast := st == ROLL or st == DIVE or st == SLIDE
					e.s = 2; e.vx = (1 if ex + 4 > X + 3 else -1) * (1000 if fast else 640); e.u = 14; e.w = 0
					sfx("stomp"); rumble(3); kick(2)
					if h == 1: ebounce(k)
			elif h == 1 and not e.u:
				e.s = 1; e.vx = 0; e.w = 0; e.u = 10; ebounce(k); sfx("stomp")
			elif h == 2 and not e.u:
				die()
		T_THIEF:   # s: 0 perched, 1 swooping at the cap, 2 flying off with it, 3 going home
			if capthief == me and (e.s != 2 or not cst): capthief = 0
			if e.s == 2 and capthief != me: e.s = 3
			var ecx := ex + 4
			var ecy := ey + 4
			var ccx := (cxp >> 8) + 4
			var ccy := (cyp >> 8) + 2
			if e.s == 0:
				e.y = e.h + SIN[(fr * 4) & 255] * 2
				if cst and cst < 3 and ckind != CAPSPIN and not capthief and absi(ccx - ecx) < 64 and absi(ccy - ecy) < 40:
					e.s = 1; sfx("emerge")
			elif e.s == 1:
				if not cst or cst == 3 or capthief:
					e.s = 3
				else:
					var dx := (ccx - ecx) * 256
					var dy := (ccy - ecy) * 256
					var d := isqrt((dx >> 8) * (dx >> 8) + (dy >> 8) * (dy >> 8)) + 1
					e.vx = dx / d * 2; e.vy = dy / d * 2; e.x += e.vx; e.y += e.vy
					if ov(ex, ey, 8, 8, cxp >> 8, cyp >> 8, 8, 5):
						e.s = 2; capthief = me; cst = 2; ct = 0; sfx("cap_catch"); burst(ccx, ccy, 0xffffff, 5)
						ev.append(["steal", i])
			elif e.s == 2:   # keeps just out of reach, waiting when far ahead
				var away := 1 if ecx > X + 3 else -1
				var dist := absi(ecx - X - 3)
				var sp := 0 if dist > 150 else (140 if dist > 90 else 300)
				e.vx += 30 if e.vx < away * sp else (-30 if e.vx > away * sp else 0)
				var ty := ((hy >> 8) - 30) << 8
				var vy := (ty - e.y) / 12
				e.vy = clampi(vy, -200, 200)
				var nx: int = (e.x + e.vx) >> 8
				if not scan(nx, ey, 8, 8, SOLID) and nx > 0 and nx < lw * 8 - 8:
					e.x += e.vx
				else:
					e.vx = 0; e.vy = -200   # over the wall
				var ny: int = (e.y + e.vy) >> 8
				if not scan(e.x >> 8, ny, 8, 8, SOLID) and ny > 0: e.y += e.vy
				cst = 2; ct = 0; cxp = e.x; cyp = e.y + (7 << 8)   # the cap dangles from its feet
			else:
				var dx: int = e.w - e.x
				var dy: int = e.h - e.y
				e.vx = clampi(dx, -300, 300); e.x += e.vx
				e.y += clampi(dy, -200, 200)
				if absi(dx) < 256 and absi(dy) < 256: e.s = 0
			ex = e.x >> 8; ey = e.y >> 8
			h = ehero(ex, ey + 1, 8, 6)
			if h == 1:
				ekill(e, 0x3a2c5a, 400); ebounce(k)
			elif h == 2 and e.s == 2:   # snatched back
				ecapback(); e.s = 3; sfx("cap_catch"); sparkle(ex + 4, ey + 4, 0xffe066, 6)
		T_RIDER:   # s: 0 waiting, 1 following Hatrick; u: frames to the next drop
			if e.s == 0:
				if absi(X - ex) < 200: e.s = 1
				return
			var tx := (X - 6 + SIN[(fr * 2) & 255] * 48 / 256) * 256
			var d := (tx - e.x) / 24
			e.vx = clampi(d, -380, 380)
			e.x += e.vx; e.y = e.h + SIN[(fr * 3) & 255] * 3
			ex = e.x >> 8; ey = e.y >> 8
			e.u += 1
			if e.u >= 150:
				e.u = 0
				var mine := 0
				for o in wd.en:
					if o.a and o.t == 1 and o.s == 1000 + me: mine += 1
				if mine < 3 and wd.en.size() < MAXEN and alive:
					var n := En.new()
					n.t = 1; n.a = 1; n.r = e.r; n.x = e.x + (2 << 8); n.y = e.y - (2 << 8); n.h = n.y
					n.vx = -100 if X < ex else 100; n.vy = -500; n.s = 1000 + me
					wd.en.append(n)
					sfx("spit")
					ev.append(["drop", i])
			if ecap(ex, ey, 14, 9):
				ekill(e, 0xf4f8ff, 800, "cap")
				return
			h = ehero(ex, ey, 14, 9)
			if h == 1:
				ekill(e, 0xf4f8ff, 800); ebounce(k)
			elif h == 2:
				die()
		T_CHOMP:   # s: 0 resting, 1 winding up, 2 lunging, 3 holding, 4 pulling back, 10 loose; w: pounds taken
			var sx: int = e.h >> 9
			var sy: int = e.h & 511
			var ax := sx * 8 + 4
			var ay := sy * 8 + 4
			var R := 52
			var bcx := ex + 5
			if e.s < 10 and st == GPSLAM and ov(X, Y, 6, 12, sx * 8 + 2, sy * 8 + 3, 4, 5):   # a pound on the stake
				hvy = -700; st = NORM; arcg = GRAV; launch = 0; cut = 0; capok = 1; diveok = 1; stall = 1; spin = 0; throwt = 0
				kick(8); rumble(5); sfx("gp_land"); burst(ax, sy * 8 + 6, 0x8a5a30, 6)
				e.w += 1
				if e.w >= 3:
					e.s = 10; e.vx = face * 420; e.vy = -600; sfx("reveal"); addscore(1000, ax, ay - 8)
					ev.append(["loose", i])
					return
			if e.s == 10:   # loose: bounds along, smashing bricks, harmless to Hatrick
				e.vy += GRAV
				if e.vy > 900: e.vy = 900
				e.y += e.vy; ey = e.y >> 8
				if scan(ex, ey, 10, 10, SOLID) and e.vy > 0:
					ey -= 1
					while scan(ex, ey, 10, 10, SOLID): ey -= 1
					e.y = ey << 8; e.vy = -420
				e.x += e.vx; ex = e.x >> 8
				while scan(ex, ey, 10, 9, 4): smash(htx, hty)
				if scan(ex, ey, 10, 9, SOLID):
					burst(ex + 5, ey + 5, 0x303040, 12); sfx("brick"); kick(6); addscore(500, ex + 5, ey); e.a = 0
					ev.append(["kill", i, "crash"])
					return
				if ey > lh * 8 or ex < -16 or ex > lw * 8 + 16:
					e.a = 0
					return
				esmashall(e, ex, ey, 10, 10)
				return
			var rx := ax + (-11 if bcx < ax else 11)
			var dist := isqrt((X + 3 - ax) * (X + 3 - ax) + (Y + 6 - ay) * (Y + 6 - ay))
			if e.u: e.u -= 1
			if e.s == 0:
				var gx0 := ey   # rest on the ground beside the stake, hopping now and then
				e.vy += GRAV
				if e.vy > 600: e.vy = 600
				e.y += e.vy; ey = e.y >> 8
				if scan(ex, ey, 10, 10, SOLID):
					ey -= 1
					while scan(ex, ey, 10, 10, SOLID) and ey > gx0 - 12: ey -= 1
					e.y = ey << 8; e.vy = 0
				e.x += (rx - bcx) * 32
				if not e.vy and not (fr % 50): e.vy = -260
				if not e.u and alive and dist < R:
					e.s = 1; e.u = 22
			elif e.s == 1:
				e.x += 128 if (fr & 2) else -128
				if not e.u:
					var dx := X + 3 - ax
					var dy := Y + 6 - ay
					var d := isqrt(dx * dx + dy * dy) + 1
					e.vx = (ax + dx * R / d - 5) << 8; e.vy = (ay + dy * R / d - 5) << 8   # where the chain runs out
					e.s = 2; sfx("dive")
			elif e.s == 2:
				var dx: int = e.vx - e.x
				var dy: int = e.vy - e.y
				var d := isqrt((dx >> 8) * (dx >> 8) + (dy >> 8) * (dy >> 8))
				if d <= 6:
					e.x = e.vx; e.y = e.vy; e.s = 3; e.u = 24; kick(3)
				else:
					var nx: int = e.x + dx / d * 6
					var ny: int = e.y + dy / d * 6
					if scan(nx >> 8, ny >> 8, 10, 10, SOLID):
						e.s = 3; e.u = 24; kick(3)
					else:
						e.x = nx; e.y = ny
			elif e.s == 3:
				if not e.u: e.s = 4
			else:
				var dx: int = ((rx - 5) << 8) - e.x
				var dy: int = ((ay - 6) << 8) - e.y
				var d := isqrt((dx >> 8) * (dx >> 8) + (dy >> 8) * (dy >> 8))
				if d <= 2:
					e.s = 0; e.u = 40
				else:
					e.x += dx / d * 2; e.y += dy / d * 2
			ex = e.x >> 8; ey = e.y >> 8
			if ecap(ex, ey, 10, 10):
				ecapback(); sfx("cap_bounce"); burst(ex + 5, ey + 5, 0xffffff, 4)
				if e.s != 0: e.s = 4
			h = ehero(ex + 1, ey + 1, 8, 8)
			if h == 1:   # too tough to stomp
				hvy = -800; st = NORM; arcg = GRAV; capok = 1; diveok = 1; stall = 1; sfx("cap_bounce")
			elif h == 2:
				die()
		T_PUFF:   # s: 0 buzzing, 1 puffed up (u: frames left); w: frames it can't hurt after deflating
			if e.s == 0:
				var ph := (fr + (e.h >> 9)) & 127
				var o := (ph if ph < 64 else 128 - ph) - 32
				e.y = e.h + o * 192; ey = e.y >> 8
				if e.w: e.w -= 1
				if ecap(ex, ey, 8, 8):
					e.s = 1; e.u = 240; ecapback(); sfx("spring"); burst(ex + 4, ey + 4, 0xffa0c8, 6)
					return
				h = ehero(ex + 1, ey + 1, 6, 7)
				if h == 1:
					ekill(e, 0xff80b0, 200); ebounce(k)
				elif h == 2 and not e.w:
					die()
			else:
				if e.vx: e.vx -= 1   # squash after a bounce
				e.u -= 1
				if e.u <= 0:
					e.s = 0; e.w = 40
					return
				var top := ey - 4
				if alive and hvy >= 0 and X + 6 > ex - 4 and X < ex + 12 and Y + 11 >= top and Y + 11 <= top + 8 + (hvy >> 8):
					hy = (top - 11) << 8; hvy = -1300 if (k & 16) else -1100; st = NORM; arcg = GRAV; launch = 0; cut = 0
					capok = 1; diveok = 1; stall = 1; spin = 0; throwt = 0; gnd = 0; coy = 99
					e.vx = 8; sfx("spring"); rumble(2); sparkle(ex + 4, top, 0xffd0e8, 6)
		T_THWOMP:   # s: 0 waiting, 1 falling, 2 landed, 3 rising; u: frames
			var y0: int = e.y
			if e.s == 0:
				if e.u: e.u -= 1
				elif alive and X + 6 > ex - 6 and X < ex + 22 and Y > ey + 8 and Y < ey + 160:
					e.s = 1; e.vy = 0
			elif e.s == 1:
				e.vy = mini(e.vy + 60, 1000)
				var ny: int = (e.y + e.vy) >> 8
				if scan(ex, ny, 16, 16, SOLID):
					while scan(ex, ny, 16, 16, SOLID) and ny > ey: ny -= 1
					e.y = ny << 8; e.s = 2; e.u = 50; e.vy = 0
					if absi(ex - X) < 200:
						kick(9); rumble(3)
					sfx("gp_land"); burst(ex + 2, ny + 16, 0xd0c8b8, 4); burst(ex + 14, ny + 16, 0xd0c8b8, 4)
				else:
					e.y += e.vy
				if (e.y >> 8) > lh * 8:
					e.a = 0
					return
				esmashall(e, ex, e.y >> 8, 16, 16)
			elif e.s == 2:
				e.u -= 1
				if e.u <= 0: e.s = 3
			else:
				e.y -= 128
				if e.y <= e.h:
					e.y = e.h; e.s = 0; e.u = 40
			if exride == me: hy += e.y - y0   # carries whoever stands on it
			ex = e.x >> 8; ey = e.y >> 8; X = hx >> 8; Y = hy >> 8
			if alive and exride != me and ov(X, Y + duck, 6, 11 - duck, ex, ey, 16, 16) and Y + 11 > ey + 6:
				if e.s == 1 and Y + 6 > ey + 8: die()   # flattened
				else:   # it's a stone wall from the side
					hx = (ex - 6 if X + 3 < ex + 8 else ex + 16) << 8; hvx = 0
		T_BEATPLAT:   # s: how many tiles it has moved from home; vx: the way it goes
			var b := beatpos() >> 8
			var target: int = ((b >> 2) & 1) * 2 * e.vx
			if e.s != target:
				var d := 1 if target > e.s else -1
				var tx0: int = (e.x >> 11) + e.s
				var ty: int = e.y >> 11
				var lead: int = tx0 + e.w if d > 0 else tx0 - 1
				var trail: int = tx0 if d > 0 else tx0 + e.w - 1
				if lead >= 0 and lead < lw and not rmget(e.r, lead, ty):
					var on := gnd and Y + 11 == ty * 8 and X + 6 > tx0 * 8 and X < (tx0 + e.w) * 8
					rmset(e.r, lead, ty, 4); rmset(e.r, trail, ty, 0); e.s += d
					if (on or ov(X, Y + duck, 6, 11 - duck, lead * 8, ty * 8, 8, 8)) and not scan(X + d * 8, Y + duck, 6, 11 - duck, SOLID):
						hx += d * 8 * 256
		T_PISTON:   # s: spikes out; u: the beat fraction, for drawing
			var bp := beatpos()
			var b := bp >> 8
			var f := bp & 255
			e.s = 1 if (b & 1) and f < 170 else 0
			e.u = f; e.w = b
			var cx: int = (ex >> 3) + e.vx
			var cy: int = (ey >> 3) + e.vy
			if e.s and alive and ov(X, Y + duck, 6, 11 - duck, cx * 8 + 1, cy * 8 + 1, 6, 6): die()
			if e.s: esmashall(e, cx * 8 + 1, cy * 8 + 1, 6, 6)


# ---------- worlds.c ----------
# Called whenever Hatrick is (re)placed in the level: the start, a checkpoint, a restart.
func worldspawn() -> void:
	tick_ = []; flowertotal = 0; flowerleft = 0; jelt = 0; insand = 0
	for r in L.rooms.size():
		var R: Dictionary = L.rooms[r]
		for y in R.h:
			for x in R.w:
				var c := _grid(R, x, y)
				if (c == 91 or c == 93) and tick_.size() < MAXTICK:   # [ ]
					tick_.append([r, x, y, 1 if c == 93 else 0])
				if c == 102: flowertotal += 1   # f
				if rmget(r, x, y) == WT_FLOWER: flowerleft += 1
	avx = (hx if room == 0 else startx << 11) - (150 << 8)   # the avalanche starts well behind


# Snow: slow going, rolls bog down. Runs early in hero(), from last frame's footing.
func worldbefore(target: int) -> int:
	if not gnd or not scan(hx >> 8, (hy >> 8) + 11, 6, 1, 1 << WT_SNOW):
		return target
	target = target * 9 / 16
	if st == ROLL: hvx = hvx * 95 / 100
	elif st == NORM and absi(hvx) > MAXV * 9 / 16: hvx -= 10 if hvx > 0 else -10
	if absi(hvx) > 120 and not (fr & 7): dust(hx + (3 << 8), hy + (11 << 8), -1 if hvx > 0 else 1, 1)
	return target


# Quicksand, jelly blocks and flowers. Runs at the end of hero(), after the move.
func worldafter(was: int, vy0: int, k: int) -> void:
	var X := hx >> 8
	var Y := hy >> 8
	if st >= TUBE:
		return
	if scan(X, Y + duck, 6, 11 - duck, 1 << WT_SAND):   # quicksand: sinking slowly, a jump gets out
		if not insand:
			draws(6 * 3)
			ev.append(["sand", X + 3, Y + 10])
		insand = 1
		if st == GPSLAM or st == GPWIND or st == DIVE or st == LONGJ or st == SPINJ or st == ROLL or st == SLIDE:
			st = NORM; posture(0)
		if hvy > -8: hvy = -8   # gravity brings it back to a slow sink next frame
		hvx = clampi(hvx, -150, 150)
		gnd = 1; capok = 1; diveok = 1
		if tile((X + 3) >> 3, (Y + 1) >> 3) == WT_SAND: doom()   # sunk over his head
		return
	insand = 0
	if gnd and not was and scan(X, Y + 11, 6, 1, 1 << WT_JELLY) and (vy0 > 700 or st == GPLAND or ((k & 16) and vy0 > 300)):
		var v := 1500 if st == GPLAND else vy0 * 7 / 8 + (250 if k & 16 else 0)   # holding Jump builds up height
		v = 650 if v < 650 else (1300 if st != GPLAND and v > 1300 else v)
		jelx = htx; jely = hty; jelt = 24
		st = NORM; posture(0); hvy = -v; gnd = 0; coy = 99; jn = -1; cut = 0; arcg = GRAV; launch = 0; poundt = 0
		capok = 1; diveok = 1; stall = 1
		if v >= 1300: SPIN(30, face)
		sfx("spring"); rumble(1); sparkle(X + 3, Y + 11, 0xb8ffb0, 6)
		ev.append(["jelly", jelx, jely])
	while scan(X, Y + duck, 6, 11 - duck, 1 << WT_FLOWER):   # flowers: all of a level's together are a big bonus
		setmap(htx, hty, 0); flowerleft -= 1; sfx("coin"); sparkle(htx * 8 + 4, hty * 8 + 4, 0xff7ab0, 10)
		if flowerleft > 0:
			addscore(1000, htx * 8 + 4, hty * 8)
		else:
			addscore(10000, htx * 8 + 4, hty * 8); sfx("moon")
			burst(htx * 8 + 4, hty * 8 + 4, 0xffe040, 12); burst(htx * 8 + 4, hty * 8 + 4, 0xff7ab0, 12)
		ev.append(["flower", flowerleft])


# Clock blocks and the avalanche, once a frame after everything else moved.
func worldtick() -> void:
	if jelt: jelt -= 1
	if tick_.size():
		var bp := beatpos()
		var ph := (bp >> 9) & 1
		tickwarn = 1 if (bp & 511) >= 384 else 0
		for tk in tick_:
			var base: int = WT_TICKB if tk[3] else WT_TICKA
			var r: int = tk[0]
			var x: int = tk[1]
			var y: int = tk[2]
			var cell := rmget(r, x, y)
			if (tk[3] == 0) != (ph == 0):
				rmset(r, x, y, base + 1)
			elif cell != base and not (r == room and st < TUBE and ov(hx >> 8, (hy >> 8) + duck, 6, 11 - duck, x * 8, y * 8, 8, 8)):
				rmset(r, x, y, base)   # never closes on Hatrick: it waits until he is out of the way
				if r == room:
					for ie in wd.en.size():
						var e: En = wd.en[ie]
						if e.a and e.r == r and ov(e.x >> 8, e.y >> 8, 8, 8, x * 8, y * 8, 8, 8): kill(ie)
	if L.avalanche and room == 0 and st < TUBE and not done:
		var stop := (gx * 8 - 56) << 8
		if avx < stop: avx += L.avalanche * 256 / 60
		if hx < avx: doom()
		if not (fr % 3): draws(5)
		if hx - avx < (90 << 8) and not (fr % 30): kick(2)


# Crabs raise their claws now and then: a stomp then hurts (a ground pound or the cap still wins).
func worldclaws(e: En) -> bool:
	return e.t == E_CRAB and (fr + (e.h >> 11) * 37) % 150 < 55


# ---------- collect.h ----------
func cl_record(l: int) -> bool:   # campaign and secret levels keep progress
	return (l >= 0 and l < NLV) or (l >= 0 and l < LV.size() and LV[l].lab == 2)


func cl_base(x: int, y: int) -> int:   # where a pole at column x, top row y meets the ground (px)
	var b := y * 8
	while b < lh * 8 and not scan(x * 8 + 3, b, 1, 1, SOLID): b += 1
	return b


# Every frame, after hazards(): the secret exit and the postcard. (The magnet hat's pull is a perk
# of a hat from the house rack; the remake has no hats yet.)
func cl_tick() -> void:
	var cl: Dictionary = L.cl
	var X := hx >> 8
	var Y := hy >> 8
	if st != WIN: clsecretnow = 0
	if st < TUBE and room == cl.gr and cl.sg and X + 6 > cl.gx * 8 + 2 and X < cl.gx * 8 + 6:   # the secret exit
		var b := cl_base(cl.gx, cl.gy)
		if Y < b:
			gx = cl.gx; gy = cl.gy; gb = b; clsecretnow = 1; touchflag(Y)
	if cl.pc and cl.pr == room and st < TUBE and cl_record(lvl) and not cardgot and ov(X, Y + duck, 6, 11 - duck, cl.px * 8 - 1, cl.py * 8 - 1, 10, 10):   # the postcard
		cardgot = true; sfx("reveal"); rumble(3)
		burst(cl.px * 8 + 4, cl.py * 8 + 4, 0xfff3d1, 10); burst(cl.px * 8 + 4, cl.py * 8 + 4, 0xe8403a, 6)
		addscore(1000, cl.px * 8 + 4, cl.py * 8 - 4)
		ev.append(["postcard"])
