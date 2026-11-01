// Molten lava: slow-crawling bright veins over a dark crust, brightest at the surface and fading
// with depth on the front face. top_local: the box's top in its own space, per instance.
shader_type spatial;
render_mode cull_back;

uniform vec4 hot : source_color = vec4(1.0, 0.62, 0.16, 1.0);
uniform vec4 glow : source_color = vec4(1.0, 0.3, 0.04, 1.0);
uniform vec4 crust : source_color = vec4(0.16, 0.04, 0.03, 1.0);
instance uniform float top_local = 0.5;

varying vec3 wp;
varying float ly;

void vertex() {
	wp = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	ly = VERTEX.y;
}

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
	vec2 i = floor(p), f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

void fragment() {
	vec3 n = (INV_VIEW_MATRIX * vec4(NORMAL, 0.0)).xyz;
	bool top = n.y > 0.5;
	vec2 q = top ? wp.xz : vec2(wp.x, wp.y * 1.6);
	float t = TIME * 0.22;
	float f = noise(q * 1.1 + vec2(t, -t * 0.6)) * 0.6 + noise(q * 2.7 - vec2(t * 1.4, t * 0.8)) * 0.4;
	float veins = smoothstep(0.42, 0.7, f);
	float depth = top ? 0.0 : clamp((top_local - ly) / 2.5, 0.0, 1.0);
	float rim = top ? 0.0 : smoothstep(0.18, 0.0, top_local - ly);   // the bright lip at the surface
	vec3 e = mix(glow.rgb * 0.9, hot.rgb * 2.2, veins);
	e *= mix(1.0, 0.35, depth);
	e += hot.rgb * rim * 2.0;
	ALBEDO = mix(crust.rgb, hot.rgb, veins * 0.4);
	EMISSION = e;
	ROUGHNESS = mix(0.9, 0.35, veins);
}
