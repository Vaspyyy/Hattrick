# Hatrick in 3D: a small rig of joints carrying smooth parts, posed procedurally every frame from
# the simulation's state (no keyframes): run cycles that follow the distance covered, flips that
# follow the game's own flip timers, squash and stretch, blinking, a face that reacts, and a cap
# that leaves his head when it is thrown.
#
# Model space: +Y up, +Z forward (toward the camera at rest), the left side at +X. For a limb
# hanging down, a negative X rotation swings it forward; knees bend with positive X.
extends Node3D

const Meshes := preload("res://src/view/meshes.gd")
const Mats := preload("res://src/view/mats.gd")
const Sim := preload("res://src/sim/sim.gd")

const SKIN := Color("ffc79a")
const CAP := Color("ff6f12")
const BAND := Color("fff0d2")
const SHIRT := Color("ee5f1b")
const OVERALL := Color("2a6c84")
const BOOT := Color("6b3a1f")
const HAIR := Color("7b3a17")
const PUPIL := Color("1c1a2c")
const BLUSH := Color("ff8f86")
const MOUTH := Color("7a2626")
const GOLD := Color("ffd04a")

const PIV := 0.62        # flips turn around the middle of the body
const YAW_SIDE := 54.0   # how far he turns to his side: a good part toward the camera, like NSMB

var J := {}               # joints by name
var Q := {}               # their current rotations (smoothed)
var mats: Array = []      # his own materials, for the hit flash
var cap_root: Node3D      # the cap on his head
var eyes: Array = []      # eye nodes (scaled to blink)
var pupils: Array = []
var brows: Array = []
var mouth: MeshInstance3D
var squash: Node3D
var flipn: Node3D
var pelvis: Node3D

var yaw := YAW_SIDE
var sq := 0.0             # squash amount (+ stretch, - squash)
var sqv := 0.0
var t := 0.0
var run_phase := 0.0
var roll_angle := 0.0
var spin_yaw := 0.0
var blink_t := 2.0
var blink := 0.0
var look_t := 0.0
var look_yaw := 0.0
var idle_t := 0.0
var throw_t := 9.0
var throw_kind := 0
var catch_t := 9.0
var cap_pop := 0.0
var sideflip_t := 9.0
var hurt_t := 9.0
var land_t := 9.0
var pound_t := 9.0
var wall_kick_t := 9.0
var prev_gnd := 1
var prev_vy := 0
var flip_cur := Vector3.ZERO
var piv_cur := PIV
var dead_spin := 0.0
var ball := 1.0           # rolling: he curls up smaller


func _ready() -> void:
	_build()


func _mat(c: Color, outline := true, rough := 0.55, rim := 0.35) -> ShaderMaterial:
	var m := Mats.toon(c, outline, rough, rim, 0.016, true)
	mats.append(m)
	return m


func _part(parent: Node3D, mesh: Mesh, c: Color, pos := Vector3.ZERO, rot := Vector3.ZERO, outline := true, rough := 0.55) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.position = pos
	mi.rotation_degrees = rot
	mi.material_override = _mat(c, outline, rough)
	parent.add_child(mi)
	return mi


func _joint(parent: Node3D, n: String, pos: Vector3) -> Node3D:
	var j := Node3D.new()
	j.name = n
	j.position = pos
	parent.add_child(j)
	J[n] = j
	Q[n] = Quaternion.IDENTITY
	return j


static func build_cap(parent: Node3D, mat_fn: Callable) -> Node3D:
	var cap := Node3D.new()
	parent.add_child(cap)
	var dome := PackedVector2Array([
		Vector2(0.300, -0.005), Vector2(0.309, 0.03), Vector2(0.305, 0.08), Vector2(0.285, 0.135),
		Vector2(0.245, 0.185), Vector2(0.18, 0.225), Vector2(0.10, 0.248), Vector2(0.0, 0.255)])
	var mi := MeshInstance3D.new()
	mi.mesh = Meshes.lathe(dome, 28, "capdome")
	mi.material_override = mat_fn.call(CAP)
	cap.add_child(mi)
	var band := PackedVector2Array([Vector2(0.296, -0.014), Vector2(0.325, -0.002), Vector2(0.33, 0.028), Vector2(0.318, 0.05), Vector2(0.296, 0.056)])
	var bi := MeshInstance3D.new()
	bi.mesh = Meshes.lathe(band, 28, "capband")
	bi.material_override = mat_fn.call(BAND)
	cap.add_child(bi)
	# the brim: a rounded visor out front
	var poly := PackedVector2Array()
	for i in 17:
		var a := PI * i / 16.0
		poly.append(Vector2(cos(a) * 0.3, sin(a) * 0.43))
	var brim := MeshInstance3D.new()
	brim.mesh = Meshes.extrude(poly, 0.035, "capbrim")
	brim.rotation_degrees = Vector3(90 + 8, 0, 0)
	brim.position = Vector3(0, 0.012, 0.02)
	brim.material_override = mat_fn.call(CAP.darkened(0.08))
	# the brim would shade the face with noisy soft shadow up close: the dome alone casts
	brim.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	bi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	cap.add_child(brim)
	# the badge: a cream disc with an H
	var badge := MeshInstance3D.new()
	badge.mesh = Meshes.cylinder(0.085, 0.085, 0.022, 20)
	badge.rotation_degrees = Vector3(90 - 32, 0, 0)
	badge.position = Vector3(0, 0.15, 0.245)
	badge.material_override = mat_fn.call(BAND)
	cap.add_child(badge)
	var h := Label3D.new()
	h.text = "H"
	h.font_size = 96
	h.pixel_size = 0.0011
	h.modulate = CAP.darkened(0.1)
	h.outline_size = 0
	h.position = Vector3(0, 0.157, 0.259)
	h.rotation_degrees = Vector3(-32, 0, 0)
	var f = load("res://src/ui/fonts.gd").bold()
	if f: h.font = f
	cap.add_child(h)
	return cap


func _build() -> void:
	squash = Node3D.new()
	add_child(squash)
	flipn = Node3D.new()
	flipn.position = Vector3(0, PIV, 0)
	squash.add_child(flipn)
	pelvis = _joint(flipn, "pelvis", Vector3(0, 0.36 - PIV, 0))
	# overalls: hips and belly
	_part(pelvis, Meshes.ellipsoid(Vector3(0.205, 0.145, 0.185)), OVERALL, Vector3(0, 0.03, 0))
	var spine := _joint(pelvis, "spine", Vector3(0, 0.07, 0))
	_part(spine, Meshes.ellipsoid(Vector3(0.215, 0.15, 0.19)), OVERALL, Vector3(0, 0.03, 0.0))
	_part(spine, Meshes.ellipsoid(Vector3(0.2, 0.15, 0.175)), SHIRT, Vector3(0, 0.15, -0.01))
	# bib and straps
	_part(spine, Meshes.rounded_box(Vector3(0.2, 0.12, 0.05), 0.022), OVERALL, Vector3(0, 0.13, 0.155), Vector3(-8, 0, 0))
	for sx in [-1.0, 1.0]:
		_part(spine, Meshes.capsule(0.022, 0.2), OVERALL, Vector3(sx * 0.085, 0.2, 0.1), Vector3(-35, 0, sx * -8))
		_part(spine, Meshes.ellipsoid(Vector3(0.028, 0.028, 0.016)), GOLD, Vector3(sx * 0.075, 0.175, 0.178), Vector3(-8, 0, 0), false, 0.3)
	var neck := _joint(spine, "neck", Vector3(0, 0.25, 0))
	var head := Node3D.new()
	neck.add_child(head)
	_part(head, Meshes.ellipsoid(Vector3(0.29, 0.27, 0.275), 24, 14), SKIN, Vector3(0, 0.22, 0))
	_part(head, Meshes.ellipsoid(Vector3(0.046, 0.04, 0.042)), SKIN.darkened(0.06), Vector3(0, 0.16, 0.272))
	for sx in [-1.0, 1.0]:
		_part(head, Meshes.ellipsoid(Vector3(0.05, 0.075, 0.04)), SKIN, Vector3(sx * 0.282, 0.2, -0.01), Vector3(0, sx * 20, 0))
		var blush := _part(head, Meshes.ellipsoid(Vector3(0.055, 0.032, 0.02)), BLUSH, Vector3(sx * 0.165, 0.135, 0.228), Vector3(0, sx * 32, 0), false)
		blush.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		var eye := Node3D.new()
		eye.position = Vector3(sx * 0.092, 0.25, 0.232)
		eye.rotation_degrees = Vector3(-6, sx * 16, 0)
		head.add_child(eye)
		_part(eye, Meshes.ellipsoid(Vector3(0.07, 0.102, 0.046)), Color.WHITE, Vector3(0, 0, 0.004), Vector3.ZERO, true, 0.7)
		var pupil := Node3D.new()
		pupil.position = Vector3(sx * -0.006, -0.004, 0.036)
		eye.add_child(pupil)
		_part(pupil, Meshes.ellipsoid(Vector3(0.043, 0.066, 0.02)), PUPIL, Vector3.ZERO, Vector3.ZERO, false, 0.2)
		var glint := _part(pupil, Meshes.ellipsoid(Vector3(0.012, 0.016, 0.008)), Color.WHITE, Vector3(0.012, 0.022, 0.017), Vector3.ZERO, false)
		glint.material_override.set_shader_parameter("emission", Color(0.6, 0.6, 0.6))
		eyes.append(eye)
		pupils.append(pupil)
		var brow := Node3D.new()
		brow.position = Vector3(sx * 0.095, 0.375, 0.238)
		brow.rotation_degrees = Vector3(-20, sx * 17, 0)
		head.add_child(brow)
		_part(brow, Meshes.capsule(0.014, 0.085), HAIR, Vector3.ZERO, Vector3(0, 0, 90), false)
		brows.append(brow)
	mouth = _part(head, Meshes.ellipsoid(Vector3(0.05, 0.02, 0.02)), MOUTH, Vector3(0, 0.095, 0.262), Vector3(-20, 0, 0), false)
	# hair under the cap: the back and the sideburns
	_part(head, Meshes.ellipsoid(Vector3(0.27, 0.2, 0.2)), HAIR, Vector3(0, 0.27, -0.1))
	for sx in [-1.0, 1.0]:
		_part(head, Meshes.ellipsoid(Vector3(0.05, 0.08, 0.06)), HAIR, Vector3(sx * 0.262, 0.29, 0.07), Vector3(0, 0, sx * -10))
	_part(head, Meshes.ellipsoid(Vector3(0.27, 0.22, 0.27)), HAIR, Vector3(0, 0.31, -0.02))
	var socket := Node3D.new()
	socket.position = Vector3(0, 0.37, -0.005)
	socket.rotation_degrees = Vector3(-5, 0, 0)
	head.add_child(socket)
	cap_root = build_cap(socket, func(c): return _mat(c))
	for side in ["L", "R"]:
		var sx := 1.0 if side == "L" else -1.0
		var sh := _joint(spine, "sh" + side, Vector3(sx * 0.215, 0.185, 0))
		_part(sh, Meshes.ellipsoid(Vector3(0.075, 0.08, 0.075)), SHIRT, Vector3(0, -0.02, 0))
		_part(sh, Meshes.capsule(0.06, 0.17), SHIRT, Vector3(0, -0.075, 0))
		var el := _joint(sh, "el" + side, Vector3(0, -0.15, 0))
		_part(el, Meshes.capsule(0.052, 0.14), SKIN, Vector3(0, -0.055, 0))
		var hand := Node3D.new()
		hand.position = Vector3(0, -0.13, 0)
		el.add_child(hand)
		J["hand" + side] = hand
		_part(hand, Meshes.ellipsoid(Vector3(0.072, 0.078, 0.07)), SKIN, Vector3(0, -0.02, 0.0))
		_part(hand, Meshes.ellipsoid(Vector3(0.028, 0.04, 0.028)), SKIN, Vector3(sx * -0.055, 0.0, 0.035), Vector3(0, 0, sx * 30))
		var hip := _joint(pelvis, "hip" + side, Vector3(sx * 0.1, -0.01, 0))
		_part(hip, Meshes.capsule(0.07, 0.17), OVERALL, Vector3(0, -0.07, 0))
		var kn := _joint(hip, "kn" + side, Vector3(0, -0.155, 0))
		_part(kn, Meshes.capsule(0.06, 0.15), OVERALL, Vector3(0, -0.06, 0))
		var an := _joint(kn, "an" + side, Vector3(0, -0.15, 0))
		_part(an, Meshes.ellipsoid(Vector3(0.088, 0.072, 0.135)), BOOT, Vector3(0, 0.022, 0.035))
		_part(an, Meshes.ellipsoid(Vector3(0.092, 0.03, 0.14)), BOOT.darkened(0.3), Vector3(0, -0.035, 0.035))
	for mi in find_children("*", "MeshInstance3D", true, false):
		(mi as MeshInstance3D).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON


# ---------- events from the simulation ----------
func on_event(e: Array, s) -> void:
	match e[0]:
		"throw":
			throw_t = 0.0
			throw_kind = e[1]
		"catch":
			catch_t = 0.0
			cap_pop = 1.0
		"sideflip":
			sideflip_t = 0.0
		"hurt":
			hurt_t = 0.0
		"land":
			var vy: int = e[1]
			sqv = -clampf(vy / 1400.0, 0.0, 1.0) * 0.42 - 0.08
			land_t = 0.0
		"pound":
			sqv = -0.55
			pound_t = 0.0
		"jump", "capbounce", "spring":
			sqv = 0.28
		"walljump":
			wall_kick_t = 0.0
			sqv = 0.2
		"spawn":
			flip_cur = Vector3.ZERO
			spin_yaw = 0.0
			yaw = YAW_SIDE * s.face
			throw_t = 9.0
			catch_t = 9.0
			hurt_t = 9.0
			dead_spin = 0.0
			for k in Q:
				Q[k] = Quaternion.IDENTITY


# ---------- posing ----------
func _neutral() -> Dictionary:
	return {
		"pelvis": Vector3.ZERO, "spine": Vector3(4, 0, 0), "neck": Vector3(-2, 0, 0),
		"shL": Vector3(0, 0, 9), "elL": Vector3(-18, 0, 0), "shR": Vector3(0, 0, -9), "elR": Vector3(-18, 0, 0),
		"hipL": Vector3(0, 0, 2), "knL": Vector3(4, 0, 0), "anL": Vector3(-4, 0, 0),
		"hipR": Vector3(0, 0, -2), "knR": Vector3(4, 0, 0), "anR": Vector3(-4, 0, 0),
		"py": 0.0,
	}


static func _lerp_pose(a: Dictionary, b: Dictionary, w: float) -> Dictionary:
	var r := {}
	for k in a:
		if b.has(k):
			r[k] = a[k].lerp(b[k], w) if a[k] is Vector3 else lerpf(a[k], b[k], w)
		else:
			r[k] = a[k]
	return r


func _arm(P: Dictionary, side: String, sh: Vector3, el: Vector3) -> void:
	# poses are written for the left arm; the right one mirrors the sideways angles
	if side == "R":
		sh = Vector3(sh.x, -sh.y, -sh.z)
		el = Vector3(el.x, -el.y, -el.z)
	P["sh" + side] = sh
	P["el" + side] = el


func _leg(P: Dictionary, side: String, hip: Vector3, kn: float, an: float) -> void:
	if side == "R":
		hip = Vector3(hip.x, -hip.y, -hip.z)
	P["hip" + side] = hip
	P["kn" + side] = Vector3(kn, 0, 0)
	P["an" + side] = Vector3(an, 0, 0)


func _tuck(P: Dictionary) -> void:
	P.spine = Vector3(38, 0, 0)
	P.neck = Vector3(10, 0, 0)
	_leg(P, "L", Vector3(-105, 0, 6), 135, -20)
	_leg(P, "R", Vector3(-100, 0, 6), 130, -20)
	_arm(P, "L", Vector3(-55, 0, 18), Vector3(-95, 0, 0))
	_arm(P, "R", Vector3(-55, 0, 18), Vector3(-95, 0, 0))
	P.py = 0.08


func _run(P: Dictionary, speed: float, ph: float) -> void:
	var w := clampf(speed / 11.7, 0.0, 1.0)   # 0 walking .. 1 full run (11.7 tiles/s)
	var amp := lerpf(22.0, 52.0, w)
	var s := sin(ph)
	var c := cos(ph)
	for side in ["L", "R"]:
		var o := 0.0 if side == "L" else PI
		var sp := sin(ph + o)
		var cp := cos(ph + o)
		var hip := -amp * sp
		var knee := lerpf(8.0, 14.0, w) + lerpf(30.0, 95.0, w) * pow(maxf(0.0, cp), 1.4) + 10.0 * maxf(0.0, -sp)
		var ankle := -0.45 * (hip + knee) + 12.0 * maxf(0.0, -cp) * w
		_leg(P, side, Vector3(hip, 0, 3), knee, ankle)
	var aamp := lerpf(18.0, 62.0, w)
	_arm(P, "L", Vector3(aamp * s, 0, 12 + 6 * w), Vector3(lerpf(-25.0, -85.0, w) - 10.0 * maxf(0.0, -s), 0, 0))
	_arm(P, "R", Vector3(-aamp * s, 0, 12 + 6 * w), Vector3(lerpf(-25.0, -85.0, w) - 10.0 * maxf(0.0, s), 0, 0))
	P.spine = Vector3(lerpf(6.0, 20.0, w), 9.0 * s * w, 0)
	P.pelvis = Vector3(0, -7.0 * s * w, 0)
	P.neck = Vector3(-lerpf(4.0, 14.0, w), -6.0 * s * w, 0)
	P.py = -0.02 * w + lerpf(0.012, 0.04, w) * absf(c) - 0.01


func animate(s, pos: Vector3, alpha: float, dt: float) -> void:
	t += dt
	position = pos
	var st: int = s.st
	var face: int = s.face
	var gnd: int = s.gnd
	var hvx: float = s.hvx
	var hvy: float = s.hvy
	var speed := absf(hvx) * 60.0 / 2048.0   # tiles per second
	var P := _neutral()
	var rate := 16.0
	var yaw_t := YAW_SIDE * face
	var piv_t := PIV
	var flip_t := Vector3.ZERO
	var direct_flip := false
	var expr := "calm"
	var cap_on: bool = s.cst == 0 or st == Sim.TUBE
	throw_t += dt
	catch_t += dt
	sideflip_t += dt
	hurt_t += dt
	land_t += dt
	pound_t += dt
	wall_kick_t += dt
	cap_pop = maxf(0.0, cap_pop - dt * 5.0)
	var spin_frac := -1.0
	if s.spin > 0 and st != Sim.DEAD and st != Sim.ROLL:
		spin_frac = clampf((s.spinlen - s.spin + alpha) / float(s.spinlen), 0.0, 1.0)
	var spinning: bool = st == Sim.SPINJ or st == Sim.GSPIN or s.twirl > 0
	if st == Sim.DEAD:
		expr = "dead"
		yaw_t = 0.0
		dead_spin += dt * 2.0
		_arm(P, "L", Vector3(-150, 0, 38), Vector3(-30, 0, 0))
		_arm(P, "R", Vector3(-150, 0, 38), Vector3(-30, 0, 0))
		_leg(P, "L", Vector3(-30, 0, 18), 60, -10)
		_leg(P, "R", Vector3(10, 0, 12), 20, 0)
		P.spine = Vector3(-14, 0, 0)
		P.neck = Vector3(-18, 0, 0)
		flip_t = Vector3(0, 0, sin(dead_spin * 3.0) * 12.0)
		cap_on = true
	elif st == Sim.WIN:
		_win(P, s)
		yaw_t = 90.0 if s.wphase == 0 else lerpf(12.0, 0.0, clampf(s.wt / 10.0, 0, 1))
		expr = "happy" if s.wphase > 0 else "calm"
		cap_on = true
	elif st == Sim.TUBE:
		var tb: Dictionary = s.L.tubes[s.tubefrom if s.stt < 20 else s.tubeto]
		yaw_t = 0.0 if tb.dir <= 1 else (90.0 if (tb.dir == 3) == (s.stt >= 20) else -90.0)
		_arm(P, "L", Vector3(-5, 0, 6), Vector3(-10, 0, 0))
		_arm(P, "R", Vector3(-5, 0, 6), Vector3(-10, 0, 0))
	elif st == Sim.ROLL:
		_tuck(P)
		P.spine = Vector3(62, 0, 0)
		P.neck = Vector3(30, 0, 0)
		P.py = -0.12
		piv_t = 0.44
		yaw_t = 90.0 * face
		roll_angle += rad_to_deg(speed * dt / 0.34) * 0.85   # a forward roll that keeps up with the ground
		flip_t = Vector3(roll_angle, 0, 0)
		direct_flip = true
		rate = 30.0
		expr = "focus"
	elif st == Sim.DIVE:
		P.spine = Vector3(-8, 0, 0)
		P.neck = Vector3(-50, 0, 0)
		_arm(P, "L", Vector3(-172, 0, 14), Vector3(-4, 0, 0))
		_arm(P, "R", Vector3(-172, 0, 14), Vector3(-4, 0, 0))
		_leg(P, "L", Vector3(10, 0, 4), 18, 20)
		_leg(P, "R", Vector3(16, 0, 4), 32, 20)
		flip_t = Vector3(clampf(72.0 + hvy / 40.0, 55.0, 95.0), 0, 0)
		yaw_t = 90.0 * face
		expr = "focus"
		rate = 18.0
	elif st == Sim.SLIDE:
		P.spine = Vector3(-10, 0, 0)
		P.neck = Vector3(-60, 0, 0)
		_arm(P, "L", Vector3(-165, 0, 22), Vector3(-10, 0, 0))
		_arm(P, "R", Vector3(-165, 0, 22), Vector3(-10, 0, 0))
		var kick := sin(t * 18.0) * 14.0
		_leg(P, "L", Vector3(20, 0, 6), 40 + kick, 30)
		_leg(P, "R", Vector3(20, 0, 6), 40 - kick, 30)
		flip_t = Vector3(90, 0, 0)
		piv_t = 0.2
		yaw_t = 90.0 * face
		expr = "happy"
	elif st == Sim.LONGJ:
		P.spine = Vector3(-4, 0, 0)
		P.neck = Vector3(-30, 0, 0)
		_arm(P, "L", Vector3(-150, 0, 26), Vector3(-10, 0, 0))
		_arm(P, "R", Vector3(-150, 0, 26), Vector3(-10, 0, 0))
		_leg(P, "L", Vector3(-20, 0, 4), 50, 10)
		_leg(P, "R", Vector3(28, 0, 4), 40, 20)
		flip_t = Vector3(clampf(40.0 + hvy / 60.0, 25.0, 60.0), 0, 0)
		yaw_t = 90.0 * face
		expr = "focus"
	elif st == Sim.GPWIND:
		_tuck(P)
		yaw_t = 90.0 * face
		expr = "focus"
	elif st == Sim.GPSLAM:
		P.spine = Vector3(8, 0, 0)
		P.neck = Vector3(12, 0, 0)
		_arm(P, "L", Vector3(-160, 0, 28), Vector3(-30, 0, 0))
		_arm(P, "R", Vector3(-160, 0, 28), Vector3(-30, 0, 0))
		_leg(P, "L", Vector3(-80, 0, 14), 120, -25)
		_leg(P, "R", Vector3(-80, 0, 14), 120, -25)
		P.py = 0.12
		yaw_t = 70.0 * face
		rate = 26.0
		expr = "focus"
	elif st == Sim.GPLAND:
		var u := clampf(s.stt / 24.0, 0.0, 1.0)
		P.spine = Vector3(lerpf(30.0, 8.0, u), 0, 0)
		_arm(P, "L", Vector3(-20, 0, lerpf(70.0, 20.0, u)), Vector3(-30, 0, 0))
		_arm(P, "R", Vector3(-20, 0, lerpf(70.0, 20.0, u)), Vector3(-30, 0, 0))
		_leg(P, "L", Vector3(lerpf(-75.0, -10.0, u), 0, 14), lerpf(120.0, 20.0, u), lerpf(-45.0, -8.0, u))
		_leg(P, "R", Vector3(lerpf(-75.0, -10.0, u), 0, 14), lerpf(120.0, 20.0, u), lerpf(-45.0, -8.0, u))
		P.py = lerpf(-0.17, -0.02, u)
		expr = "focus"
	elif st == Sim.HANG or st == Sim.CLIMB:
		var u := clampf(s.stt / 12.0, 0.0, 1.0) if st == Sim.CLIMB else 0.0
		_arm(P, "L", Vector3(lerpf(-172.0, -60.0, u), 0, 12), Vector3(lerpf(-8.0, -60.0, u), 0, 0))
		_arm(P, "R", Vector3(lerpf(-172.0, -60.0, u), 0, 12), Vector3(lerpf(-8.0, -60.0, u), 0, 0))
		var sw := sin(t * 3.0) * 6.0 * (1.0 - u)
		_leg(P, "L", Vector3(-10 + sw - 40 * u, 0, 4), 25 + 60 * u, 0)
		_leg(P, "R", Vector3(6 - sw - 20 * u, 0, 4), 15 + 40 * u, 0)
		P.spine = Vector3(-4 + 30 * u, 0, 0)
		P.neck = Vector3(-24 + 20 * u, 0, 0)
		P.py = -0.05 * (1.0 - u)
		yaw_t = 80.0 * face
	elif s.mv_swing:
		_arm(P, "L", Vector3(-178, 0, 10), Vector3(0, 0, 0))
		_arm(P, "R", Vector3(-178, 0, 10), Vector3(0, 0, 0))
		_leg(P, "L", Vector3(-20, 0, 4), 30, 0)
		_leg(P, "R", Vector3(-10, 0, 4), 20, 0)
		var a := float(s.mv_ang) / 65536.0 * 360.0
		flip_t = Vector3(a, 0, 0)
		piv_t = 1.25
		direct_flip = true
		yaw_t = 90.0 * face
	elif s.mv_wslide:
		var wsd: int = s.mv_wslide
		yaw_t = -wsd * 30.0
		_arm(P, "L" if wsd > 0 else "R", Vector3(-20, 0, 95), Vector3(-30, 0, 0))
		_arm(P, "R" if wsd > 0 else "L", Vector3(-30, 0, 30), Vector3(-50, 0, 0))
		_leg(P, "L", Vector3(-50, 0, 10), 80, -10)
		_leg(P, "R", Vector3(-10, 0, 8), 30, 0)
		P.spine = Vector3(-6, wsd * 10.0, wsd * 6.0)
		P.neck = Vector3(-8, -wsd * 20.0, 0)
		expr = "focus"
	elif s.mv_swim:
		var sw2 := sin(t * 6.0)
		_arm(P, "L", Vector3(-90 + sw2 * 50, 0, 40), Vector3(-30, 0, 0))
		_arm(P, "R", Vector3(-90 - sw2 * 50, 0, 40), Vector3(-30, 0, 0))
		_leg(P, "L", Vector3(sw2 * 30, 0, 6), 30, 20)
		_leg(P, "R", Vector3(-sw2 * 30, 0, 6), 30, 20)
		flip_t = Vector3(35, 0, 0)
		yaw_t = 85.0 * face
	elif spinning:
		_arm(P, "L", Vector3(-10, 0, 92), Vector3(-6, 0, 0))
		_arm(P, "R", Vector3(-10, 0, 92), Vector3(-6, 0, 0))
		_leg(P, "L", Vector3(-6, 0, 6), 12, 0)
		_leg(P, "R", Vector3(-2, 0, 6), 8, 0)
		P.spine = Vector3(-2, 0, 0)
		spin_yaw += dt * 360.0 * 3.2 * face
		flip_t = Vector3(0, spin_yaw, 0)
		direct_flip = true
		rate = 24.0
		expr = "happy"
	elif s.duck and gnd:
		P.spine = Vector3(30, 0, 0)
		P.neck = Vector3(-24, 0, 0)
		var cw := 0.0
		if speed > 0.3:
			run_phase += speed * dt / 0.8 * TAU
			cw = sin(run_phase)
		_leg(P, "L", Vector3(-72 - cw * 18, 0, 10), 122 + cw * 10, -48)
		_leg(P, "R", Vector3(-72 + cw * 18, 0, 10), 122 - cw * 10, -48)
		_arm(P, "L", Vector3(-38, 0, 18), Vector3(-50, 0, 0))
		_arm(P, "R", Vector3(-38, 0, 18), Vector3(-50, 0, 0))
		P.py = -0.16
		rate = 20.0
	elif gnd:
		if s.skid:
			P.spine = Vector3(-18, 0, 0)
			P.neck = Vector3(10, 0, 0)
			_leg(P, "L", Vector3(-48, 0, 8), 14, 10)
			_leg(P, "R", Vector3(22, 0, 8), 46, -10)
			_arm(P, "L", Vector3(-80, 0, 40), Vector3(-20, 0, 0))
			_arm(P, "R", Vector3(-30, 0, 60), Vector3(-30, 0, 0))
			P.py = -0.05
			yaw_t = YAW_SIDE * face
			expr = "worried"
			rate = 22.0
		elif speed > 0.25:
			run_phase += speed * dt / 1.55 * TAU
			_run(P, speed, run_phase)
			idle_t = 0.0
			expr = "focus" if speed > 9.0 else "calm"
			rate = 22.0
		else:
			idle_t += dt
			var br := sin(t * 2.4)
			P.spine = Vector3(3 + br * 1.6, 0, 0)
			P.neck = Vector3(-3 - br * 1.2, look_yaw, 0)
			P.py = br * 0.006
			_arm(P, "L", Vector3(2 + br * 2, 0, 9 + br), Vector3(-18, 0, 0))
			_arm(P, "R", Vector3(2 + br * 2, 0, 9 + br), Vector3(-18, 0, 0))
			if idle_t > 1.0:
				yaw_t = YAW_SIDE * face * 0.55
			if idle_t > 6.0 and fmod(idle_t, 7.0) < 1.4:   # tap the cap
				var u := sin(fmod(idle_t, 7.0) / 1.4 * PI)
				_arm(P, "R" if face > 0 else "L", Vector3(-150 * u, 0, 30 * u), Vector3(-90 * u, 0, 0))
			rate = 8.0
	else:
		# in the air
		var fl: bool = s.mv_flut > 0 or s.capx_flut > 0
		if fl:
			var k := sin(t * 34.0)
			_leg(P, "L", Vector3(-40 + k * 40, 0, 6), 60 - k * 20, 0)
			_leg(P, "R", Vector3(-40 - k * 40, 0, 6), 60 + k * 20, 0)
			_arm(P, "L", Vector3(-30, 0, 70 + k * 25), Vector3(-30, 0, 0))
			_arm(P, "R", Vector3(-30, 0, 70 - k * 25), Vector3(-30, 0, 0))
			expr = "focus"
			rate = 30.0
		elif spin_frac >= 0.0:
			_tuck(P)
			rate = 24.0
			expr = "happy"
		elif hvy < -150:
			if s.jn == 1:
				_arm(P, "L", Vector3(-165, 0, 30), Vector3(-10, 0, 0))
				_arm(P, "R", Vector3(-165, 0, 30), Vector3(-10, 0, 0))
				_leg(P, "L", Vector3(-30, 0, 6), 60, 0)
				_leg(P, "R", Vector3(-10, 0, 6), 30, 0)
			else:
				var fwd := "R" if face > 0 else "L"
				var back := "L" if face > 0 else "R"
				_arm(P, fwd, Vector3(-165, 0, 16), Vector3(-10, 0, 0))
				_arm(P, back, Vector3(36, 0, 24), Vector3(-28, 0, 0))
				_leg(P, back, Vector3(-62, 0, 6), 92, -10)
				_leg(P, fwd, Vector3(14, 0, 4), 22, 14)
			P.spine = Vector3(-6, 0, 0)
			P.neck = Vector3(-14, 0, 0)
			expr = "jump"
			rate = 20.0
		else:
			var fall := clampf((hvy - 150.0) / 900.0, 0.0, 1.0)
			var wob := sin(t * 14.0) * 10.0 * fall
			_arm(P, "L", Vector3(-70 - 70 * fall + wob, 0, 45), Vector3(-30, 0, 0))
			_arm(P, "R", Vector3(-70 - 70 * fall - wob, 0, 45), Vector3(-30, 0, 0))
			_leg(P, "L", Vector3(-30 + wob, 0, 8), 50, -6)
			_leg(P, "R", Vector3(-6 - wob, 0, 8), 24, 6)
			P.spine = Vector3(2, 0, 0)
			P.neck = Vector3(-6, 0, 0)
			expr = "worried" if fall > 0.8 else "jump"
			rate = 14.0
	# flips from the game's flip timer: one smooth turn, eased in and out
	if spin_frac >= 0.0 and not spinning:
		var e := spin_frac * spin_frac * (3.0 - 2.0 * spin_frac)
		var sgn := 1.0 if s.spind == face else -1.0
		flip_t = Vector3(sgn * 360.0 * e, 0, 0)
		direct_flip = true
		yaw_t = 90.0 * face
		if sideflip_t < 0.7:
			flip_t.y = 180.0 * (1.0 - e) * -face
	# cap throw / catch overlays
	if throw_t < 0.32 and st != Sim.DEAD and not spinning:
		_throw_overlay(P, face)
	if catch_t < 0.22 and throw_t > 0.3 and not spinning and st < Sim.TUBE:
		var u := sin(catch_t / 0.22 * PI)
		var arm := "R" if face > 0 else "L"
		P["sh" + arm] = P["sh" + arm].lerp(Vector3(-150, 0, (-20 if arm == "R" else 20)), u)
		P["el" + arm] = P["el" + arm].lerp(Vector3(-40, 0, 0), u)
	if hurt_t < 0.4 and st < Sim.TUBE:
		var u := sin(hurt_t / 0.4 * PI)
		P.spine = P.spine.lerp(Vector3(-30, 0, 0), u)
		P.neck = P.neck.lerp(Vector3(-25, 0, 0), u)
		_arm(P, "L", Vector3(-130, 0, 40), Vector3(-30, 0, 0))
		_arm(P, "R", Vector3(-130, 0, 40), Vector3(-30, 0, 0))
		expr = "hurt"
	# apply
	var k := 1.0 - exp(-rate * dt)
	for n in ["pelvis", "spine", "neck", "shL", "elL", "shR", "elR", "hipL", "knL", "anL", "hipR", "knR", "anR"]:
		var v: Vector3 = P[n]
		var target := Quaternion.from_euler(Vector3(deg_to_rad(v.x), deg_to_rad(v.y), deg_to_rad(v.z)))
		Q[n] = (Q[n] as Quaternion).slerp(target, k)
		(J[n] as Node3D).quaternion = Q[n]
	pelvis.position.y = lerpf(pelvis.position.y, 0.36 - piv_cur + P.py, 1.0 - exp(-24.0 * dt))
	piv_cur = lerpf(piv_cur, piv_t, 1.0 - exp(-18.0 * dt))
	flipn.position.y = piv_cur
	if direct_flip:
		flip_cur = flip_t
	else:
		flip_cur = flip_cur.lerp(_wrap_near(flip_t, flip_cur), 1.0 - exp(-16.0 * dt))
		spin_yaw = 0.0 if not spinning else spin_yaw
	if not direct_flip and absf(flip_cur.x) > 360.0:
		flip_cur.x = fmod(flip_cur.x, 360.0)
	flipn.quaternion = Quaternion.from_euler(Vector3(deg_to_rad(flip_cur.x), deg_to_rad(flip_cur.y), deg_to_rad(flip_cur.z)))
	if not spinning and not direct_flip:
		spin_yaw = 0.0
	yaw = lerpf(yaw, yaw_t, 1.0 - exp(-15.0 * dt))
	rotation.y = deg_to_rad(yaw)
	# squash and stretch
	if gnd != prev_gnd and gnd == 0 and hvy < -300 and sqv == 0.0:
		sqv = 0.22
	prev_gnd = gnd
	sq = sqv
	sqv *= exp(-11.0 * dt)
	if absf(sqv) < 0.002:
		sqv = 0.0
	if st == Sim.GPSLAM:
		sq = maxf(sq, 0.16)
	ball = lerpf(ball, 0.8 if st == Sim.ROLL else 1.0, 1.0 - exp(-20.0 * dt))
	squash.scale = Vector3(1.0 - sq * 0.5, 1.0 + sq, 1.0 - sq * 0.5) * ball
	# the cap on his head: off while it flies
	cap_root.visible = cap_on
	cap_root.scale = Vector3.ONE * (1.0 + cap_pop * 0.35)
	_face(expr, dt, face)
	# blinking after a hit: every 4th frame white, as in the C game
	var fl2: float = 0.85 if (s.capx_inv > 0 and (s.fr & 4)) else 0.0
	for m in mats:
		m.set_shader_parameter("flash", fl2)


func _wrap_near(target: Vector3, cur: Vector3) -> Vector3:
	var r := target
	for i in 3:
		while r[i] - cur[i] > 180.0: r[i] -= 360.0
		while r[i] - cur[i] < -180.0: r[i] += 360.0
	return r


func _throw_overlay(P: Dictionary, face: int) -> void:
	var u := clampf(throw_t / 0.32, 0.0, 1.0)
	var arm := "R" if face > 0 else "L"
	var mz := -1.0 if arm == "R" else 1.0
	var sh: Vector3
	var el: Vector3
	match throw_kind:
		Sim.CAPUP:
			sh = Vector3(lerpf(-30.0, -178.0, smoothstep(0.0, 0.5, u)), 0, 10 * -mz)
			el = Vector3(-10, 0, 0)
			P.neck = Vector3(-25, 0, 0)
			P.spine = Vector3(-8, 0, 0)
		Sim.CAPDOWN:
			sh = Vector3(lerpf(-160.0, -10.0, smoothstep(0.0, 0.5, u)), 0, 15 * -mz)
			el = Vector3(-10, 0, 0)
			P.spine = Vector3(22, 0, 0)
			P.neck = Vector3(20, 0, 0)
		_:
			var wind := smoothstep(0.0, 0.25, u)
			var swing := smoothstep(0.2, 0.55, u)
			sh = Vector3(lerpf(lerpf(0.0, 40.0, wind), -95.0, swing), 0, lerpf(lerpf(10.0, 75.0, wind), 20.0, swing) * -mz)
			el = Vector3(lerpf(-60.0, -5.0, swing), 0, 0)
			P.spine = Vector3(P.spine.x + 4, lerpf(28.0, -22.0, swing) * -mz, 0)
	var out := sin(u * PI) if u > 0.6 else 1.0
	var w := clampf(out, 0.0, 1.0)
	P["sh" + arm] = P["sh" + arm].lerp(sh, w)
	P["el" + arm] = P["el" + arm].lerp(el, w)


func _win(P: Dictionary, s) -> void:
	if s.wphase == 0:   # sliding down the pole
		_arm(P, "L", Vector3(-120, 0, 10), Vector3(-50, 0, 0))
		_arm(P, "R", Vector3(-100, 0, 10), Vector3(-60, 0, 0))
		_leg(P, "L", Vector3(-50, 0, 10), 70, 0)
		_leg(P, "R", Vector3(-30, 0, 10), 50, 0)
		P.spine = Vector3(-4, 0, 0)
	elif s.wphase == 1 and s.wt < 10:   # hop off
		_arm(P, "L", Vector3(-160, 0, 30), Vector3(-20, 0, 0))
		_arm(P, "R", Vector3(-160, 0, 30), Vector3(-20, 0, 0))
		_leg(P, "L", Vector3(-40, 0, 8), 60, 0)
		_leg(P, "R", Vector3(-20, 0, 8), 40, 0)
	else:   # the pose: a fist up, then waving the cap
		var wave := sin(t * 9.0)
		_arm(P, "L", Vector3(-10, 0, 35), Vector3(-110, 0, 0))   # hand on the hip
		_arm(P, "R", Vector3(-168, 0, 22 + wave * 16), Vector3(-25 - wave * 10, 0, 0))
		_leg(P, "L", Vector3(4, 0, 9), 6, -4)
		_leg(P, "R", Vector3(-8, 0, 9), 14, -4)
		P.spine = Vector3(-6, 0, wave * 3)
		P.neck = Vector3(-10, 0, wave * 4)


func _face(expr: String, dt: float, face: int) -> void:
	blink_t -= dt
	if blink_t <= 0.0:
		blink = 0.13
		blink_t = randf_range(1.6, 4.2)
	blink = maxf(0.0, blink - dt)
	var open := 1.0
	if blink > 0.0:
		open = 0.12
	var happy := expr == "happy"
	if happy: open = 0.35
	if expr == "dead": open = 0.25
	if expr == "hurt": open = 1.15
	for i in eyes.size():
		var e: Node3D = eyes[i]
		e.scale.y = lerpf(e.scale.y, open, 1.0 - exp(-30.0 * dt))
		var p: Node3D = pupils[i]
		var up := 0.012 if expr == "jump" else (-0.01 if expr == "worried" else 0.0)
		p.position.y = lerpf(p.position.y, -0.004 + up, 1.0 - exp(-10.0 * dt))
	var brow_r := 0.0
	var brow_y := 0.37
	match expr:
		"focus": brow_r = 14.0; brow_y = 0.36
		"worried", "hurt": brow_r = -16.0; brow_y = 0.385
		"happy": brow_y = 0.385
		"jump": brow_y = 0.38
	for i in brows.size():
		var b: Node3D = brows[i]
		var sx := 1.0 if i == 0 else -1.0
		b.position.y = lerpf(b.position.y, brow_y, 1.0 - exp(-14.0 * dt))
		b.rotation_degrees.z = lerpf(b.rotation_degrees.z, brow_r * sx, 1.0 - exp(-14.0 * dt))
	var ms := Vector3(1, 1, 1)
	match expr:
		"jump": ms = Vector3(0.8, 2.1, 1)
		"hurt", "dead": ms = Vector3(0.9, 2.6, 1)
		"happy": ms = Vector3(1.25, 1.6, 1)
		"focus": ms = Vector3(0.85, 0.8, 1)
		"worried": ms = Vector3(0.7, 1.4, 1)
	mouth.scale = mouth.scale.lerp(ms, 1.0 - exp(-16.0 * dt))
