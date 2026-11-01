// Characters and props: a soft cel terminator, a warm rim and a hit flash.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

uniform vec4 albedo : source_color = vec4(1.0);
uniform float roughness : hint_range(0.0, 1.0) = 0.55;
uniform float rim : hint_range(0.0, 2.0) = 0.35;
uniform vec4 rim_color : source_color = vec4(1.0, 0.96, 0.88, 1.0);
uniform float terminator : hint_range(0.01, 1.0) = 0.22;
uniform float shine : hint_range(0.0, 1.0) = 0.25;
uniform vec4 emission : source_color = vec4(0.0, 0.0, 0.0, 1.0);
uniform float flash : hint_range(0.0, 1.0) = 0.0;

void fragment() {
	ALBEDO = mix(albedo.rgb, vec3(1.0), flash);
	ROUGHNESS = roughness;
	SPECULAR = 0.3;
	float f = pow(1.0 - clamp(dot(NORMAL, VIEW), 0.0, 1.0), 3.0);
	EMISSION = emission.rgb + rim_color.rgb * f * rim * 0.45 * albedo.rgb + vec3(flash) * 0.5;
}

void light() {
	float ndl = dot(NORMAL, LIGHT);
	float d = smoothstep(-terminator * 0.4, terminator, ndl);
	DIFFUSE_LIGHT += d * ATTENUATION * LIGHT_COLOR / PI;
	vec3 hv = normalize(VIEW + LIGHT);
	float s = pow(clamp(dot(NORMAL, hv), 0.0, 1.0), mix(12.0, 90.0, 1.0 - roughness));
	SPECULAR_LIGHT += smoothstep(0.55, 0.75, s) * shine * ATTENUATION * LIGHT_COLOR / PI;
}
