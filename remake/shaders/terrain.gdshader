// The ground: a grass top and a grass band that hangs over the dirt front with a wavy edge, dirt
// with soft strata and pebbles below. UV2.x is 1 on the grass top, UV2.y the depth below the
// surface (tiles), so the band and the shade under it follow the terrain without textures.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

uniform vec4 grass_hi : source_color = vec4(0.62, 0.90, 0.38, 1.0);
uniform vec4 grass_lo : source_color = vec4(0.30, 0.70, 0.22, 1.0);
uniform vec4 dirt_hi : source_color = vec4(0.72, 0.44, 0.20, 1.0);
uniform vec4 dirt_lo : source_color = vec4(0.50, 0.29, 0.14, 1.0);
uniform vec4 pebble : source_color = vec4(0.86, 0.64, 0.38, 1.0);
uniform float grass_on = 1.0;
uniform float tint_ice = 0.0;

varying vec3 wpos;
varying vec3 wnrm;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
vec2 hash2(vec2 p) { return fract(sin(vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)))) * 43758.5453); }
float vnoise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}
// distance to the nearest random point, and that point's id
vec2 cells(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	float best = 9.0;
	float id = 0.0;
	for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) {
		vec2 g = vec2(float(x), float(y));
		vec2 o = hash2(i + g);
		vec2 d = g + 0.15 + o * 0.7 - f;
		float dd = dot(d, d);
		if (dd < best) { best = dd; id = hash(i + g + 7.0); }
	}
	return vec2(sqrt(best), id);
}

void vertex() {
	wpos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	wnrm = normalize((MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz);
}

void fragment() {
	vec3 col;
	float depth = UV2.y;
	// the face's own 2D coordinates: front faces use x/y, side faces z/y, the top x/z
	vec2 fp = abs(wnrm.x) > 0.6 ? wpos.zy : wpos.xy;
	float wave = 0.28 + 0.07 * sin(wpos.x * 4.7 + wpos.z * 3.0) + 0.04 * sin(wpos.x * 11.3 + 1.7) + 0.06 * vnoise(vec2(wpos.x * 2.6 + wpos.z * 2.0, 3.0));
	if (grass_on > 0.5 && UV2.x > 0.5) {
		float n = vnoise(wpos.xz * vec2(1.7, 2.3));
		float tuft = vnoise(wpos.xz * 7.0);
		col = mix(grass_lo.rgb, grass_hi.rgb, 0.55 + 0.3 * n);
		col *= 0.92 + 0.12 * tuft;
		col = mix(col, grass_hi.rgb * 1.06, smoothstep(0.2, 1.1, wpos.z) * 0.6);
		col = mix(col, vec3(0.86, 0.95, 1.0), tint_ice * 0.75);
	} else if (grass_on > 0.5 && depth < wave) {
		float k = depth / wave;
		col = mix(grass_hi.rgb * 0.96, grass_lo.rgb * 0.92, k);
		col *= 0.95 + 0.08 * vnoise(fp * vec2(9.0, 2.0));
		col = mix(col, vec3(0.80, 0.92, 1.0), tint_ice * 0.7);
	} else {
		float strata = vnoise(vec2(fp.x * 0.35, fp.y * 1.6)) * 0.7 + vnoise(fp * 3.1) * 0.3;
		col = mix(dirt_lo.rgb, dirt_hi.rgb, smoothstep(0.25, 0.8, strata));
		vec2 c = cells(fp * 2.2);
		float peb = smoothstep(0.30, 0.24, c.x) * step(0.55, c.y);
		col = mix(col, mix(pebble.rgb, dirt_lo.rgb * 0.8, step(0.8, c.y)), peb * 0.85);
		// a soft shade just under the grass, and deeper down
		if (grass_on > 0.5) col *= mix(0.62, 1.0, smoothstep(wave, wave + 0.35, depth));
		col *= mix(1.0, 0.72, smoothstep(2.0, 9.0, depth));
	}
	ALBEDO = col;
	ROUGHNESS = 0.92;
	SPECULAR = 0.25;
}
