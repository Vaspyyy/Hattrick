# Keyboard and controller to the C game's key bits (CONTROLS.md), read once per step.
# Keyboard: arrows, Z / Y / Space jump, X throws, C is the second cap button (recall, down
# throw), R restarts. Controller: A / B jump, Y throws, X is the second cap button, shoulders and
# triggers crouch, left stick (analog speed) or D-pad move.
extends RefCounted

const Sim := preload("res://src/sim/sim.gd")
const DEADZONE := 0.22
const TRIGGER := 0.25
# rumble 1..8 (sim.rumq): weak motor, strong motor (0..1), seconds; from the C game's table
const RUM := [[0.28, 0.0, 0.05], [0.22, 0.19, 0.07], [0.19, 0.31, 0.08], [0.31, 0.41, 0.1],
	[0.44, 0.38, 0.15], [0.56, 0.72, 0.18], [0.75, 1.0, 0.38], [0.56, 0.31, 0.45]]

var last_pad := -1


static func _key(k: Key) -> bool:
	return Input.is_physical_key_pressed(k)


func keys() -> int:
	var k := 0
	if _key(KEY_LEFT): k |= Sim.K_LEFT
	if _key(KEY_RIGHT): k |= Sim.K_RIGHT
	if _key(KEY_UP): k |= Sim.K_UP
	if _key(KEY_DOWN): k |= Sim.K_DOWN
	if _key(KEY_Z) or _key(KEY_Y) or _key(KEY_SPACE): k |= Sim.K_JUMP
	if _key(KEY_X): k |= Sim.K_CAP
	if _key(KEY_C): k |= Sim.CAP2
	var axis := 0
	for d in Input.get_connected_joypads():
		var any := false
		if Input.is_joy_button_pressed(d, JOY_BUTTON_A) or Input.is_joy_button_pressed(d, JOY_BUTTON_B):
			k |= Sim.K_JUMP; any = true
		if Input.is_joy_button_pressed(d, JOY_BUTTON_Y):
			k |= Sim.K_CAP; any = true
		if Input.is_joy_button_pressed(d, JOY_BUTTON_X):
			k |= Sim.CAP2; any = true
		if Input.is_joy_button_pressed(d, JOY_BUTTON_LEFT_SHOULDER) or Input.is_joy_button_pressed(d, JOY_BUTTON_RIGHT_SHOULDER) \
				or Input.get_joy_axis(d, JOY_AXIS_TRIGGER_LEFT) > TRIGGER or Input.get_joy_axis(d, JOY_AXIS_TRIGGER_RIGHT) > TRIGGER:
			k |= Sim.K_DOWN | Sim.PADCROUCH; any = true
		if Input.is_joy_button_pressed(d, JOY_BUTTON_DPAD_LEFT): k |= Sim.K_LEFT; any = true
		if Input.is_joy_button_pressed(d, JOY_BUTTON_DPAD_RIGHT): k |= Sim.K_RIGHT; any = true
		if Input.is_joy_button_pressed(d, JOY_BUTTON_DPAD_UP): k |= Sim.K_UP; any = true
		if Input.is_joy_button_pressed(d, JOY_BUTTON_DPAD_DOWN): k |= Sim.K_DOWN; any = true
		var x := Input.get_joy_axis(d, JOY_AXIS_LEFT_X)
		var y := Input.get_joy_axis(d, JOY_AXIS_LEFT_Y)
		var a := 0
		if absf(x) > DEADZONE:
			a = int(clampf((absf(x) - DEADZONE) / (1.0 - DEADZONE - 0.05), 0.0, 1.0) * 256.0) * signi(int(signf(x)))
			any = true
		if absi(a) > absi(axis): axis = a
		if y < -0.5: k |= Sim.K_UP; any = true
		if y > 0.5: k |= Sim.K_DOWN; any = true
		if any: last_pad = d
	if axis:
		k |= Sim.ANALOG | ((axis + 256) << 10)
	return k


func rumble(level: int) -> void:
	if level < 1 or level > 8:
		return
	var r: Array = RUM[level - 1]
	for d in Input.get_connected_joypads():
		Input.start_joy_vibration(d, r[0], r[1], r[2])
