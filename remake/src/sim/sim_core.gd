# The game logic of Hattrick, ported line by line from the C game so the remake moves exactly like
# the tuned 2D game: fixed point (1/256 px), 8 px tiles, y pointing down, 60 steps a second.
# Nothing here draws. The 3D view reads this state, the changed cells in `dirty` and the
# presentation events in `ev` (sounds, particles, score pops).
#
# The port is split the way the C sources are, each file extending the one before:
#   sim_core.gd  hatrick.c, movement.h, cap.h and the shared parts of enemies.h (this file)
#   sim_ext.gd   enemies.h's extra enemies, worlds.c, items.h and collect.h's level side
#   sim_gim.gd   gimmicks.h (platforms, doors that turn rooms, flips, cannons, fans, rising lava)
#   sim.gd       boss.c: the four bosses and two mini-bosses
# The C game calls those parts through hooks (worldbefore(), extenemy(), gim_pre(), bosstick(), ...);
# here they are methods this file declares empty and the later files fill in.
#
# Names follow the C code on purpose (hx, hvy, st, cst, ...) so the two can be compared side by
# side: tools/ctrace.c prints the C simulator's state every frame, tools/xtrace.gd the port's,
# and tools/difftrace.sh diffs them. Particles live in the view, but rnd() is shared with the
# game logic in C, so every particle call here still draws the same random numbers.
extends RefCounted

const W := 256   # the C game's screen, in px: the camera keeps the same framing
const H := 144
const GRAV := 48
const MAXV := 400
const ACC := 18
const AACC := 14
const FRIC := MAXV / 10
const CAP2 := 256
const ANALOG := 512
const ROLLSTART := MAXV * 20 / 14
const ROLLMAX := MAXV * 35 / 14
const ROLLBOOST := MAXV * 6 / 14
const TRIPLE_V := 1220
const LJLATE := 9
const CAPBOUNCE_V := 768
const CAPVAULT_V := CAPBOUNCE_V * 32 / 26
const PADCROUCH := 1 << 25
const GPJUMP_V := 1400
const UPSIDE := 96
const CAPREACH := 2
const MAXHP := 3
# worlds.h tiles: 17 snow, 18 quicksand (not solid), 19 jelly, 20 flower, 21/22 and 30/31 clock blocks
const WT_SNOW := 17
const WT_SAND := 18
const WT_JELLY := 19
const WT_FLOWER := 20
const WT_TICKA := 21
const WT_TICKB := 30
const WORLDSOLID := (1 << WT_SNOW) | (1 << WT_JELLY) | (1 << WT_TICKA) | (1 << WT_TICKB)
# full tiles 1..5, slopes 8..9, tubes 10..12, crumble 13, found hidden block 15, fire bar pivot 16
const SOLID := 0x1BF3E | WORLDSOLID | 0x38000000
const MAXEN := 480
const E_CRAB := 40   # worlds.c
# enemies.h: shy-walker, shell walker, cap thief, cloud rider, chomp stake, puffer, thwomp, beat
# platform, beat piston (map characters "ukvLxpQYZ")
enum { T_SHY = 60, T_SHELL, T_THIEF, T_RIDER, T_CHOMP, T_PUFF, T_THWOMP, T_BEATPLAT, T_PISTON }
# keys (the bits of k): the same as the C game
const K_LEFT := 1
const K_RIGHT := 2
const K_UP := 4
const K_DOWN := 8
const K_JUMP := 16
const K_CAP := 32
const K_RESTART := 64

enum { NORM, LONGJ, GPWIND, GPSLAM, GPLAND, DIVE, SLIDE, ROLL, SPINJ, GSPIN, HANG, CLIMB, TUBE, DEAD, WIN }
enum { CAPFORWARD, CAPUP, CAPDOWN, CAPSPIN }
enum { T_UP, T_DOWN, T_LEFT, T_RIGHT }
enum { MV_NONE, MV_ICE, MV_CONVL, MV_CONVR, MV_POLE, MV_WATER }
enum { POW_NONE, POW_HEAVY, POW_FLUTTER, POW_SEED }
const TC_REDOFF := 24
const TC_BLUEOFF := 25
const TC_POST := 26
const TC_SWITCH := 27
const TC_RED := 28
const TC_BLUE := 29
const MV_ICEBRAKE := 8
const MV_CONVEYOR := 128
const MV_SWIMMAX := MAXV * 3 / 4
const MV_SLIDEACC := 20
const MV_SLIDEDRAG := 14

const SIN: PackedInt32Array = [0,6,13,19,25,31,38,44,50,56,62,68,74,80,86,92,98,104,109,115,121,126,132,137,142,147,152,157,162,167,172,177,181,185,190,194,198,202,206,209,213,216,220,223,226,229,231,234,237,239,241,243,245,247,248,250,251,252,253,254,255,255,256,256,256,256,256,255,255,254,253,252,251,250,248,247,245,243,241,239,237,234,231,229,226,223,220,216,213,209,206,202,198,194,190,185,181,177,172,167,162,157,152,147,142,137,132,126,121,115,109,104,98,92,86,80,74,68,62,56,50,44,38,31,25,19,13,6,0,-6,-13,-19,-25,-31,-38,-44,-50,-56,-62,-68,-74,-80,-86,-92,-98,-104,-109,-115,-121,-126,-132,-137,-142,-147,-152,-157,-162,-167,-172,-177,-181,-185,-190,-194,-198,-202,-206,-209,-213,-216,-220,-223,-226,-229,-231,-234,-237,-239,-241,-243,-245,-247,-248,-250,-251,-252,-253,-254,-255,-255,-256,-256,-256,-256,-256,-255,-255,-254,-253,-252,-251,-250,-248,-247,-245,-243,-241,-239,-237,-234,-231,-229,-226,-223,-220,-216,-213,-209,-206,-202,-198,-194,-190,-185,-181,-177,-172,-167,-162,-157,-152,-147,-142,-137,-132,-126,-121,-115,-109,-104,-98,-92,-86,-80,-74,-68,-62,-56,-50,-44,-38,-31,-25,-19,-13,-6]


class En:   # an enemy: t 1 walker, 2 buzzer (up and down), 3 buzzer (left and right), 40 crab, 60.. enemies.h
	var x := 0
	var y := 0
	var vx := 0
	var vy := 0
	var t := 0
	var a := 0
	var h := 0
	var r := 0
	var s := 0
	var u := 0
	var w := 0
	func copy() -> En:
		var e := En.new()
		e.x = x; e.y = y; e.vx = vx; e.vy = vy; e.t = t; e.a = a; e.h = h; e.r = r; e.s = s; e.u = u; e.w = w
		return e


class Crumble:   # state 0 in place (t: shaking), 1 falling, 2 gone (t: until back)
	var room := 0
	var x := 0
	var y := 0
	var state := 0
	var t := 0
	var fy := 0
	var vy := 0
	var stood := 0
	func copy() -> Crumble:
		var c := Crumble.new()
		c.room = room; c.x = x; c.y = y; c.state = state; c.t = t; c.fy = fy; c.vy = vy; c.stood = stood
		return c


class Check:
	var room := 0
	var x := 0
	var y := 0
	var up := 0
	func copy() -> Check:
		var c := Check.new()
		c.room = room; c.x = x; c.y = y; c.up = up
		return c


class Dweller:   # phase 0 hidden, 1 rising, 2 out, 3 sinking; ofs: px out of the tube
	var phase := 0
	var t := 0
	var ofs := 0
	var a := 0
	func copy() -> Dweller:
		var d := Dweller.new()
		d.phase = phase; d.t = t; d.ofs = ofs; d.a = a
		return d


class Shot:   # a spitter's seed, 1/256 px
	var room := 0
	var x := 0
	var y := 0
	var vx := 0
	var vy := 0
	var a := 0
	func copy() -> Shot:
		var p := Shot.new()
		p.room = room; p.x = x; p.y = y; p.vx = vx; p.vy = vy; p.a = a
		return p


# gimmicks.h. Lifts (kind 1): x, y the plank's top-left, w its width (px), py the pulley's height,
# lo..hi how high and low the first of a pair may go, tilt the pair's summed heights.
# Seesaws (kind 2): x, y the pivot's top, w the plank's half length (px), tilt its slope in 1/256.
class GimPlat:
	var room := 0
	var kind := 0
	var x := 0
	var y := 0
	var w := 0
	var vy := 0
	var tilt := 0
	var link := -1
	var lo := 0
	var hi := 0
	var py := 0
	func copy() -> GimPlat:
		var p := GimPlat.new()
		p.room = room; p.kind = kind; p.x = x; p.y = y; p.w = w; p.vy = vy; p.tilt = tilt; p.link = link
		p.lo = lo; p.hi = hi; p.py = py
		return p


class GimObj:   # a map character's cell; t: used, or a timer
	var room := 0
	var kind := 0
	var x := 0
	var y := 0
	var t := 0
	func copy() -> GimObj:
		var o := GimObj.new()
		o.room = room; o.kind = kind; o.x = x; o.y = y; o.t = t
		return o


class GimBall:
	var room := 0
	var x := 0
	var y := 0
	var vx := 0
	var a := 0
	func copy() -> GimBall:
		var b := GimBall.new()
		b.room = room; b.x = x; b.y = y; b.vx = vx; b.a = a
		return b


class GimWorld:
	var pl: Array = []        # GimPlat
	var ride := 0             # the platform Hatrick stands on, +1
	var ob: Array = []        # GimObj
	var ball: Array = []      # GIM_NB GimBall
	var rise := 0             # the lava's top, 1/256 px
	var risedelay := 0
	var sx := 0               # the auto-scroll camera, 1/256 px
	var xroom := 0            # the one turned or flipped area
	var rot := 0              # its quarter turns after a flip
	var flip := 0
	var flipt := 0            # the hat flip under way
	var flipk := 0
	var fliproom := 0
	var gold := 0             # the gold block on Hatrick's head
	var goldrun := 0
	var goldn := 0
	var rott := 0             # a door's turn in progress
	var pvy := 0              # Hatrick before this frame's step
	var poldy := 0
	func _init() -> void:
		for i in 12: ball.append(GimBall.new())
	func copy() -> GimWorld:
		var g := GimWorld.new()
		g.pl = []; for p in pl: g.pl.append(p.copy())
		g.ob = []; for o in ob: g.ob.append(o.copy())
		g.ball = []; for b in ball: g.ball.append(b.copy())
		g.ride = ride; g.rise = rise; g.risedelay = risedelay; g.sx = sx; g.xroom = xroom; g.rot = rot
		g.flip = flip; g.flipt = flipt; g.flipk = flipk; g.fliproom = fliproom; g.gold = gold
		g.goldrun = goldrun; g.goldn = goldn; g.rott = rott; g.pvy = pvy; g.poldy = poldy
		return g


class World:   # everything a checkpoint saves and a death restores
	var rm: Array = []    # PackedByteArray per room: live tile types, rs[r] cells a row
	var en: Array = []    # En
	var cr: Array = []    # Crumble
	var dw: Array = []    # Dweller, one per tube dweller of the level
	var sh: Array = []    # 8 Shot
	var ck: Array = []    # Check
	var found := 0        # bonus rooms already visited (bit per room)
	var moongot := 0      # moon coins picked up on this visit (bit per coin)
	var gw := GimWorld.new()
	func _init() -> void:
		for i in 8: sh.append(Shot.new())
	func copy() -> World:
		var w := World.new()
		for m in rm: w.rm.append(m.duplicate())
		for e in en: w.en.append(e.copy())
		for c in cr: w.cr.append(c.copy())
		for d in dw: w.dw.append(d.copy())
		w.sh = []; for p in sh: w.sh.append(p.copy())
		for c in ck: w.ck.append(c.copy())
		w.found = found
		w.moongot = moongot
		w.gw = gw.copy()
		return w


# ---------- level data ----------
var LV: Array = []
var NLV := 0
var lvl := 0
var L: Dictionary
var wd := World.new()
var saved: World
var mvs: Array = []   # per room: the surface layer (ice, conveyors, poles, water)

# ---------- hero ----------
var hx := 0; var hy := 0; var hvx := 0; var hvy := 0; var face := 1; var st := NORM; var stt := 0
var gnd := 0; var jn := -1; var landt := 99; var capok := 0; var diveok := 0; var stall := 0
var wall := 0; var coy := 0; var jbuf := 0; var lock := 0; var spin := 0; var cut := 0; var skid := 0
var cst := 0; var cxp := 0; var cyp := 0; var cvx := 0; var cvy := 0; var ct := 0; var cready := 0
var ckind := 0; var throwt := 0; var oldhy := 0
var duck := 0; var catcht := 0; var catchok := 0; var twirl := 0; var gpspin := 0; var rollbuf := 0
var arcg := GRAV; var runt := 0; var rundir := 0; var launch := 0; var boostt := 0; var capbuf := 0
var capkeys := 0; var capextend := 0; var capreflect := 0
var ledget := 0; var climbx := 0; var climby := 0; var slopedir := 0; var poundt := 0
var spinlen := 1; var spind := 1
var hp := MAXHP; var hpt := 99; var healt := 0
var coins := 0; var lcoins := 0; var score := 0; var lscore := 0; var deaths := 0
var prevk := 0; var fr := 0; var tim := 0; var shake := 0
var room := 0; var lw := 1; var lh := 32; var startx := 0; var starty := 0
var gx := 0; var gy := 0; var gb := 0
var haveck := 0; var ckroom := 0; var ckx := 0; var cky := 0; var savedcoins := 0; var savedscore := 0
var left := 0; var timeout := 0; var lstart := 0; var split := 0
var tubefrom := 0; var tubeto := 0; var tubelock := 0; var ride := 0
var wphase := 0; var wt := 0; var tally := 0; var flagy := 0
var finished := false     # the course clear is over: the game moves on
var firstclear := false
var htx := 0; var hty := 0
var _slope := 0
# cap.h
var capx_pow := 0; var capx_inv := 0; var capx_flut := 0; var capx_fuel := 0; var capx_stuck := 0
var capx_pull := 0; var capx_carry := 0; var capx_bend := 0; var capx_pcst := 0; var capx_swt := 0
var capx_pk := 0; var capx_blue := 0
var capx_wait: Array = []
var capone := 0
# movement.h
var mv_swim := 0; var mv_stroke := 0; var mv_flut := 0; var mv_flutok := 0; var mv_jpress := 0
var mv_wslide := 0; var mv_wcoy := 0; var mv_wdir := 0; var mv_wdrop := 0
var mv_swing := 0; var mv_ang := 0; var mv_w := 0; var mv_st := 0; var mv_px := 0; var mv_py := 0
var mv_regrab := 0; var mv_conv := 0; var mv_hvx0 := 0; var mv_slide := 0; var mv_slidek := 0
# presentation only (never read by the logic): camera, as in the C game's camera()
var cxf := 0; var cyf := 0; var look := 0; var camgy := 0
# for the view
var ev: Array = []        # events since the view last looked: [kind, ...]
var dirty: Array = []     # [room, tx, ty] cells whose tile changed
var dirty_all := false    # the whole level changed (load, respawn)
var loads := 0
var rumq := 0
var _rng := 1
var rs := PackedInt32Array()   # per room: cells a row of wd.rm (a room a door turns is stored square)
var done := false              # the run is over (the C game's done): rising lava and the like stop
var assets := "../assets"      # for music.txt (the beat)
var bossnear := 0              # a boss fight is on: the music's danger strings come in
# enemies.h
var capthief := 0   # the enemy (index + 1) flying off with the cap, 0 if none
var exride := 0     # the thwomp (index + 1) Hatrick stands on this frame
var ebpm := 10400   # the current theme's tempo, beats per 100 minutes
var ebpmof := ""
# cap.h: the spitter's seeds Hatrick lobs
var capx_seed: Array = []
# items.h: what is on right now. it_gold: frames left; it_ajump: the shoes' air jump is spent
var it_pie := 0; var it_feather := 0; var it_boom := 0; var it_gold := 0; var it_shoes := 0
var it_ajump := 0; var it_magnet := 0; var it_egg := 0
var it_boomback := 0; var it_pcst := 0; var it_pext := 0; var it_eggx := 0; var it_eggy := 0
var clstall := 0    # collect.h: extra frames of air-throw stall, from the hat being worn
var gim_again_on := false   # gimmicks.h: inside the speed flip's second step


func setup(levels: Array, ncampaign: int, assets_dir := "") -> void:
	LV = levels
	NLV = ncampaign
	if assets_dir != "":
		assets = assets_dir
	for i in 4: capx_seed.append(Shot.new())


func rnd(n: int) -> int:
	_rng = (_rng * 1103515245 + 12345) & 0xffffffff
	return (_rng >> 16) % n


static func brake(v: int, amount: int) -> int:
	return v - amount if v > amount else (v + amount if v < -amount else 0)


static func moveaxis(k: int) -> int:
	if k & 3:
		return (((k >> 1) & 1) - (k & 1)) * 256
	if k & ANALOG:
		return ((k >> 10) & 1023) - 256
	return 0


static func upaim(k: int) -> int:
	return k & ~4 if absi(moveaxis(k)) > UPSIDE else k


func sfx(n: String) -> void:
	ev.append(["sfx", n])


func rumble(k: int) -> void:
	if k > rumq: rumq = k


func kick(n: int) -> void:
	if n > shake: shake = n


# Particles: the C game's part() takes random numbers in its arguments, from the same rnd() as the
# game logic. The view makes its own particles; here only the same count of draws is kept, so
# everything random after them (bosses, clouds) stays in step with the C game.
func draws(n: int) -> void:
	for i in n:
		_rng = (_rng * 1103515245 + 12345) & 0xffffffff


func burst(x: int, y: int, c: int, n: int) -> void:   # x, y in px
	draws(3 * n)
	ev.append(["burst", x, y, c, n])


func sparkle(x: int, y: int, c: int, n: int) -> void:
	draws(2 * n)
	ev.append(["sparkle", x, y, c, n])


func dust(x: int, y: int, d: int, n: int) -> void:     # x, y in 1/256 px
	draws(5 * n)
	ev.append(["dust", x / 256.0, y / 256.0, d, n])


func bubbles(x: int, y: int, n: int) -> void:   # x, y in px (movement.h)
	draws(5 * n)
	ev.append(["bubbles", x, y, n])


func splash(x: int, y: int) -> void:
	draws(40)
	ev.append(["splash", x, y])
	sfx("land")


func addscore(v: int, x: int, y: int) -> void:
	score += v
	ev.append(["score", x, y, v])


func setmap(tx: int, ty: int, t: int) -> void:   # the C game's map[ty][tx] = t
	rmset(room, tx, ty, t)


func rmset(r: int, tx: int, ty: int, t: int) -> void:   # wd.rm[r][ty][tx] = t, and the view hears of it
	var i: int = ty * rs[r] + tx
	if wd.rm[r][i] != t:
		wd.rm[r][i] = t
		dirty.append([r, tx, ty])


func rmget(r: int, tx: int, ty: int) -> int:
	return wd.rm[r][ty * rs[r] + tx]


# ---------- the map ----------
static func tiletype(c: int) -> int:
	match c:
		35: return 1        # #
		66: return 2        # B
		94: return 3        # ^
		83: return 4        # S
		84: return 5        # T
		111: return 6       # o
		47: return 8        # /
		92: return 9        # \
		124: return 10      # |
		45: return 11       # -
		72: return 26       # H
		88: return 27       # X
		82: return 28       # R
		85: return 25       # U
		67: return 13       # C
		63: return 14       # ?
		42, 37: return 16   # * %
		89, 90: return 4    # Y Z
		36, 38: return 16   # $ &
		65, 69, 87: return 4   # A E W
		73, 60, 62: return 1   # I < >
		77: return 12       # M
		115: return WT_SNOW         # s
		113: return WT_SAND         # q
		106: return WT_JELLY        # j
		102: return WT_FLOWER       # f
		91: return WT_TICKA         # [
		93: return WT_TICKB + 1     # ]: the clock starts with the [ blocks solid
	if c >= 48 and c <= 57:
		return 12
	return 0


func tile(x: int, y: int) -> int:
	if x < 0:
		return 4
	if x >= lw or y < 0 or y >= lh:
		return 0
	return wd.rm[room][y * rs[room] + x]


# First tile of a type in mask m under the pixel rect; its position goes to htx/hty.
func scan(x: int, y: int, w: int, h: int, m: int) -> int:
	var map: PackedByteArray = wd.rm[room]
	var stride: int = rs[room]
	for ty in range(y >> 3, ((y + h - 1) >> 3) + 1):
		for tx in range(x >> 3, ((x + w - 1) >> 3) + 1):
			var t: int
			if tx < 0: t = 4
			elif tx >= lw or ty < 0 or ty >= lh: t = 0
			else: t = map[ty * stride + tx]
			if (m >> t) & 1:
				if t == 8 or t == 9:
					var a := x - tx * 8 if x > tx * 8 else 0
					var b := x + w - 1 - tx * 8 if x + w - 1 < tx * 8 + 7 else 7
					var bottom := y + h - 1 - ty * 8 if y + h - 1 < ty * 8 + 7 else 7
					if bottom < ((7 - b) if t == 8 else a):
						continue
				htx = tx
				hty = ty
				return t
	return 0


static func ov(ax: int, ay: int, aw: int, ah: int, bx: int, by: int, bw: int, bh: int) -> bool:
	return ax < bx + bw and bx < ax + aw and ay < by + bh and by < ay + ah


# Surface under the whole foot: ramps have the same occupied pixels as their art. Slope to _slope.
func floorat(x: int, feet: int) -> int:
	var best := 320 * 8 + 99
	_slope = 0
	for px in range(x, x + 6):
		for ty in range((feet - 4) >> 3, ((feet + 5) >> 3) + 1):
			var t := tile(px >> 3, ty)
			var y := ty * 8
			if not ((SOLID >> t) & 1):
				continue
			if t == 8 or t == 9:
				y += (7 - (px & 7)) if t == 8 else (px & 7)
			if y >= feet - 4 and y <= feet + 5 and y < best:
				best = y
				_slope = -1 if t == 8 else (1 if t == 9 else 0)
	return best


func freemove() -> bool:
	return st == NORM or st == LONGJ or st == SPINJ


# hy always keeps the standing body's top, so shrinking leaves the feet in place.
func posture(want: int) -> void:
	if want:
		duck = want
	elif not scan(hx >> 8, hy >> 8, 6, 11, SOLID):
		duck = 0


func roll(speed: int) -> void:
	if face * hvx < speed: hvx = face * speed
	st = ROLL; stt = 0; boostt = 15; arcg = GRAV; posture(5); spin = 0; throwt = 0; cut = 0
	rollbuf = 0; capbuf = 0; poundt = 0


func longjump() -> void:
	st = LONGJ
	if face * hvx < 700: hvx = face * 700
	hvy = -560; posture(0); cut = 0; spin = 0; gnd = 0; coy = 99; jn = -1; launch = 0


func SPIN(n: int, d: int) -> void:
	spin = n; spinlen = n; spind = d


func capthrow(k: int, downthrow: bool, rolling: bool, takeoff: int) -> void:
	if k & 4: ckind = CAPUP
	elif downthrow or st == GPLAND: ckind = CAPDOWN
	elif st == SPINJ or st == GSPIN: ckind = CAPSPIN
	else: ckind = CAPFORWARD
	cst = 1; ct = 0; cready = 0; capextend = 0; capreflect = 0; cvx = 0; cvy = 0
	cxp = hx - 256 + face * (9 << 8); cyp = hy + (duck + 3) * 256
	if ckind == CAPUP:
		cxp = hx + 256; cyp = hy + (duck - 6) * 256; cvy = -1100
	elif downthrow:
		cxp = hx + 256; cyp = hy + (13 << 8); cvy = 1100
	elif ckind != CAPSPIN:
		cvx = face * 1100
	if (ckind == CAPDOWN and gnd) or rolling:
		cyp = hy + (7 << 8)
	capreflect = 1 if rolling else 0
	var i := 0
	while i < 16 and scan(cxp >> 8, cyp >> 8, 8, 4, SOLID):
		if cvy: cyp -= 256 if cvy > 0 else -256
		else: cxp -= face * 256
		i += 1
	if scan(cxp >> 8, cyp >> 8, 8, 4, SOLID): cst = 3
	if not gnd and st != SPINJ:
		st = NORM; spin = 0; cut = 0
		# Preserve an actual jump launch; a same-frame throw must not pin it to the floor.
		if stall and not takeoff:
			stall = 0; throwt = 8 + clstall
	sfx("cap_throw")
	ev.append(["throw", ckind])


# ---------- hearts, deaths ----------
func hurt() -> bool:   # a heart takes the hit. Pits and the timer always count.
	if (hy >> 8) > lh * 8 or not left or hp <= 1:
		return false
	hp -= 1; hpt = 0; capx_inv = 90; it_shoes = 0
	if hvy > -600: hvy = -600
	burst((hx >> 8) + 3, (hy >> 8) + 2, 0xff4a5a, 10); sfx("cap_bounce"); rumble(5); kick(6)
	ev.append(["hurt"])
	return true


var dieforce := false
func die() -> void:
	if not dieforce and (it_shield() or capx_absorb() or it_eggsave() or (st < TUBE and hurt())):
		return
	if st < TUBE:
		st = DEAD; stt = 0; hvy = -900; hvx = 0; hp = 0; hpt = 0; deaths += 1
		sfx("death"); rumble(7); kick(9)
		ev.append(["death"])


# ---------- items.h: what the shop's items do in a level (all off until one is used) ----------
func hpmax() -> int:
	return MAXHP + it_pie


func it_level() -> void:   # a new level starts with nothing on
	it_pie = 0; it_feather = 0; it_boom = 0; it_gold = 0; it_shoes = 0; it_ajump = 0; it_magnet = 0
	it_egg = 0; it_boomback = 0


func it_spawn() -> void:   # going down loses the pie, the gold cap, the shoes and the egg
	it_pie = 0; it_gold = 0; it_shoes = 0; it_ajump = 0; it_egg = 0; it_boomback = 0; it_pcst = 0
	it_eggx = hx; it_eggy = hy


func it_shield() -> bool:   # the gold cap shrugs off anything a fall or the timer doesn't settle
	return it_gold != 0 and st < TUBE and (hy >> 8) <= lh * 8 and left != 0


func it_eggsave() -> bool:   # the egg takes the hit and breaks
	if not it_egg or st >= TUBE or (hy >> 8) > lh * 8 or not left:
		return false
	it_egg = 0; capx_inv = 90
	if hvy > -600: hvy = -600
	burst((it_eggx >> 8) + 3, (it_eggy >> 8) + 3, 0xfff8e8, 10); burst((it_eggx >> 8) + 3, (it_eggy >> 8) + 3, 0x5ac85a, 6)
	sfx("brick"); rumble(4); kick(4)
	ev.append(["egg"])
	return true


func it_glide(k: int) -> void:   # hero(), right after the flutter: the feather cap's glide
	if gnd:
		it_ajump = 0
		if it_feather: capx_fuel = 96
		return
	if not it_feather or st != NORM or not (k & 16) or mv_swim or capx_flut or hvy <= 300:
		return
	hvy = 300
	if not (fr & 7): draws(2)


func it_airjump() -> bool:   # a new Jump press in the air: the spring shoes' extra jump
	if not it_shoes or it_ajump or not freemove() or mv_swim:
		return false
	it_ajump = 1; jbuf = 0; throwt = 0; twirl = 0
	st = NORM; hvy = -960; arcg = GRAV; cut = 1; jn = -1; launch = 0; posture(0)
	SPIN(30, face); sfx("spring"); rumble(1)
	ev.append(["jump", "spring"])
	return true


func it_captick() -> void:   # a new throw, or an extended one, flies out again first
	if (cst == 1 and not it_pcst) or (capextend and not it_pext): it_boomback = 0
	it_pcst = cst; it_pext = capextend


func it_boomturn() -> bool:
	if not it_boom or it_boomback:
		return false
	it_boomback = 1
	return true


func it_boomhome() -> bool:
	if not it_boomback:
		return false
	var dx := hx + 256 - cxp
	var dy := hy + (duck + 3) * 256 - cyp
	var m := maxi(absi(dx), absi(dy))
	if m < 1024:
		cst = 0; catcht = 10; it_boomback = 0; sfx("cap_catch")
		ev.append(["catch"])
		return true
	cxp += dx * 900 / m; cyp += dy * 900 / m
	return true


func it_capslow() -> int:   # how fast a thrown cap slows down
	return 36 if it_boom else 72


func it_moonreach() -> int:   # the magnet's reach for moon coins: px added on every side of Hatrick
	return 20 if it_magnet else 0


func it_tick() -> void:   # every frame in a level, after cl_tick()
	var X := hx >> 8
	var Y := hy >> 8
	var alive := st < TUBE
	if it_gold and st < DEAD:
		it_gold -= 1
		if it_gold == 120: sfx("hurry")
		if not (fr & 3): draws(2)
		if alive:
			for e in wd.en:   # running into an enemy knocks it out
				if ekillable(e) and ov(X - 1, Y + duck - 1, 8, 13 - duck, (e.x >> 8) + 1, (e.y >> 8) + 1, 6, 7): ekill(e, 0xffd84a, 200)
	if it_magnet and alive:   # coins within three tiles fly in
		var cx := X + 3
		var cy := Y + 5
		for ty in range((cy - 24) >> 3, ((cy + 24) >> 3) + 1):
			for tx in range((cx - 24) >> 3, ((cx + 24) >> 3) + 1):
				if tile(tx, ty) == 6:
					setmap(tx, ty, 0); coins += 1; sfx("coin"); addscore(100, tx * 8 + 4, ty * 8)
					ev.append(["coin", room, tx, ty])
	if it_egg:   # the egg waddles along behind Hatrick
		var tx := hx - face * (11 << 8)
		var ty := hy + (4 << 8)
		it_eggx += (tx - it_eggx) / 6; it_eggy += (ty - it_eggy) / 6


func doom() -> void:   # lava, rising lines, avalanches, quicksand, squeezes: no hat power soaks it up
	dieforce = true; die(); dieforce = false


func smash(tx: int, ty: int) -> void:
	setmap(tx, ty, 0)
	ev.append(["smash", room, tx, ty])
	burst(tx * 8 + 4, ty * 8 + 4, 0xd0602a, 6); sfx("brick"); rumble(3); kick(5)
	addscore(50, tx * 8 + 4, ty * 8)


func crumbleat(r: int, x: int, y: int) -> Crumble:
	for c in wd.cr:
		if c.room == r and c.x == x and c.y == y:
			return c
	return null


# ---------- the live level ----------
# The level as the file describes it, before anything happened.
func build() -> void:
	L = LV[lvl]
	wd = World.new()
	mvs = []
	rs = PackedInt32Array()
	for r in L.rooms.size():
		var R: Dictionary = L.rooms[r]
		var stride: int = R.w
		if r > 0 and R.w <= 320 and R.grid.has(68):   # D: a door can turn this room a quarter turn
			stride = maxi(R.w, R.h)
		rs.append(stride)
		var m := PackedByteArray()
		m.resize(stride * (stride if stride != R.w else R.h))
		var sl := PackedByteArray()
		sl.resize(R.w * R.h)
		for y in R.h:
			for x in R.w:
				var c: int = R.grid[y * R.w + x]
				if c == 0:
					continue
				m[y * stride + x] = tiletype(c)
				if r == 0 and c == 64: startx = x; starty = y
				if r == 0 and c == 70: gx = x; gy = y
				if (c == 103 or c == 98 or c == 104) and wd.en.size() < MAXEN:   # g b h (extended enemies share the list)
					var n := En.new()
					n.x = x << 11; n.y = y << 11; n.h = y << 11; n.vx = -100; n.vy = 0
					n.t = 1 if c == 103 else (2 if c == 98 else 3); n.a = 1; n.r = r
					if n.t == 3: n.vx = n.x - sweep(n.h >> 9)   # a sweeper's vx holds the left end of its patrol
					wd.en.append(n)
				if c == 99 and wd.en.size() < MAXEN:   # c: a crab (worlds.c)
					var n := En.new()
					n.x = x << 11; n.y = y << 11; n.vx = -170; n.t = E_CRAB; n.a = 1; n.h = y << 11; n.r = r
					wd.en.append(n)
				extspawn(r, x, y, c)
				if c == 67 and wd.cr.size() < 960:
					var cr := Crumble.new(); cr.room = r; cr.x = x; cr.y = y
					wd.cr.append(cr)
				if c == 75 and wd.ck.size() < 64:
					var ck := Check.new(); ck.room = r; ck.x = x; ck.y = y
					wd.ck.append(ck)
				sl[y * R.w + x] = MV_ICE if c == 73 else MV_CONVL if c == 60 else MV_CONVR if c == 62 else MV_POLE if c == 61 else MV_WATER if c == 119 else 0
		wd.rm.append(m)
		mvs.append(sl)
	for i in L.homes.size():   # staggered
		var d := Dweller.new()
		d.phase = 0; d.t = 40 + i * 53 % 100; d.ofs = 0; d.a = 1
		wd.dw.append(d)
	room = 0; lw = L.rooms[0].w; lh = L.rooms[0].h
	gb = gy * 8
	while gb < lh * 8 and not scan(gx * 8 + 3, gb, 1, 1, SOLID):
		gb += 1
	for r in L.rooms.size():   # movement.h mv_build: water fills down to the ground
		var R: Dictionary = L.rooms[r]
		var sl: PackedByteArray = mvs[r]
		for x in R.w:
			for y in range(1, R.h):
				if sl[(y - 1) * R.w + x] == MV_WATER and not sl[y * R.w + x] and not ((SOLID >> rmget(r, x, y)) & 1):
					sl[y * R.w + x] = MV_WATER
	gim_build()


func sweep(ph: int) -> int:
	ph = (ph + 1) & 127
	return (ph if ph < 64 else 128 - ph) * 96


# Puts Hatrick in area r at (x, y) (1/256 px) with a fresh state: level start, checkpoint or tube.
func spawn(r: int, x: int, y: int) -> void:
	room = r; lw = L.rooms[r].w; lh = L.rooms[r].h
	hx = x; hy = y
	hvx = 0; hvy = 0; st = NORM; stt = 0; jn = 0; cst = 0; lock = 0; spin = 0; skid = 0; fr = 0; gnd = 0
	jbuf = 0; coy = 0; wall = 0; cut = 0; capok = 0; diveok = 0; stall = 0; cready = 0; throwt = 0
	duck = 0; catcht = 0; catchok = 0; twirl = 0; gpspin = 0; rollbuf = 0; cvy = 0; ckind = 0
	arcg = GRAV; runt = 0; rundir = 0; launch = 0; boostt = 0; capbuf = 0; capkeys = 0; capextend = 0; capreflect = 0
	ledget = 0; climbx = 0; climby = 0; slopedir = 0; poundt = 0
	mv_swim = 0; mv_stroke = 0; mv_flut = 0; mv_flutok = 0; mv_wslide = 0; mv_wcoy = 0; mv_wdir = 0; mv_wdrop = 0
	mv_swing = 0; mv_ang = 0; mv_w = 0; mv_st = 0; mv_regrab = 0; mv_conv = 0; mv_slide = 0; mv_slidek = 0
	hp = MAXHP; hpt = 99; healt = 0
	it_spawn()
	loads += 1
	bossstart()
	capx_reset()
	gim_restart()
	jn = -1; face = 1; landt = 99
	cxf = hx - ((W / 2 - 3) << 8); cyf = hy - ((H * 5 / 8) << 8); look = 0; camgy = hy
	worldspawn()
	_clamp_camera()
	dirty_all = true
	ev.append(["spawn"])


func load_level(l: int = -1) -> void:   # (re)start the level from the top
	if l >= 0:
		lvl = l
		lcoins = coins
		lscore = score
		it_level()
	build()
	haveck = 0; left = L.time * 60; timeout = 0; lstart = tim; finished = false
	wphase = 0; wt = 0; tally = 0
	spawn(0, startx << 11, (starty - 1) << 11)
	coins = lcoins; score = lscore


func respawn() -> void:   # after a death: from the last checkpoint touched, else from the top
	if not haveck:
		load_level()
		return
	wd = saved.copy(); coins = savedcoins; score = savedscore
	if timeout:
		left = L.time * 60; timeout = 0
	spawn(ckroom, ckx, cky)


# ---------- hooks: the later files of the port fill these in ----------
func extspawn(_r: int, _x: int, _y: int, _c: int) -> void: pass           # enemies.h
func extenemy(_i: int, _k: int) -> void: pass
func extride() -> bool: return false
func worldspawn() -> void: pass                                          # worlds.c
func worldbefore(target: int) -> int: return target
func worldafter(_was: int, _vy0: int, _k: int) -> void: pass
func worldtick() -> void: pass
func worldclaws(_e: En) -> bool: return false
func cl_tick() -> void: pass                                             # collect.h
func gim_build() -> void: pass                                           # gimmicks.h
func gim_restart() -> void: pass
func gim_pre(_k: int, _pr: int) -> bool: return false
func gim_post(_k: int, _pr: int) -> void: pass
func gim_again(_k: int) -> void: pass
func gim_camera() -> void: pass
func bossstart() -> void: pass                                           # boss.c
func bosstick() -> void: pass
func bosscam() -> void: pass


# ---------- tubes ----------
func tubeexit(t: Dictionary) -> Array:   # where the way out ends, and its direction: [x, y, vx, vy]
	var x: int = t.x * 8
	var y: int = t.y * 8
	var vx := 0
	var vy := 0
	match t.dir:
		T_UP: x = t.x * 8 + 5; y = t.y * 8 - 11; vy = -160
		T_DOWN: x = t.x * 8 + 5; y = t.y * 8 + 8; vy = 160
		T_LEFT: x = t.x * 8 - 7; y = t.y * 8 + 5; vx = -128
		T_RIGHT: x = t.x * 8 + 9; y = t.y * 8 + 5; vx = 128
	return [x, y, vx, vy]


func entertube(i: int) -> void:
	var t: Dictionary = L.tubes[i]
	st = TUBE; stt = 0; tubefrom = i; tubeto = t.link
	hvx = 0; hvy = 0; spin = 0; throwt = 0; duck = 0; cst = 0; gnd = 0
	if t.dir == T_UP or t.dir == T_DOWN: hx = (t.x * 8 + 5) << 8
	sfx("tube")


func tubemove() -> void:
	var a: Dictionary = L.tubes[tubefrom]
	var b: Dictionary = L.tubes[tubeto]
	if stt < 20:   # in: the reverse of the mouth's way out
		var o := tubeexit(a)
		hx -= o[2]; hy -= o[3]
	else:
		var o := tubeexit(b)
		if stt == 20:   # inside the far mouth, out of sight
			if b.room != room:
				room = b.room; lw = L.rooms[room].w; lh = L.rooms[room].h
				if room and not ((wd.found >> room) & 1):
					wd.found |= 1 << room; addscore(2000, o[0] + 3, o[1] - 4); sfx("reveal")
				ev.append(["room", room])
			hx = (o[0] << 8) - o[2] * 20; hy = (o[1] << 8) - o[3] * 20; sfx("tube")
			if o[2]: face = 1 if o[2] > 0 else -1
			cxf = hx - ((W / 2 - 3) << 8); cyf = hy - ((H * 5 / 8) << 8); camgy = hy
			_clamp_camera()
			ev.append(["cut"])
		hx += o[2]; hy += o[3]
		while scan(hx >> 8, hy >> 8, 6, 11, 4) == 2:
			smash(htx, hty)
	stt += 1
	if stt == 40:
		st = NORM; coy = 99; jn = -1; landt = 99; tubelock = tubeto + 1


func tubecheck(k: int, d: int, X: int, Y: int, vy0: int) -> bool:   # enter a tube this frame?
	if st != NORM and st != SPINJ:
		return false
	for i in L.tubes.size():
		var t: Dictionary = L.tubes[i]
		if t.room != room or t.link < 0:
			continue
		var c := X + 3
		var mid: bool = c >= t.x * 8 + 3 and c <= t.x * 8 + 13
		var at := false
		var push := false
		match t.dir:
			T_UP: at = gnd and Y + 11 == t.y * 8 and mid; push = (k & 8) != 0
			T_DOWN: at = not gnd and vy0 < 0 and Y + duck == t.y * 8 + 8 and mid; push = (k & 4) != 0
			T_LEFT: at = gnd and X + 6 == t.x * 8 and Y + 11 == t.y * 8 + 16; push = d > 0
			_: at = gnd and X == t.x * 8 + 8 and Y + 11 == t.y * 8 + 16; push = d < 0
		if tubelock == i + 1:   # just came out here: the way back in opens once Hatrick moves off or lets go
			if not at or not push:
				tubelock = 0
			continue
		if at and push:
			entertube(i)
			return true
	return false


# ---------- the course clear ----------
func touchflag(Y: int) -> void:
	var top := gy * 8
	var h := gb - top
	var f := (gb - (Y + 11)) * 100 / h if h > 0 else 100
	st = WIN; stt = 0; hvx = 0; hvy = 0; hx = (gx * 8 - 3) << 8; sfx("clear"); rumble(8)
	split = tim - lstart; wphase = 0; wt = 0; flagy = top + 1
	tally = left / 60
	addscore(5000 if f >= 90 else 2000 if f >= 65 else 800 if f >= 40 else 400 if f >= 20 else 100, gx * 8 + 8, Y)
	ev.append(["flag", f])


func win(pr: int) -> void:
	stt += 1
	if stt > 6 and (pr & 16) and not finished:
		score += tally * 50; tally = 0; finished = true
		ev.append(["skipclear"])
		return
	if wphase == 0:   # slide down with the flag
		var down: bool = not scan(hx >> 8, (hy >> 8) + 1, 6, 11, SOLID) and (hy >> 8) + 11 < gb
		if down: hy += 256
		if flagy < gb - 9: flagy += 1
		if not down and flagy >= gb - 9:
			wphase = 1; wt = 0; face = 1
			if not scan(gx * 8 + 6, hy >> 8, 6, 11, SOLID): hx = (gx * 8 + 6) << 8
	elif wphase == 1:   # pose
		wt += 1
		if wt == 40: wphase = 2; wt = 0
	elif wphase == 2:   # count the time bonus in
		if not tally:
			wphase = 3; wt = 0; sfx("bonus")
		else:
			wt += 1
			if not (wt & 1):
				var n := tally / 15 if tally > 30 else 1
				tally -= n; score += n * 50; sfx("tick")
	elif wphase == 3:
		wt += 1
		if wt == 45: finished = true


# ---------- the hero ----------
func hero(k: int, pr: int) -> void:
	var axis := moveaxis(k)
	var dir := 1 if axis > 0 else (-1 if axis < 0 else 0)
	var target := absi(axis) * MAXV / 256
	var D := (k >> 3) & 1
	var U := (k >> 2) & 1
	var g := GRAV
	var X: int
	var Y: int
	var takeoff := 0
	var rollcancel: bool = st == ROLL and not D
	var cappress := pr & 32
	var downthrow: bool = D and (pr & CAP2) and not (prevk & 32) and not (k & PADCROUCH)
	if st == DEAD:
		hvy += GRAV; hy += hvy; stt += 1
		if stt > 60: respawn()
		return
	if st == WIN:
		win(pr)
		return
	if st == TUBE:
		tubemove()
		return
	pr = mv_pre(k, pr)
	if pr < 0:
		return
	if ledget: ledget -= 1
	if st == HANG:
		hvx = 0; hvy = 0
		if (pr & 8) or dir == -face:
			st = NORM; ledget = 15; coy = 99; pr &= ~8
		elif pr & (4 | 16):
			if not scan(climbx >> 8, climby >> 8, 6, 11, SOLID):
				st = CLIMB; stt = 0
		if st == HANG:
			return
	if st == CLIMB:
		hvx = 0; hvy = 0
		hx += (climbx - hx) / (12 - stt); hy += (climby - hy) / (12 - stt)
		stt += 1
		if stt == 12:
			hx = climbx; hy = climby; st = NORM; gnd = 1; coy = 0; landt = 0
		return
	target = worldbefore(target)   # snow
	if lock: lock -= 1; dir = 0
	if runt: runt -= 1
	if launch: launch -= 1
	if boostt: boostt -= 1
	if capbuf: capbuf -= 1
	if poundt: poundt -= 1
	if gnd and not D and dir and dir * hvx >= 150:
		runt = 10; rundir = dir
	if cappress and (not D or downthrow):
		if not cst or (cst == 3 and not (pr & CAP2)):
			capbuf = 10; capkeys = upaim(k) & (4 | 8 | CAP2)
	if gnd and (pr & upaim(k) & 4) and not (pr & 16) and (st == NORM or st == GSPIN):
		if st == NORM: sfx("spin")
		st = GSPIN; stt = 0
	# Late in the window only a still-held direction counts: letting go and pressing Down is a ground pound.
	if not gnd and launch and (pr & 8) and not (k & 32) and (dir or (runt and launch >= LJLATE - 5)):
		face = dir if dir else rundir
		longjump(); takeoff = 1; sfx("longjump")
	jbuf = 6 if (pr & 16) else (jbuf - 1 if jbuf else 0)
	if gnd:
		arcg = GRAV; coy = 0; capok = 1; diveok = 1; stall = 1; catchok = 1; throwt = 0; twirl = 0
	else:
		coy += 1
	if landt < 99: landt += 1
	if spin: spin -= 1
	if skid: skid -= 1
	if catcht: catcht -= 1
	if twirl: twirl -= 1
	if rollbuf: rollbuf -= 1
	if (st == GPWIND or st == GPSLAM or st == GPLAND) and D and cappress: rollbuf = 6

	if st == GPWIND:
		posture(4); hvx = 0; hvy = 0; g = 0
		stt += 1
		if stt > 14: st = GPSLAM
	elif st == GPSLAM:
		hvx = 0; g = 0; hvy = 1400
	elif st == GPLAND:
		hvx = 0; stt += 1
		if rollbuf:
			roll(MAXV * 30 / 14 if gpspin else ROLLSTART)
		elif jbuf and stt >= 5:
			jbuf = 0; hvy = -GPJUMP_V; st = NORM; posture(0); SPIN(40, face)
			cut = 0; gnd = 0; coy = 99; jn = -1; launch = 0; poundt = 0; takeoff = 1; sfx("jump3")
		elif stt >= 30 or (stt >= 24 and dir):
			st = NORM
	elif st == ROLL:
		posture(5)
		if D and cappress and not boostt:
			boostt = 15
			var speed := absi(hvx) + ROLLBOOST
			hvx = face * (ROLLMAX if speed > ROLLMAX else speed); sfx("roll")
			ev.append(["boost"])
		if gnd:
			hvx = hvx * 998 / 1000 if D else brake(hvx, FRIC)
		if jbuf and coy < 6:
			jbuf = 0
			if rollcancel and cappress:
				st = NORM; hvy = -870; posture(0); gnd = 0; coy = 99; cut = 1; jn = 0; launch = LJLATE
			else:
				longjump()
			takeoff = 1; sfx("jump" if (rollcancel and cappress) else "longjump")
		elif not D:
			st = NORM; posture(0)
	elif st == GSPIN:
		posture(0); hvx = hvx * 95 / 100
		target = absi(hvx) if absi(hvx) > MAXV * 8 / 14 else MAXV * 8 / 14
		if dir:
			hvx += axis * AACC / 256
			if absi(hvx) > target: hvx = (1 if hvx > 0 else -1) * target
			face = dir
		if jbuf and coy < 6:
			jbuf = 0; st = SPINJ; hvy = -560; cut = 0; launch = 0; gnd = 0; coy = 99; jn = -1; takeoff = 1; sfx("spin")
		else:
			stt += 1
			if stt >= 90 or D or not gnd: st = NORM
	elif st == SLIDE and mv_slidetick(dir):   # the slope slide (movement.h)
		pass
	elif st == SLIDE:
		posture(5)
		if gnd and D:
			roll(absi(hvx) if absi(hvx) > ROLLSTART else ROLLSTART)
		else:
			hvx = brake(hvx, FRIC)
			if absi(hvx) < 100:
				st = NORM; hvx = 0; posture(0)
			elif gnd and dir:   # directional input regains ground control
				st = NORM; posture(0)
			if jbuf and gnd:
				jbuf = 0; st = NORM; hvy = -760; posture(0); cut = 0; gnd = 0; coy = 99; sfx("jump")
	elif st != DIVE:
		posture(4 if (gnd and D) else 0)
		if gnd:
			st = NORM; gpspin = 0
			if D: target = absi(axis) * 128 / 256
			if dir:
				if dir * hvx < 0:
					if not D and dir * hvx < -150:
						if not skid: sfx("skid")
						skid = 8
					hvx += dir * FRIC
				elif dir * hvx < target:
					hvx += dir * ACC
					if dir * hvx > target: hvx = dir * target
				elif dir * hvx > target:
					hvx -= dir * (FRIC if D else 12)
					if dir * hvx < target: hvx = dir * target
				face = dir
			else:
				hvx = brake(hvx, FRIC)
		elif (st == NORM or st == SPINJ) and dir:
			if dir * hvx < target:
				hvx += dir * (AACC * 2 if dir * hvx < 0 else AACC)
				if target < MAXV and dir * hvx > target: hvx = dir * target
			face = dir
		if jbuf and coy < 6:
			var js := "jump"
			jbuf = 0; coy = 99; cut = 1; spin = 0; gnd = 0; posture(0); takeoff = 1; launch = LJLATE
			if poundt and not D and not U:
				hvy = -GPJUMP_V; cut = 0; poundt = 0; jn = -1; SPIN(40, face); js = "jump3"
			elif upaim(k) & 4:
				st = SPINJ; hvy = -560; jn = -1; cut = 0; launch = 0; js = "spin"
			elif D and (dir or runt or absi(hvx) > 150):
				if dir: face = dir
				elif runt: face = rundir
				longjump(); js = "longjump"
			elif D:
				hvy = -1060; arcg = 32; hvx = -face * 200; SPIN(40, -face); jn = -1; cut = 0; launch = 0; js = "flip"
				ev.append(["backflip"])
			elif skid and dir:
				face = dir; hvx = dir * 260; hvy = -1020; arcg = 32; SPIN(40, dir); jn = -1; cut = 0; launch = 0; js = "flip"
				ev.append(["sideflip"])
			elif catcht:
				hvy = -900; arcg = 42; catcht = 0; jn = -1; js = "jump2"
			else:
				# A stationary double is valid; only the third jump needs forward speed.
				jn = jn + 1 if (landt <= 10 and jn >= 0 and jn < 2 and (jn == 0 or face * hvx > 200)) else 0
				hvy = -TRIPLE_V if jn == 2 else (-1030 if jn else -870)
				if jn == 2:
					SPIN(44, face); arcg = 32; cut = 0; launch = 0
				js = "jump3" if jn == 2 else ("jump2" if jn else "jump")
			sfx(js)
			ev.append(["jump", js])
		elif jbuf and wall and not gnd:
			jbuf = 0; hvx = -wall * 440; hvy = -900; face = -wall; lock = 7; cut = 1; jn = -1; spin = 0
			st = NORM; arcg = GRAV; launch = 0; posture(0); capok = 1; diveok = 1; stall = 1; catchok = 1
			throwt = 0; twirl = 0; sfx("wall_jump"); rumble(1)
			dust(hx + (3 << 8) + wall * (3 << 8), hy + (6 << 8), -wall, 4)
			ev.append(["walljump", wall])
		elif jbuf and catcht and catchok and not gnd:
			jbuf = 0; catcht = 0; catchok = 0; st = NORM; spin = 0; cut = 0
			hvy = -320; arcg = 26; throwt = 0; twirl = 10; stall = 1; launch = 0; sfx("spin")
		elif (pr & 16) and not gnd and not takeoff and it_airjump():   # items.h: the spring shoes
			pass
		elif not takeoff and (pr & 8) and not gnd and not (k & 32):
			gpspin = 1 if st == SPINJ else 0; st = GPWIND; stt = 0; throwt = 0; twirl = 0; SPIN(14, face); sfx("gp_spin")
	if cappress or ((pr & 8) and (k & 32)):
		if gnd and D and (st == NORM or st == SLIDE):
			roll(ROLLSTART); sfx("roll")
		elif D and not downthrow and diveok and not gnd and not (st == GPSLAM and scan(hx >> 8, (hy >> 8) + 11, 6, 7, SOLID)) and (freemove() or st == GPWIND or st == GPSLAM):
			st = DIVE; posture(0)
			if face * hvx < 760: hvx = face * 760
			if hvy > -420: hvy = -420
			g = GRAV; arcg = GRAV; launch = 0; capbuf = 0; diveok = 0; spin = 0; throwt = 0; twirl = 0; cut = 0; sfx("dive")
		elif cappress and cst and cst < 3 and (not D or downthrow):
			if pr & CAP2:
				cst = 3   # the second throw button explicitly recalls the cap
			elif not capextend and ckind != CAPSPIN:
				# One append throw per flight. Aim at the nearest enemy in front, or extend forward.
				var dx := 0 if cvy else face * 1100
				var dy := ((1100 if cvy > 0 else -1100) if cvy else 0)
				var best := 64 * 64
				for e in wd.en:
					if e.a and e.r == room:
						var ex: int = (e.x - cxp) >> 8
						var ey: int = (e.y - cyp) >> 8
						var d := ex * ex + ey * ey
						if d < best and ex * face >= -4:
							best = d
							var scale := maxi(absi(ex), absi(ey))
							if scale:
								dx = ex * 1100 / scale; dy = ey * 1100 / scale
				cvx = dx; cvy = dy; cst = 1; ct = 0; capextend = 1; sfx("cap_throw")
	if capbuf and not cst and (freemove() or st == GSPIN or (st == GPLAND and not D)):
		var ck := capkeys
		capbuf = 0
		capthrow(ck, (ck & (8 | CAP2)) == (8 | CAP2), rollcancel, takeoff)
	# Odyssey gives both long jumps and dives a 0.5 u/frame counter-input brake.
	if (st == LONGJ or st == DIVE) and dir * hvx < 0:
		var minimum := MAXV * 5 / 28
		var speed := absi(hvx) - AACC
		hvx = (1 if hvx > 0 else -1) * (minimum if speed < minimum else speed)
	if st == NORM: g = arcg
	if st == LONGJ: g = 34
	if st == SPINJ: g = 13
	if hvy < 0 and cut and not (k & 16):
		g *= 2
	elif st == NORM and cut and (k & 16) and hvy > -200 and hvy < 200:
		g = g * 5 / 8
	if throwt and st == NORM:
		throwt -= 1; hvy = 0; g = 0
	hvy += g
	capx_flutter(k, pr)
	it_glide(k)   # items.h: the feather cap
	if st != GPSLAM:
		if hvy > 1100: hvy = 1100   # wall slides brake in mv_move()
	mv_move(k, dir, g)

	if gnd and st == NORM and D and slopedir and not takeoff:
		face = slopedir; roll(absi(hvx) if absi(hvx) > 200 else 200)
	if gnd and st == ROLL and slopedir:
		hvx += slopedir * 18
		if absi(hvx) > ROLLMAX: hvx = (1 if hvx > 0 else -1) * ROLLMAX
		if hvx: face = 1 if hvx > 0 else -1
	hx += hvx; X = hx >> 8; Y = hy >> 8
	if gnd and hvy >= 0:
		var fl := floorat(X, Y + 11)
		if fl <= Y + 16 and (_slope or slopedir) and not scan(X, fl - 11 + duck, 6, 11 - duck, SOLID):
			hy = (fl - 11) * 256; Y = hy >> 8
	if scan(X, Y + duck, 6, 11 - duck, SOLID):   # in a wall: out to the nearest free spot close by, back the way he came first
		var s := -1 if hvx > 0 else 1
		var nx := X
		for d in range(1, 13):
			if nx != X: break
			if not scan(X + s * d, Y + duck, 6, 11 - duck, SOLID): nx = X + s * d
			elif not scan(X - s * d, Y + duck, 6, 11 - duck, SOLID): nx = X - s * d
		X = nx; hx = X << 8; hvx = 0
	var was := gnd
	var vy0 := hvy
	var top0 := Y + duck
	gnd = 0
	hy += hvy; Y = hy >> 8
	if vy0 < 0:   # hidden blocks: only a bump from below finds them
		var ty := (Y + duck) >> 3
		for tx in range(X >> 3, ((X + 5) >> 3) + 1):
			if tile(tx, ty) == 14 and top0 >= ty * 8 + 8:
				setmap(tx, ty, 15); coins += 1
				ev.append(["reveal", room, tx, ty])
				addscore(1100, tx * 8 + 4, ty * 8 - 4); sfx("reveal"); sfx("coin")
	if st == GPSLAM:
		while scan(X, Y + duck, 6, 12 - duck, 4) == 2: smash(htx, hty)
		capx_heavy(X, Y + duck, 12 - duck)
	if hvy < 0:
		while scan(X, Y + duck, 6, 11 - duck, 4) == 2:
			smash(htx, hty); hvy = 0
	if scan(X, Y + duck, 6, 11 - duck, SOLID):
		var s := -1 if hvy > 0 else 1
		var guard := 0
		Y += s
		while scan(X, Y + duck, 6, 11 - duck, SOLID) and guard < 2600:
			Y += s; guard += 1
		hy = Y << 8
		if hvy > 0: gnd = 1
		if hvy < 0: ev.append(["bonk"])
		hvy = 0
	elif hvy >= 0 and scan(X, Y + 11, 6, 1, SOLID):
		gnd = 1; hy = Y << 8; hvy = 0
	ride = 0
	if not gnd and hvy >= 0:   # riding a falling crumble block
		for i in wd.cr.size():
			var c: Crumble = wd.cr[i]
			if c.room == room and c.state == 1 and X + 6 > c.x * 8 and X < c.x * 8 + 8 and Y + 11 >= (c.fy >> 8) and Y + 11 <= (c.fy >> 8) + 6 + (c.vy >> 8):
				hy = c.fy - (11 << 8); Y = hy >> 8; gnd = 1; hvy = 0; ride = i + 1
				break
	if not gnd and hvy >= 0 and extride():   # a thwomp (enemies.h)
		Y = hy >> 8
	if not gnd:   # a cap stuck on a post
		var ny := capx_stand(X, Y)
		if ny != -99999:
			Y = ny; gnd = 1
	slopedir = 0
	if gnd:
		for tx in range(X >> 3, ((X + 5) >> 3) + 1):
			if tile(tx, (Y + 12) >> 3) == 13:
				var c := crumbleat(room, tx, (Y + 12) >> 3)
				if c and not c.state: c.stood = 1   # objects() shakes it next frame
		floorat(X, Y + 11); slopedir = _slope; launch = 0
		capok = 1; diveok = 1; stall = 1; catchok = 1; throwt = 0; twirl = 0
		if not was:
			landt = 0
			if st == GPSLAM and mv_slopepound():
				pass
			elif st == GPSLAM:
				st = GPLAND; stt = 0; poundt = 31; kick(10); sfx("gp_land"); rumble(6)
				ev.append(["pound"])
				if rollbuf: roll(MAXV * 30 / 14 if gpspin else ROLLSTART)
			elif (st == DIVE or st == LONGJ) and D:
				roll(absi(hvx) if absi(hvx) > ROLLSTART else ROLLSTART)
			elif st == DIVE:
				st = SLIDE; posture(5)
			elif st == LONGJ or st == SPINJ:
				st = NORM
			if vy0 > 900 and st != GPLAND:
				rumble(2); sfx("land"); kick(5 if vy0 > 1300 else 0)
			ev.append(["land", vy0])
		if scan(X, Y + 11, 6, 1, 32):
			hvy = -1500; gnd = 0; st = NORM; arcg = GRAV; launch = 0; posture(0); coy = 99; jbuf = 0
			SPIN(30, face); cut = 0; sfx("spring"); rumble(5); sparkle(hx / 256 + 3, hy / 256 + 11, 0xfff0a8, 8)
			ev.append(["spring", room, htx, hty])
	wall = 0
	if not gnd:
		wall = 1 if scan(X + 6, Y + duck + 1, 1, 9 - duck, SOLID) else (-1 if scan(X - 1, Y + duck + 1, 1, 9 - duck, SOLID) else 0)
	if not gnd and not ledget and wall and dir == wall and hvy >= 0 and st == NORM and not duck:
		var side := X + 6 if wall > 0 else X - 1
		var tx := side >> 3
		for ty in range((Y - 2) >> 3, ((Y + 6) >> 3) + 1):
			var t := tile(tx, ty)
			var top := ty * 8
			var edge := tx * 8 if wall > 0 else (tx + 1) * 8
			# A hanging body sits below the lip. Keep two tiles on its side clear so
			# snapping into HANG cannot put its feet inside the lower step of stairs.
			var airx := edge - 8 if wall > 0 else edge
			if (t == 1 or t == 2 or t == 4) and top >= Y - 2 and top <= Y + 6 and not scan(side, top - 11, 1, 11, SOLID) and not scan(airx, top, 8, 16, SOLID):
				face = wall; hx = (edge - 6 if wall > 0 else edge) * 256; hy = (top + 2) * 256
				climbx = (edge + 1 if wall > 0 else edge - 7) * 256; climby = (top - 11) * 256
				st = HANG; hvx = 0; hvy = 0; spin = 0; throwt = 0; launch = 0; jn = -1; sfx("ledge")
				break
	worldafter(was, vy0, k)   # quicksand, jelly, flowers
	if st == DEAD:
		return
	while scan(X, Y + duck, 6, 11 - duck, 64):
		setmap(htx, hty, 0); coins += 1; sfx("coin")
		ev.append(["coin", room, htx, hty])
		addscore(100, htx * 8 + 4, hty * 8); sparkle(htx * 8 + 4, hty * 8 + 4, 0xffe066, 6)
	if scan(X - 1, Y + duck + 2, 8, 8 - duck, 8) or scan(X + 1, Y + duck - 1, 4, 13 - duck, 8) or Y > lh * 8 + 8:
		die()
	if room == 0 and X + 6 > gx * 8 + 2 and X < gx * 8 + 6 and Y < gb and st < TUBE:
		touchflag(Y)
	elif st < TUBE and not lock:
		tubecheck(k, dir, X, Y, vy0)


# ---------- the cap ----------
func capupd(k: int) -> void:
	if capx_tick(k):
		return
	it_captick()
	if not cst:
		return
	if ckind == CAPSPIN and cst < 3:
		ct += 1
		var a := ct * 256 / 24
		var nx: int = hx + 256 + SIN[(a + 64) & 255] * 14
		var ny: int = hy + (5 << 8) + SIN[a & 255] * 5
		if not scan(nx >> 8, ny >> 8, 8, 4, SOLID):
			cxp = nx; cyp = ny
		if ct >= 24: cst = 3
	elif cst == 1 and it_boomhome():   # items.h: the boomerang cap flying home
		pass
	elif cst == 1:
		var nx := cxp + cvx
		var ny := cyp + cvy
		var slow := it_capslow()
		if scan(nx >> 8, ny >> 8, 8, 4, SOLID):
			if capreflect and cvx:
				cvx = -cvx; capreflect = 0
			elif not it_boomturn():
				cst = 2; ct = 0
		else:
			cxp = nx; cyp = ny
			if cvx: cvx -= slow if cvx > 0 else -slow
			if cvy: cvy -= slow if cvy > 0 else -slow
			if absi(cvx) < 100 and absi(cvy) < 100 and not it_boomturn():
				cst = 2; ct = 0
	elif cst == 2:
		if ct < 24: ct += 1
		if ct == 24 and not (k & 32): cst = 3
	else:
		var dx := hx + 256 - cxp
		var dy := hy + (duck + 3) * 256 - cyp
		cxp += clampi(dx, -1000, 1000)
		cyp += clampi(dy, -1000, 1000)
		if absi(dx) < 1024 and absi(dy) < 1024:
			cst = 0; catcht = 10; sfx("cap_catch")
			ev.append(["catch"])
	if cst and cst < 3:
		while scan(cxp >> 8, cyp >> 8, 8, 4, 64):
			setmap(htx, hty, 0)
			ev.append(["coin", room, htx, hty])
			capx_scoop(htx, hty); sparkle(htx * 8 + 4, hty * 8 + 4, 0xffe066, 6)
	var touching := ov(hx >> 8, (hy >> 8) + duck, 6, 11 - duck, cxp >> 8, cyp >> 8, 8, 5)
	if not touching: cready = 1
	var landing: bool = hvy >= 0 and oldhy + (11 << 8) <= cyp + 256 and hy + (11 << 8) >= cyp
	var diving: bool = st == DIVE and hy < cyp + (2 << 8)
	var vault: bool = gnd and st == NORM and cst == 2 and (k & 32)
	if ckind != CAPSPIN and cst and cst < 3 and cready and (capok or vault) and (freemove() or st == DIVE) and touching and (vault or (not gnd and (landing or diving))):
		if not gnd: capok = 0
		elif face * hvx < 700: hvx = face * 700
		diveok = 1; stall = 1; hvy = -CAPVAULT_V if vault else -CAPBOUNCE_V; arcg = 32; launch = 0; cut = 0
		st = NORM; posture(0); gnd = 0; jn = -1; spin = 0; throwt = 0; twirl = 0; coy = 99; cst = 3
		burst((cxp >> 8) + 4, cyp >> 8, 0xffffff, 5); sfx("cap_bounce"); rumble(1)
		ev.append(["capbounce", (cxp >> 8) + 4, cyp >> 8])


# cap.h: posts, switches, the in-flight bend, coins riding home, hat powers, blinking after a hit.
# spawn(): a fresh Hatrick has no power, no carried coins and no cap on a post. The red / blue
# state is read back from the map, which a checkpoint restores.
func capx_reset() -> void:
	capx_pow = 0; capx_inv = 0; capx_flut = 0; capx_fuel = 0; capx_stuck = 0; capx_pull = 0
	capx_carry = 0; capx_bend = 0; capx_pcst = 0; capx_swt = 0; capx_blue = 0; capx_wait = []
	for p in capx_seed: p.a = 0
	for r in L.rooms.size():
		for y in L.rooms[r].h:
			for x in L.rooms[r].w:
				var t := rmget(r, x, y)
				if t == TC_BLUE or t == TC_REDOFF:
					capx_blue = 1
					return


func capx_inside(x: int, y: int) -> bool:
	return ov(hx >> 8, (hy >> 8) + duck, 6, 11 - duck, x * 8, y * 8, 8, 8)


func capx_waiting(force: bool) -> void:   # a waiting block comes on once he's clear (force: now)
	var i := 0
	while i < capx_wait.size():
		var cx: int = capx_wait[i][0]
		var cy: int = capx_wait[i][1]
		var t := rmget(room, cx, cy)
		var on := (1 if not capx_blue else 0) if t == TC_REDOFF else ((1 if capx_blue else 0) if t == TC_BLUEOFF else -1)
		if on > 0 and not force and capx_inside(cx, cy):
			i += 1
			continue
		if on > 0:
			setmap(cx, cy, TC_RED if t == TC_REDOFF else TC_BLUE)
		capx_wait[i] = capx_wait[capx_wait.size() - 1]
		capx_wait.pop_back()


func capx_toggle() -> void:   # the cap hit a switch: red and blue blocks swap, in every area
	capx_waiting(true)
	capx_blue ^= 1
	for r in L.rooms.size():
		for y in L.rooms[r].h:
			for x in L.rooms[r].w:
				var t := rmget(r, x, y)
				var n := t
				if t == TC_RED: n = TC_REDOFF
				elif t == TC_REDOFF: n = TC_RED
				elif t == TC_BLUE: n = TC_BLUEOFF
				elif t == TC_BLUEOFF: n = TC_BLUE
				if r == room and (n == TC_RED or n == TC_BLUE) and capx_wait.size() < 16 and capx_inside(x, y):
					n = TC_REDOFF if n == TC_RED else TC_BLUEOFF
					capx_wait.append([x, y])
				rmset(r, x, y, n)
	sfx("reveal"); rumble(3)
	ev.append(["switch", capx_blue])


const CAPX_COL := [0, 0x9a48d0, 0xffd23c, 0xe0586a]   # the cap's colour with each power
func capx_gain(p: int, x: int, y: int) -> void:   # the cap knocked out an enemy that carries power p
	if not p:
		return
	capx_pow = p; capx_flut = 0
	burst(x, y, CAPX_COL[p], 10); burst((hx >> 8) + 3, (hy >> 8) + 2, CAPX_COL[p], 8); sfx("reveal")
	ev.append(["power", p])


func capx_enemy(e: En) -> void:   # walkers give the heavy pound, buzzers the flutter
	capx_gain(POW_HEAVY if e.t == 1 else (POW_FLUTTER if e.t == 2 or e.t == 3 else POW_NONE), (e.x >> 8) + 4, (e.y >> 8) + 4)


# die() asks first: a worn power takes the hit, and Hatrick blinks for a moment. Pits and the
# level timer always count.
func capx_absorb() -> bool:
	if st >= TUBE or (hy >> 8) > lh * 8 or not left:
		return false
	if capx_inv:
		return true
	if not capx_pow:
		return false
	burst((hx >> 8) + 3, (hy >> 8) + 2, CAPX_COL[capx_pow], 12)
	capx_pow = 0; capx_flut = 0; capx_inv = 90
	if hvy > -600: hvy = -600
	sfx("cap_bounce"); rumble(2)
	ev.append(["power", 0])
	return true


func capx_scoop(tx: int, ty: int) -> void:   # a coin rides home on the cap
	capx_carry += 1; sfx("coin"); burst(tx * 8 + 4, ty * 8 + 4, 0xffd84a, 3)


func capx_flutter(k: int, pr: int) -> void:   # the buzzer's power
	if gnd:
		capx_fuel = 48; capx_flut = 0
		return
	if capx_pow != POW_FLUTTER or st != NORM or mv_swim:
		capx_flut = 0
		return
	if (pr & 16) and hvy > -200 and capx_fuel: capx_flut = 1
	if not capx_flut:
		return
	if not (k & 16) or not capx_fuel:
		capx_flut = 0
		return
	capx_fuel -= 1; hvy -= GRAV + (60 if hvy > 0 else 24)   # brakes a fall fast, then a gentle lift
	if hvy < -240: hvy = -240
	if not (capx_fuel & 3): draws(2)


func capx_heavy(X: int, top: int, h: int) -> void:
	if capx_pow != POW_HEAVY:
		return
	if scan(X, top, 6, h, 16) == 4: shake = 6


func capx_stand(X: int, Y: int) -> int:   # feet coming down onto a cap stuck on a post: the new Y, or -99999
	if cst != 4 or capx_pull or hvy < 0:
		return -99999
	var cx := cxp >> 8
	var cy := cyp >> 8
	if X + 6 <= cx or X >= cx + 8 or (oldhy >> 8) + 11 > cy + 1 or Y + 11 < cy:
		return -99999
	hy = (cy - 11) << 8; hvy = 0
	return cy - 11


func capx_lob() -> void:   # the spitter's seed, lobbed forward with each throw
	for p in capx_seed:
		if not p.a:
			p.room = room; p.x = hx + (3 << 8) + face * (4 << 8); p.y = hy + (3 << 8)
			p.vx = face * 560 + hvx / 2; p.vy = -460; p.a = 1
			sfx("spit")
			return


func capx_seeds() -> void:   # seeds knock out enemies and tube dwellers and smash bricks
	for p in capx_seed:
		if not p.a:
			continue
		if p.room != room:
			p.a = 0
			continue
		p.vy += 22; p.x += p.vx; p.y += p.vy
		var sx: int = p.x >> 8
		var sy: int = p.y >> 8
		var t := tile(sx >> 3, sy >> 3)
		if sy > lh * 8 + 16:
			p.a = 0
			continue
		if t == 2 and sx >= 0:
			smash(sx >> 3, sy >> 3); p.a = 0
			continue
		if (SOLID >> t) & 1:
			p.a = 0; burst(sx, sy, 0x9a6a3a, 3)
			continue
		for i in wd.en.size():
			if not p.a: break
			var e: En = wd.en[i]
			if e.a and e.r == room and ov(sx - 3, sy - 3, 6, 6, e.x >> 8, e.y >> 8, 8, 8):
				kill(i); p.a = 0
		for i in L.homes.size():
			if not p.a: break
			var d: Dweller = wd.dw[i]
			var h: Dictionary = L.homes[i]
			if d.a and h.room == room and dwellerhit(d, h, sx - 2, sy - 2, 4, 4):
				var sp := dwellerspot(h)
				d.a = 0; p.a = 0; burst(sp[0], sp[1], 0xe0586a if h.spit else 0x2f8f9a, 10); sfx("stomp"); rumble(4)
				addscore(500, sp[0], sp[1])
				ev.append(["dweller", i])


# capupd() calls this first, every frame. Returns true when it moved the cap itself (on a post).
func capx_tick(k: int) -> bool:
	var pr := k & ~capx_pk
	capx_pk = k
	if capx_inv: capx_inv -= 1
	if capx_swt: capx_swt -= 1
	if capx_wait.size(): capx_waiting(false)
	capx_seeds()
	if cst == 1 and not capx_pcst:   # a new throw
		capx_bend = 0
		if capx_pow == POW_SEED: capx_lob()
	if not cst and capx_carry:   # the cap is home with its coins
		coins += capx_carry; addscore(100 * capx_carry, (hx >> 8) + 3, (hy >> 8) - 2); sfx("coin"); capx_carry = 0
	if cst == 4:   # on a post: a platform until it times out, or a grapple
		var cx := (cxp >> 8) + 1
		var cy := cyp >> 8
		if (pr & 32) and not capx_pull and (freemove() or st == DIVE or st == SLIDE or st == ROLL or st == GSPIN):
			capx_pull = 30; st = NORM; posture(0); spin = 0; throwt = 0; twirl = 0; sfx("cap_throw")
		if capx_pull:
			var dx := cx - (hx >> 8)
			var dy := cy - 11 - (hy >> 8)
			var m := maxi(absi(dx), absi(dy))
			capx_pull -= 1
			if capx_pull == 0 or m < 6 or st != NORM:
				capx_pull = 0; cst = 3; capok = 1; diveok = 1
				if st == NORM and hvy > -700: hvy = -700
			else:
				hvx = dx * 1000 / m; hvy = dy * 1000 / m; gnd = 0; coy = 99; jn = -1
		else:
			capx_stuck -= 1
			if capx_stuck <= 0: cst = 3
		capx_pcst = cst
		return true
	if cst == 1:
		var v := upaim(k) & 12
		if ckind == CAPFORWARD and v and v != 12:   # Up / Down bend a forward throw (a sideways-held Up doesn't)
			if not capx_bend: capx_bend = 1 if cvx >= 0 else -1
			var s := (1 if (k & 8) else -1) * capx_bend
			var vx := cvx
			cvx -= s * cvy * 26 / 256; cvy += s * vx * 26 / 256
		var nx := (cxp + cvx) >> 8
		var ny := (cyp + cvy) >> 8
		if not capx_swt and scan(nx, ny, 8, 4, 1 << TC_SWITCH):
			capx_toggle(); capx_swt = 12; burst(htx * 8 + 4, hty * 8 + 4, 0xffffff, 6)
		if ckind != CAPSPIN and scan(nx, ny, 8, 4, 1 << TC_POST):   # caught on a post
			cxp = (htx * 8) << 8; cyp = (hty * 8 + 1) << 8; cvx = 0; cvy = 0
			cst = 4; capx_stuck = 120; capx_pull = 0; capx_pcst = cst
			burst(htx * 8 + 4, hty * 8 + 2, 0xfff0b0, 5); sfx("cap_catch"); rumble(1)
			return true
	capx_pcst = cst
	return false


# ---------- movement.h ----------
func mvsurf(tx: int, ty: int) -> int:
	if tx < 0 or tx >= lw or ty < 0 or ty >= lh:
		return 0
	var w: int = L.rooms[room].w   # the surface layer never turns with a room (as in the C game)
	if tx >= w or ty >= L.rooms[room].h:
		return 0
	return mvs[room][ty * w + tx]


func mvwater(x: int, y: int) -> bool:
	return mvsurf(x >> 3, y >> 3) == MV_WATER


func mvfeet(X: int, Y: int) -> int:
	var best := 0
	for tx in range(X >> 3, ((X + 5) >> 3) + 1):
		var s := mvsurf(tx, (Y + 12) >> 3)
		if s == MV_ICE:
			return s
		if s == MV_CONVL or s == MV_CONVR:
			best = s
	return best


func mv_swingtick(k: int, pr: int) -> void:
	var m := moveaxis(k)
	var d := 1 if m > 0 else (-1 if m < 0 else 0)
	mv_st += 1
	if d == face and mv_w < 2600: mv_w += 14
	elif d == -face and mv_w > 700: mv_w -= 20
	mv_ang = (mv_ang + mv_w) & 0xffff
	var a := (-(mv_ang >> 8) if face > 0 else (mv_ang >> 8)) & 255
	hx = mv_px * 256 - SIN[a] * 7 - 3 * 256
	hy = mv_py * 256 + SIN[(a + 64) & 255] * 7 - (11 << 7)
	hvx = 0; hvy = 0; gnd = 0; coy = 99; jbuf = 0; st = NORM; posture(0)
	capok = 1; diveok = 1; stall = 1; catchok = 1; throwt = 0; twirl = 0
	if not (mv_st & 15): sfx("spin")
	if pr & (16 | 8):
		mv_swing = 0; mv_regrab = 18; arcg = GRAV; launch = 0; cut = 0; jn = -1
		if pr & 16:
			var t := mini(mv_st, 150)
			var sp := 520 + t * 4 + (mv_w - 700) / 6
			hvx = face * mini(sp, 1150); hvy = -mini(760 + t * 3, 1240)
			SPIN(36, face); sfx("jump3"); rumble(2)
			sparkle(mv_px, mv_py, 0xfff0a8, 6)


func mv_pre(k: int, pr: int) -> int:
	var X := hx >> 8
	var Y := hy >> 8
	mv_hvx0 = hvx; mv_jpress = pr & 16
	if st != SLIDE: mv_slide = 0
	if mv_regrab: mv_regrab -= 1
	if mv_stroke: mv_stroke -= 1
	if mv_wcoy: mv_wcoy -= 1
	if mv_wdrop: mv_wdrop -= 1
	if mv_swing:
		mv_swingtick(k, pr)
		return -1 if mv_swing else 0
	if st == HANG or st == CLIMB:
		mv_wslide = 0
		return pr
	if not gnd and not mv_regrab and not mv_swim and (freemove() or st == DIVE):
		var tx := (X + 3) >> 3
		var ty := (Y + 1) >> 3
		if mvsurf(tx, ty) == MV_POLE or mvsurf(tx, (Y + 4) >> 3) == MV_POLE:
			if mvsurf(tx, ty) != MV_POLE: ty = (Y + 4) >> 3
			mv_swing = 1; mv_st = 0; mv_px = tx * 8 + 4; mv_py = ty * 8 + 4; mv_wslide = 0; mv_flut = 0
			var d := moveaxis(k)
			if d: face = 1 if d > 0 else -1
			elif hvx: face = 1 if hvx > 0 else -1
			mv_w = mini(800 + (absi(hvx) + absi(hvy)) / 2, 1800)
			mv_ang = 0; spin = 0; cst = 3 if (cst == 1 or cst == 2) else cst
			sfx("ledge"); rumble(1)
			mv_swingtick(k, 0)
			return -1
	var wet := mvwater(X + 3, Y + 6)
	if wet and not mv_swim:
		if hvy > 300:
			splash(X + 3, Y + 6); bubbles(X + 3, Y + 8, 5)
		if hvy > 0: hvy /= 3
		if st == LONGJ or st == SPINJ or st == DIVE or st == GPWIND or st == GPSLAM or mv_slide:
			st = NORM; posture(0)
		mv_flut = 0; mv_wslide = 0; mv_slide = 0
	elif not wet and mv_swim and hvy < 0:
		splash(X + 3, Y + 10)
	mv_swim = 1 if wet else 0
	if mv_swim:
		if st == LONGJ or st == SPINJ or st == DIVE or st == GPWIND or st == GPSLAM:
			st = NORM; spin = 0
		diveok = 0
		if pr & 16:   # a stroke; near the surface it leaps out
			var top := not mvwater(X + 3, Y - 3)
			hvy = -900 if top else (-430 if hvy > -200 else hvy - 230)
			if hvy < -900: hvy = -900
			gnd = 0; coy = 99; jbuf = 0; cut = 0; mv_stroke = 12; st = NORM
			sfx("jump" if top else "dive"); bubbles(X + 3, Y + 10, 3)
		pr &= ~16
		if not gnd: pr &= ~8   # no ground pound under water: Down sinks faster
		if not (fr & 31): bubbles(X + 3 + face * 2, Y + 1, 1)
	if mv_wslide and (pr & 8):
		pr &= ~8; mv_wslide = 0; mv_wdrop = 12; mv_wcoy = 0
	if not gnd and not wall and mv_wcoy and (pr & 16) and st == NORM:
		wall = mv_wdir
	return pr


func mv_move(k: int, dir: int, g: int) -> void:
	var X := hx >> 8
	var Y := hy >> 8
	var D := (k >> 3) & 1
	if gnd:
		mv_flutok = 0; mv_flut = 0
	if twirl == 10: mv_flutok = 1
	if mv_swim:
		hvy -= g
		if not gnd:
			hvy += 26 if D else 10
			var sink := 460 if D else 180
			if hvy > sink: hvy = hvy - 40 if hvy - 40 > sink else sink
			if not dir: hvx = hvx * 15 / 16
		hvx = clampi(hvx, -MV_SWIMMAX, MV_SWIMMAX)
		if cst == 1 and ckind == CAPFORWARD and mvwater((cxp >> 8) + 4, (cyp >> 8) + 2):
			if absi(cvx) > 300: cvx += 48 if cvx > 0 else -48
			if not (fr & 3): bubbles((cxp >> 8) + 4, (cyp >> 8) + 2, 1)
		mv_wslide = 0
		return
	if not gnd and st == NORM and mv_flutok and not twirl and mv_jpress and not wall and hvy > 0 and not capx_flut:
		mv_flutok = 0; mv_flut = 44; sfx("spin")
	if mv_flut:
		if gnd or st != NORM or not (k & 16):
			mv_flut = 0
		else:
			mv_flut -= 1
			var want := -110 if mv_flut > 20 else 70
			hvy -= g
			hvy += 30 if hvy < want else (-30 if hvy > want else 0)
			if not (fr & 3): dust(hx + (3 << 8), hy + (11 << 8), 0, 1)
	var was := mv_wslide
	mv_wslide = wall if (not gnd and wall and st == NORM and hvy > 0 and not duck and not mv_wdrop and (dir == wall or (was == wall and dir != -wall))) else 0
	if mv_wslide and hvy > 200: hvy = hvy - 100 if hvy - 100 > 200 else 200
	if was and not mv_wslide and not gnd:
		mv_wcoy = 6; mv_wdir = was
	if mv_wslide:
		mv_wdir = mv_wslide; mv_flut = 0; capx_flut = 0
	var surf := mvfeet(X, Y) if gnd else 0
	if surf == MV_ICE and (st == NORM or st == SLIDE or st == ROLL or st == GSPIN):
		var dv := hvx - mv_hvx0
		if absi(hvx) < absi(mv_hvx0) or hvx * mv_hvx0 < 0:
			dv = clampi(dv, -MV_ICEBRAKE, MV_ICEBRAKE)
		elif st == NORM:
			dv = dv / 2 + (dv & 1)
		hvx = mv_hvx0 + dv
		if dir and dir * hvx < -150 and not (fr & 3): dust(hx + (3 << 8), hy + (11 << 8), 1 if hvx > 0 else -1, 1)
	if surf == MV_CONVL or surf == MV_CONVR:
		mv_conv = -MV_CONVEYOR if surf == MV_CONVL else MV_CONVEYOR
		hx += mv_conv
	elif not gnd and mv_conv:
		hvx += mv_conv; mv_conv = 0
	else:
		mv_conv = 0


func mv_slopepound() -> bool:
	if not slopedir or rollbuf or mv_swim:
		return false
	st = SLIDE; stt = 0; mv_slide = 1; mv_slidek = 0; face = slopedir; hvx = slopedir * MAXV; posture(5)
	kick(6); sfx("gp_land"); rumble(6); dust(hx + (3 << 8), hy + (11 << 8), -face, 4)
	ev.append(["pound"])
	return true


func mv_slidetick(dir: int) -> bool:
	if not mv_slide:
		return false
	posture(5); stt += 1
	var s := 1 if hvx > 0 else (-1 if hvx < 0 else slopedir)
	var stuck: bool = not hvx and stt > 4
	if gnd and slopedir:
		hvx += slopedir * MV_SLIDEACC
		if absi(hvx) > ROLLMAX: hvx = (1 if hvx > 0 else -1) * ROLLMAX
	elif gnd:
		hvx = brake(hvx, MV_SLIDEDRAG)
	if gnd and dir and dir * hvx < 0: hvx = brake(hvx, MV_SLIDEDRAG)
	if hvx: face = 1 if hvx > 0 else -1
	if jbuf and coy < 6:
		jbuf = 0; st = NORM; mv_slide = 0; hvy = -870; posture(0); cut = 1; gnd = 0; coy = 99; jn = -1; launch = LJLATE; sfx("jump")
	elif stuck or (gnd and absi(hvx) < 120 and (not slopedir or s != slopedir) and stt > 4):
		st = NORM; mv_slide = 0; posture(0)
	return true


func mv_slidehits() -> void:   # whatever the slide runs into is knocked out, more points for each one in a row
	if not mv_slide or st != SLIDE:
		return
	var X := hx >> 8
	var Y := hy >> 8
	for o in wd.en:
		if ekillable(o) and ov(X, Y + duck, 6, 11 - duck, (o.x >> 8) + 1, (o.y >> 8) + 1, 6, 7):
			ekill(o, 0x9a48d0, 200 << mini(mv_slidek, 3), "slide")
			mv_slidek += 1


# ---------- enemies ----------
func kill(i: int) -> void:   # a stomp or anything else that knocks out a plain enemy
	var e: En = wd.en[i]
	e.a = 0; burst((e.x >> 8) + 4, (e.y >> 8) + 4, 0x9a48d0, 8); sfx("stomp"); rumble(4); kick(5)
	addscore(200, (e.x >> 8) + 4, e.y >> 8)
	ev.append(["kill", i, "stomp"])


# enemies.h's shared helpers
func ekill(e: En, c: int, pts: int, how := "stomp") -> void:
	if e.t == T_THIEF and capthief == wd.en.find(e) + 1: ecapback()
	e.a = 0; burst((e.x >> 8) + 4, (e.y >> 8) + 4, c, 8); sfx("stomp"); rumble(4); kick(5)
	addscore(pts, (e.x >> 8) + 4, e.y >> 8)
	ev.append(["kill", wd.en.find(e), how])


func capstolen() -> bool:   # the base enemy loop leaves the cap harmless while a bird carries it
	if not capthief or capthief > wd.en.size():
		return false
	var e: En = wd.en[capthief - 1]
	return e.a != 0 and e.t == T_THIEF and e.s == 2 and cst != 0


func ecapback() -> void:   # the cap flies home
	capthief = 0
	if cst:
		cst = 3; ct = 0


func ehero(x: int, y: int, w: int, h: int) -> int:   # Hatrick against a box: 0 apart, 1 a stomp from above, 2 any other touch
	var X := hx >> 8
	var Y := hy >> 8
	if st >= TUBE or not ov(X, Y + duck, 6, 11 - duck, x, y, w, h):
		return 0
	return 1 if (hvy > 0 or st == GPSLAM) and Y + 11 < y + h * 3 / 4 else 2


func ebounce(k: int) -> void:
	hvy = -1000 if (k & 16) else -650; st = NORM; arcg = GRAV; launch = 0; cut = 0
	capok = 1; diveok = 1; stall = 1; spin = 0; throwt = 0


# the cap already hit an enemy this frame: one at a time, so it can't take out two of a stack
func ecap(x: int, y: int, w: int, h: int) -> bool:
	if not capone and cst and cst < 3 and not capstolen() and ov((cxp >> 8) - CAPREACH, (cyp >> 8) - CAPREACH, 8 + 2 * CAPREACH, 5 + 2 * CAPREACH, x, y, w, h):
		capone = 1
		return true
	return false


func capbox(x: int, y: int, w: int, h: int) -> bool:   # ov(CAPBOX, ...)
	return ov((cxp >> 8) - CAPREACH, (cyp >> 8) - CAPREACH, 8 + 2 * CAPREACH, 5 + 2 * CAPREACH, x, y, w, h)


func ekillable(o: En) -> bool:
	return o.a != 0 and o.r == room and (o.t <= 3 or o.t == T_SHY or o.t == T_SHELL or o.t == T_THIEF or o.t == T_RIDER or o.t == T_PUFF)


func esmashall(me: En, x: int, y: int, w: int, h: int) -> void:   # a shell, thwomp or loose chomp runs enemies over
	for o in wd.en:
		if o != me and ekillable(o) and ov(x, y, w, h, (o.x >> 8) + 1, (o.y >> 8) + 1, 6, 7): ekill(o, 0x9a48d0, 200)


static func isqrt(v: int) -> int:
	var r := 0
	while (r + 1) * (r + 1) <= v: r += 1
	return r


func ewalk(e: En, edges: bool) -> void:   # walker physics: gravity, a floor, turning at walls (and at ledges if edges)
	e.vy += GRAV
	if e.vy > 900: e.vy = 900
	e.y += e.vy
	var ex: int = e.x >> 8
	var ey: int = e.y >> 8
	if scan(ex, ey, 8, 8, SOLID):
		var sg := 1 if e.vy < 0 else -1
		ey += sg
		while scan(ex, ey, 8, 8, SOLID) and absi(ey - (e.y >> 8)) < 16: ey += sg
		e.y = ey << 8; e.vy = 0
	e.x += e.vx; ex = e.x >> 8
	if scan(ex, ey, 8, 8, SOLID) or (edges and not e.vy and not scan(ex + 8 if e.vx > 0 else ex - 1, ey + 8, 1, 1, SOLID)):
		e.x -= e.vx; e.vx = -e.vx
	if ey > lh * 8: e.a = 0


# The beat: beats since the theme (re)started, in 1/256 beat, from the frame counter and the tempo in
# music.txt, never from the audio device, so replays stay exact. The theme restarts whenever fr does.
func beatpos() -> int:
	var m: String = L.music
	if m != ebpmof:
		ebpmof = m; ebpm = 10400
		var f := FileAccess.open(assets.path_join("music").path_join(m).path_join("music.txt"), FileAccess.READ)
		if f:
			var re := RegEx.create_from_string("^[ \\t]*bpm[ \\t]*([+-]?(\\d+\\.?\\d*|\\.\\d+)([eE][+-]?\\d+)?)")
			for line in f.get_as_text().split("\n"):
				var mt := re.search(line)
				if mt:
					var bp := mt.get_string(1).to_float()
					if bp >= 30 and bp <= 400: ebpm = int(bp * 100 + 0.5)
	return fr * ebpm * 256 / 360000


# A fire bar's angle (256 = one turn). Beat bars ($ &) carry speed +-1 and snap an eighth turn per beat.
func barangle(b: Dictionary) -> int:
	if absi(b.speed) != 1:
		return ((b.a0 * 256 + b.speed * fr) >> 8) & 255
	var bp := beatpos()
	var f := bp & 255
	var step := (bp >> 8) * 32 + (f * 32 / 40 - 32 if f < 40 else 0)
	return (b.a0 + b.speed * step) & 255


func enemies(k: int) -> void:
	var X := hx >> 8
	var Y := hy >> 8
	var n0 := wd.en.size()
	capone = 0
	var x0 := PackedInt32Array()   # for stacks
	x0.resize(n0)
	for i in n0:
		x0[i] = wd.en[i].x
	mv_slidehits()
	var i := 0
	while i < wd.en.size():   # a cloud rider can add enemies on the way
		var e: En = wd.en[i]
		if not e.a or e.r != room:
			i += 1
			continue
		if e.t >= T_SHY and e.t <= T_PISTON:
			extenemy(i, k)
			i += 1
			continue
		var ex: int
		var ey: int
		if e.t == 1 or e.t == E_CRAB:   # walker (and crab): patrols its platform
			e.vy += GRAV; e.y += e.vy; ex = e.x >> 8; ey = e.y >> 8
			if scan(ex, ey, 8, 8, SOLID):
				ey -= 1
				while scan(ex, ey, 8, 8, SOLID): ey -= 1
				e.y = ey << 8; e.vy = 0
			e.x += e.vx; ex = e.x >> 8
			if scan(ex, ey, 8, 8, SOLID) or (not e.vy and not scan(ex + 8 if e.vx > 0 else ex - 1, ey + 8, 1, 1, SOLID)):
				e.x -= e.vx; e.vx = -e.vx
			if ey > lh * 8: e.a = 0
		else:   # flyers: 2 bob vertically, 3 sweep horizontally
			var ph := (fr + (e.h >> 9)) & 127
			var o := (ph if ph < 64 else 128 - ph) - 32
			if e.t == 2: e.y = e.h + o * 192
			else: e.x = e.vx + sweep(ph)   # from its patrol, so it can't drift
		ex = e.x >> 8; ey = e.y >> 8
		if st < TUBE and ov(X, Y + duck, 6, 11 - duck, ex + 1, ey + 1, 6, 7):
			if (hvy > 0 or st == GPSLAM) and Y + 11 < ey + 6 and (st == GPSLAM or not worldclaws(e)):
				kill(i)
				hvy = -1000 if (k & 16) else -650; st = NORM; arcg = GRAV; launch = 0; cut = 0
				capok = 1; diveok = 1; stall = 1; spin = 0; throwt = 0
			else:
				die()
		if e.a and ecap(ex, ey, 8, 8):
			capx_enemy(e)
			ekill(e, 0x9a48d0, 200, "cap")
		i += 1
	estack(x0, n0)


# Stacks: walkers, crabs, shy-walkers and shell walkers (a resting shell too) land on each other's
# heads and ride along, so a stack walks and turns with its bottom enemy. Knock one out and the ones
# above drop down. Runs after every enemy has moved; x0 holds where the first n0 started the frame.
func estackable(e: En) -> bool:
	return e.a != 0 and e.r == room and (e.t == 1 or e.t == E_CRAB or e.t == T_SHY or (e.t == T_SHELL and e.s < 2))


func estack(x0: PackedInt32Array, n0: int) -> void:
	var s: Array = []
	for ie in wd.en.size():
		var e: En = wd.en[ie]
		if estackable(e):   # lowest first, so a rider's base has settled
			var i := s.size()
			s.append(ie)
			while i > 0 and wd.en[s[i - 1]].y < e.y:
				s[i] = s[i - 1]
				i -= 1
			s[i] = ie
	for i in range(1, s.size()):
		var ie: int = s[i]
		var e: En = wd.en[ie]
		if e.vy <= 0:
			continue   # on the ground, or on the way up
		var bot := e.y + (8 << 8)
		var was := bot - e.vy
		for j in i:
			var io: int = s[j]
			var o: En = wd.en[io]
			if absi(o.x - e.x) >= (6 << 8) or bot + (2 << 8) < o.y or was > o.y + (2 << 8) + (o.vy if o.vy > 0 else 0):
				continue
			var nx: int = (x0[ie] if ie < n0 else e.x) + ((o.x - x0[io]) if io < n0 else 0)
			var ny := o.y - (8 << 8)
			nx += (o.x - nx) / 8   # and eased onto the middle of its base
			if not scan(nx >> 8, ny >> 8, 8, 8, SOLID):   # carried along, unless that runs it into a wall
				e.x = nx
				if e.t != T_SHY and not (e.t == T_SHELL and e.s) and o.vx: e.vx = o.vx   # a shy-walker keeps its own facing
			e.y = ny; e.vy = 0
			break


# Crumble blocks: shake while stood on, fall after half a second (carrying whoever stands on
# them), and come back in place a few seconds later. Runs before hero() so a ride moves along.
func objects() -> void:
	for c in wd.cr:
		if c.state == 0:
			if c.stood:
				if not c.t: sfx("crumble")
				c.t += 1
				if c.t >= 30:
					c.state = 1; c.fy = (c.y * 8) << 8; c.vy = 0
					rmset(c.room, c.x, c.y, 0)
			elif c.t:
				c.t -= 1
			c.stood = 0
		elif c.state == 1:
			c.vy = mini(c.vy + 20, 640); c.fy += c.vy
			if ride and wd.cr[ride - 1] == c and room == c.room: hy += c.vy
			if (c.fy >> 8) > L.rooms[c.room].h * 8 + 16:
				c.state = 2; c.t = 150
		else:
			c.t -= 1
			if c.t <= 0:
				if c.room == room and ov(hx >> 8, hy >> 8, 6, 11, c.x * 8, c.y * 8, 8, 8):
					c.t = 10   # not into Hatrick
				else:
					c.state = 0; c.t = 0
					rmset(c.room, c.x, c.y, 13)
					if c.room == room: burst(c.x * 8 + 4, c.y * 8 + 4, 0xf0d098, 4)


# The tube a dweller lives in: [the opening's x centre, the y where it comes out (px), opens up].
func dwellerspot(h: Dictionary) -> Array:
	var t: Dictionary = L.tubes[h.tube]
	var up: bool = t.dir == T_UP
	return [t.x * 8 + 8, t.y * 8 if up else t.y * 8 + 8, up]


func dwellerhit(d: Dweller, h: Dictionary, x: int, y: int, w: int, hh: int) -> bool:   # its body: 6 px wide, ofs px out
	var sp := dwellerspot(h)
	return d.ofs > 2 and ov(x, y, w, hh, sp[0] - 3, sp[1] - d.ofs + 1 if sp[2] else sp[1], 6, d.ofs - 1)


# Moon coins, checkpoints, fire bars, tube dwellers and their seeds. Runs after hero() and the cap.
func hazards() -> void:
	var X := hx >> 8
	var Y := (hy >> 8) + duck
	var hh := 11 - duck
	var alive := st < TUBE
	var capon: bool = cst and cst < 3
	if hpt < 99: hpt += 1
	if healt: healt -= 1
	var mr := it_moonreach()
	for i in L.moons.size():   # moon coins: a 10 px box around each
		var m: Dictionary = L.moons[i]
		if m.room == room and alive and not ((wd.moongot >> i) & 1) and ov(X - mr, Y - mr, 6 + 2 * mr, hh + 2 * mr, m.x * 8 - 1, m.y * 8 - 1, 10, 10):
			wd.moongot |= 1 << i; sfx("moon"); rumble(3)
			burst(m.x * 8 + 4, m.y * 8 + 4, 0xfff0a0, 10); burst(m.x * 8 + 4, m.y * 8 + 4, 0xc8d8ff, 8)
			addscore(2000, m.x * 8 + 4, m.y * 8 - 4)
			ev.append(["moon", i])
	for i in wd.ck.size():
		var c: Check = wd.ck[i]
		if c.up and c.up < 20: c.up += 1
		if c.room == room and alive and ov(X, Y, 6, hh, c.x * 8 + 1, c.y * 8 - 8, 6, 16):
			if not c.up:
				haveck = 1; ckroom = room; ckx = (c.x * 8 + 1) << 8; cky = (c.y * 8 - 3) << 8
				c.up = 20; saved = wd.copy(); savedcoins = coins; savedscore = score   # the respawn snapshot has it raised
				c.up = 1; sfx("checkpoint")
				ev.append(["check", i])
			if hp < hpmax():   # any checkpoint, new or already raised, fills the hearts
				if c.up > 1: sfx("checkpoint")
				hp = hpmax(); healt = 24; sparkle(X + 3, Y + 2, 0xff7a8a, 10)
				ev.append(["heal"])
	for b in L.bars:
		if b.room != room or not alive:
			continue
		var a := barangle(b)
		for i in range(1, b.len):
			var fx: int = b.x * 8 + 4 + SIN[(a + 64) & 255] * i * 8 / 256
			var fy: int = b.y * 8 + 4 + SIN[a] * i * 8 / 256
			if ov(X, Y, 6, hh, fx - 2, fy - 2, 4, 4): die()
	for i in L.homes.size():
		var d: Dweller = wd.dw[i]
		var h: Dictionary = L.homes[i]
		if not d.a:
			continue
		var sp := dwellerspot(h)
		var cx: int = sp[0]
		var my: int = sp[1]
		var up: bool = sp[2]
		var here: bool = h.room == room
		var used: bool = st == TUBE and (tubefrom == h.tube or tubeto == h.tube)
		var near: bool = here and absi(X + 3 - cx) < 22 and ((Y + hh >= my - 40 and Y <= my + 16) if up else (Y <= my + 40 and Y + hh >= my - 16))
		if used:
			d.phase = 0; d.ofs = 0; d.t = maxi(d.t, 60)
		elif d.phase == 0:
			d.t -= 1
			if d.t <= 0:
				if near: d.t = 8
				else:
					d.phase = 1
					if here and absi(X - cx) < 200: sfx("emerge")
		elif d.phase == 1:
			d.ofs += 1
			if d.ofs >= 16: d.phase = 2; d.t = 70
		elif d.phase == 2:
			if h.spit and here and (d.t == 50 or d.t == 22):   # a seed lobbed toward Hatrick
				for p in wd.sh:
					if not p.a:
						var dx := (X + 3 - cx) * 256 / 80
						p.room = room; p.x = cx << 8; p.y = (my - 14 if up else my + 14) << 8
						p.vx = clampi(dx, -300, 300); p.vy = -560 if up else 0; p.a = 1
						sfx("spit")
						break
			d.t -= 1
			if d.t <= 0: d.phase = 3
		else:
			d.ofs -= 1
			if d.ofs <= 0:
				d.ofs = 0; d.phase = 0; d.t = 100
		if here and alive and dwellerhit(d, h, X, Y, 6, hh): die()   # no stomping these
		if here and capon and dwellerhit(d, h, (cxp >> 8) - CAPREACH, (cyp >> 8) - CAPREACH, 8 + 2 * CAPREACH, 5 + 2 * CAPREACH):
			if h.spit: capx_gain(POW_SEED, cx, my)
			d.a = 0; burst(cx, my - d.ofs / 2 if up else my + d.ofs / 2, 0xe0586a if h.spit else 0x2f8f9a, 10); sfx("stomp"); rumble(4); kick(5)
			addscore(500, cx, my - d.ofs if up else my)
			ev.append(["dweller", i])
	for p in wd.sh:
		if not p.a:
			continue
		p.vy += 14; p.x += p.vx; p.y += p.vy
		var sx: int = p.x >> 8
		var sy: int = p.y >> 8
		if p.room != room or sy > lh * 8 + 16 or ((SOLID >> tile(sx >> 3, sy >> 3)) & 1):
			p.a = 0
			continue
		if alive and ov(X, Y, 6, hh, sx - 2, sy - 2, 4, 4): die()
		if capon and capbox(sx - 2, sy - 2, 4, 4):
			p.a = 0; burst(sx, sy, 0x9a6a3a, 4); addscore(50, sx, sy)


func _clamp_camera() -> void:
	cxf = mini(cxf, (lw * 8 - W) * 256); cxf = maxi(cxf, 0)
	cyf = mini(cyf, (lh * 8 - H) << 8); cyf = maxi(cyf, 0)


func camera() -> void:
	if st != DEAD:
		var want := 0 if st == WIN else hvx * 24
		want = clampi(want, -(48 << 8), 48 << 8)
		look += (want - look) / 40
		cxf += (hx + (3 << 8) + look - ((W / 2) << 8) - cxf) / 7
		if gnd or st == WIN: camgy = hy
		var ty := camgy - ((H * 5 / 8) << 8)
		if hy - ty < (36 << 8): ty = hy - (36 << 8)       # keep a high jump in view
		if hy - ty > (100 << 8): ty = hy - (100 << 8)     # and a fall
		cyf += (ty - cyf) / 9
	gim_camera()   # the auto-scroll owns the camera's x
	bosscam()      # a fight holds the camera on the arena
	_clamp_camera()


# One step of the game, 1/60 s. k: the keys held (see the K_ constants; ANALOG carries the stick).
func tick(kraw: int) -> void:
	var pr := kraw & ~prevk
	prevk = kraw
	var k := kraw
	if k & CAP2: k |= 32
	if pr & CAP2: pr |= 32   # pressing the other face button is a real new action
	if pr & K_RESTART:
		load_level()
		return
	fr += 1
	if not done: tim += 1
	if shake: shake -= 1
	oldhy = hy
	if gim_pre(k, pr):   # platforms; a door turning the room holds the game
		return
	objects()
	hero(k, pr)
	capupd(k)
	enemies(k)
	hazards()
	cl_tick()
	it_tick()
	bosstick()
	worldtick()   # clock blocks, the avalanche
	gim_post(k, pr)
	if st < DEAD and not done and left > (1 if st == TUBE else 0):   # the level timer (it holds at 1 in a tube)
		left -= 1
		if left == 100 * 60: sfx("hurry")
		if not left:
			timeout = 1; die()
	gim_again(k)   # the speed flip runs a second step
	if not gim_again_on:
		camera()
