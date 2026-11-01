// A pennant or banner on a subdivided plane: UV.x runs from the pole (0) to the tip (1). It
// waves more toward the tip; with tri = 1 the cloth is cut to a triangle.
shader_type spatial;
render_mode cull_disabled, diffuse_burley;

uniform vec4 albedo : source_color = vec4(0.18, 0.78, 0.36, 1.0);
uniform vec4 stripe : source_color = vec4(1.0, 0.95, 0.8, 1.0);
uniform float tri = 1.0;
uniform float amp = 0.12;
uniform float speed = 6.0;

void vertex() {
	float k = UV.x;
	VERTEX.z += sin(TIME * speed - k * 5.0) * amp * k;
	VERTEX.y += sin(TIME * speed * 0.7 - k * 3.0) * amp * 0.3 * k;
}

void fragment() {
	float half = 0.5 * (1.0 - UV.x * tri);
	if (abs(UV.y - 0.5) > half) discard;
	vec3 c = albedo.rgb;
	if (abs(UV.y - 0.5) > half - 0.06) c = mix(c, stripe.rgb, 0.6);
	c *= FRONT_FACING ? 1.0 : 0.85;
	ALBEDO = c;
	ROUGHNESS = 0.8;
	SPECULAR = 0.2;
}
