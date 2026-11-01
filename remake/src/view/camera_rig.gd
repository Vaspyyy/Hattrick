# The camera: frames the same 32 x 18 tiles as the C game's 256 x 144 screen and follows the
# sim's own camera (look-ahead and the Mario-style vertical band), from a little above, so the
# ground's top and the depth of the world show. Shake follows the sim's `shake` counter.
extends Camera3D

const FOV := 30.0
const PITCH := 7.0
const VIEW_H := 17.0   # tiles visible top to bottom on the play plane (the C game shows 18)
var dist := 0.0
var t := 0.0
var punch := 0.0       # an extra kick from the view (landing a ground pound, a boss hit)


func _ready() -> void:
	fov = FOV
	near = 0.5
	far = 400.0
	dist = VIEW_H * 0.5 / tan(deg_to_rad(FOV * 0.5))


# center: the point of the play plane at the middle of the screen (world units)
func follow(center: Vector3, shake: int, dt: float) -> void:
	t += dt
	punch = maxf(0.0, punch - dt * 3.0)
	var s := float(shake * shake) / 30.0 * 0.06 + punch * 0.25
	var off := Vector3(sin(t * 57.0) * s * 0.5, sin(t * 71.0 + 1.0) * s, 0)
	var p := deg_to_rad(PITCH)
	var target := center + off
	var pos := target + Vector3(0, sin(p) * dist, cos(p) * dist)
	look_at_from_position(pos, target, Vector3.UP)
