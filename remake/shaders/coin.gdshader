// A coin spinning on its vertical axis in the vertex shader, so a level of coins costs no script.
// All coins turn in step (as in the C game), with a warm glow so they read against the grass.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

uniform vec4 albedo : source_color = vec4(1.0, 0.78, 0.22, 1.0);
uniform vec4 edge : source_color = vec4(0.85, 0.5, 0.08, 1.0);
uniform float speed = 2.6;
uniform float glow = 0.18;
uniform float offset = 0.0;
uniform float alpha = 1.0;

void vertex() {
	float a = TIME * speed + offset + NODE_POSITION_WORLD.x * 0.45;
	float c = cos(a);
	float s = sin(a);
	mat3 r = mat3(vec3(c, 0.0, -s), vec3(0.0, 1.0, 0.0), vec3(s, 0.0, c));
	VERTEX = r * VERTEX;
	NORMAL = r * NORMAL;
}

void fragment() {
	float f = pow(1.0 - clamp(dot(NORMAL, VIEW), 0.0, 1.0), 2.0);
	vec3 c = mix(albedo.rgb, edge.rgb, f * 0.8);
	ALBEDO = c;
	METALLIC = 0.0;
	ROUGHNESS = 0.25;
	SPECULAR = 0.7;
	EMISSION = albedo.rgb * glow + vec3(1.0, 0.85, 0.4) * f * 0.6;
	ALPHA = alpha;
}
