# The sky, sun and post-processing: bright, warm and soft, like a sunny Nintendo morning.
# Caves get a dim, warm look with a lantern following Hatrick.
extends Node3D

var env: Environment
var sun: DirectionalLight3D
var we: WorldEnvironment
var sky_mat: ProceduralSkyMaterial
var lantern: OmniLight3D
var mode := ""


func _ready() -> void:
	we = WorldEnvironment.new()
	env = Environment.new()
	sky_mat = ProceduralSkyMaterial.new()
	var sky := Sky.new()
	sky.sky_material = sky_mat
	sky.radiance_size = Sky.RADIANCE_SIZE_128
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.tonemap_white = 6.0
	env.tonemap_exposure = 1.0
	env.ssao_enabled = true
	env.ssao_radius = 0.9
	env.ssao_intensity = 1.6
	env.ssao_power = 1.4
	env.ssao_detail = 0.6
	env.ssil_enabled = false
	env.glow_enabled = true
	env.glow_intensity = 0.5
	env.glow_strength = 0.9
	env.glow_bloom = 0.04
	env.glow_hdr_threshold = 1.1
	env.glow_blend_mode = Environment.GLOW_BLEND_MODE_SOFTLIGHT
	env.adjustment_enabled = true
	env.adjustment_saturation = 1.12
	env.adjustment_contrast = 1.04
	env.fog_enabled = true
	env.fog_mode = Environment.FOG_MODE_DEPTH
	env.fog_depth_begin = 30.0
	env.fog_depth_end = 190.0
	env.fog_depth_curve = 0.8
	env.fog_sky_affect = 0.0
	we.environment = env
	add_child(we)
	sun = DirectionalLight3D.new()
	sun.shadow_enabled = true
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_2_SPLITS
	sun.directional_shadow_max_distance = 70.0
	sun.shadow_blur = 1.4
	sun.shadow_normal_bias = 1.2
	sun.light_angular_distance = 1.5
	add_child(sun)
	# from the upper left, a little in front: shadows fall back onto the ground behind Hatrick
	sun.look_at_from_position(Vector3.ZERO, Vector3(0.42, -0.78, -0.62), Vector3.UP)
	lantern = OmniLight3D.new()
	lantern.omni_range = 9.0
	lantern.light_energy = 0.0
	lantern.light_color = Color(1.0, 0.82, 0.6)
	lantern.shadow_enabled = false
	add_child(lantern)
	set_mode("sky")


func set_mode(m: String) -> void:
	if m == mode:
		return
	mode = m
	match m:
		"cave", "dark":
			sky_mat.sky_top_color = Color(0.09, 0.07, 0.09)
			sky_mat.sky_horizon_color = Color(0.16, 0.12, 0.12)
			sky_mat.ground_bottom_color = Color(0.05, 0.04, 0.04)
			sky_mat.ground_horizon_color = Color(0.16, 0.12, 0.12)
			sky_mat.sun_angle_max = 1.0
			sun.light_energy = 0.35 if m == "cave" else 0.08
			sun.light_color = Color(0.85, 0.75, 0.9)
			# no sun underground: as a fill light it would throw the ceiling's and every block's
			# shadow onto the back wall as black bands
			sun.shadow_enabled = false
			env.ambient_light_energy = 0.7 if m == "cave" else 0.25
			env.fog_light_color = Color(0.1, 0.07, 0.07)
			env.fog_density = 0.3
			lantern.light_energy = 1.8 if m == "cave" else 2.6
		"night":   # gimmicks.h's night areas: the C game tints everything a dim blue
			sky_mat.sky_top_color = Color(0.03, 0.05, 0.14)
			sky_mat.sky_horizon_color = Color(0.16, 0.22, 0.4)
			sky_mat.ground_bottom_color = Color(0.04, 0.06, 0.1)
			sky_mat.ground_horizon_color = Color(0.16, 0.22, 0.4)
			sky_mat.sun_angle_max = 1.0
			sun.light_energy = 0.42
			sun.light_color = Color(0.62, 0.72, 1.0)
			sun.shadow_enabled = true
			env.ambient_light_energy = 0.45
			env.fog_light_color = Color(0.09, 0.12, 0.25)
			env.fog_density = 0.45
			lantern.light_energy = 1.2
		_:
			sky_mat.sky_top_color = Color(0.22, 0.52, 0.93)
			sky_mat.sky_horizon_color = Color(0.72, 0.87, 0.99)
			sky_mat.sky_curve = 0.12
			sky_mat.ground_bottom_color = Color(0.3, 0.55, 0.32)
			sky_mat.ground_horizon_color = Color(0.72, 0.87, 0.99)
			sky_mat.sun_angle_max = 30.0
			sky_mat.sun_curve = 0.1
			sun.light_energy = 1.25
			sun.light_color = Color(1.0, 0.96, 0.88)
			sun.shadow_enabled = true
			env.ambient_light_energy = 0.75
			env.ambient_light_sky_contribution = 1.0
			env.fog_light_color = Color(0.74, 0.86, 0.98)
			env.fog_density = 0.55
			lantern.light_energy = 0.0
