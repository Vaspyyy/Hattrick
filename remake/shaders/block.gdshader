// Blocks drawn from their UVs (0..1 across each face of a rounded box): stone, brick, crumble
// biscuit, the found block with its cap stamp, and iron. INSTANCE_CUSTOM.x shifts the shade a
// little per block (in a MultiMesh) so walls of the same block don't look stamped.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

uniform int pattern = 0;   // 0 stone, 1 brick, 2 crumble, 3 found, 4 iron, 5 plain
uniform vec4 base : source_color = vec4(0.6, 0.62, 0.68, 1.0);
uniform vec4 dark : source_color = vec4(0.35, 0.37, 0.44, 1.0);
uniform vec4 light : source_color = vec4(0.82, 0.85, 0.9, 1.0);
uniform float rough = 0.8;
uniform float metal = 0.0;
uniform vec4 glow : source_color = vec4(0.0, 0.0, 0.0, 1.0);

varying float vary;
varying vec3 lpos;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

void vertex() {
	vary = INSTANCE_CUSTOM.x;
	lpos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	vec2 uv = UV;
	vec3 col = base.rgb;
	float r = rough;
	vec3 em = glow.rgb;
	if (pattern == 0) {   // stone: a chiselled block with a soft inner panel
		float n = vnoise(uv * 6.0 + lpos.xy * 0.37) * 0.6 + vnoise(uv * 17.0 + lpos.yz) * 0.4;
		vec2 e = min(uv, 1.0 - uv);
		float inset = smoothstep(0.10, 0.16, min(e.x, e.y));
		col = mix(dark.rgb, base.rgb, 0.65 + 0.35 * inset);
		col = mix(col, light.rgb, (1.0 - smoothstep(0.0, 0.1, min(e.x, e.y))) * 0.25);
		col *= 0.9 + 0.2 * n;
		col *= 0.93 + 0.14 * vary;
	} else if (pattern == 1) {   // brick: two courses, the upper one offset
		float row = floor(uv.y * 2.0);
		float u = fract(uv.x * 2.0 + row * 0.5);
		float v = fract(uv.y * 2.0);
		float mortar = max(smoothstep(0.07, 0.03, min(v, 1.0 - v)), smoothstep(0.06, 0.02, min(u, 1.0 - u)));
		float n = vnoise(uv * 11.0 + lpos.xy);
		vec3 b = mix(base.rgb, light.rgb, smoothstep(0.75, 0.95, 1.0 - v) * 0.5) * (0.9 + 0.2 * n);
		col = mix(b, dark.rgb, mortar);
		r = mix(0.75, 0.95, mortar);
	} else if (pattern == 2) {   // crumble: a cracked biscuit
		float n = vnoise(uv * 9.0 + lpos.xy * 0.7);
		col = base.rgb * (0.9 + 0.2 * n);
		float c1 = abs(uv.y - 0.42 - 0.08 * sin(uv.x * 9.0));
		float c2 = abs(uv.x - 0.55 - 0.06 * sin(uv.y * 11.0)) + step(uv.y, 0.42) * 2.0;
		float crack = smoothstep(0.035, 0.012, min(c1 + step(0.65, uv.x) * 2.0, c2));
		col = mix(col, dark.rgb, crack);
		for (int i = 0; i < 5; i++) {   // little dimples
			vec2 p = vec2(hash(vec2(float(i), 1.0)), hash(vec2(float(i), 2.0))) * 0.7 + 0.15;
			col = mix(col, dark.rgb * 1.2, smoothstep(0.045, 0.025, length(uv - p)) * 0.7);
		}
	} else if (pattern == 3) {   // found block: a teal panel with a cap stamp
		vec2 e = min(uv, 1.0 - uv);
		float inset = smoothstep(0.08, 0.13, min(e.x, e.y));
		col = mix(dark.rgb, base.rgb, inset);
		vec2 p = uv - vec2(0.5, 0.55);
		float dome = step(length(p * vec2(1.0, 1.35)), 0.22) * step(p.y, 0.02);
		float brim = step(abs(p.y - 0.04), 0.04) * step(p.x, 0.32) * step(-0.24, p.x);
		col = mix(col, dark.rgb * 1.15, max(dome, brim) * inset);
		col = mix(col, light.rgb, (1.0 - smoothstep(0.0, 0.06, min(e.x, e.y))) * 0.4);
	} else if (pattern == 4) {   // iron with rivets
		vec2 e = min(uv, 1.0 - uv);
		col = mix(dark.rgb, base.rgb, smoothstep(0.05, 0.12, min(e.x, e.y)));
		vec2 q = abs(uv - 0.5);
		float rivet = smoothstep(0.06, 0.04, length(q - vec2(0.32)));
		col = mix(col, light.rgb, rivet);
		vec2 c = uv - 0.5;
		em += vec3(1.0, 0.55, 0.15) * smoothstep(0.16, 0.1, length(c)) * 1.5;
	}
	ALBEDO = col;
	ROUGHNESS = r;
	METALLIC = metal;
	SPECULAR = 0.35;
	EMISSION = em;
}
