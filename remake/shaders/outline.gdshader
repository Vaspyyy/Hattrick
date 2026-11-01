// Inverted hull: a slightly bigger back-facing copy in a darker shade of the part's colour.
shader_type spatial;
render_mode unshaded, cull_front, depth_draw_opaque, shadows_disabled;

uniform vec4 color : source_color = vec4(0.12, 0.06, 0.05, 1.0);
uniform float width = 0.018;

void vertex() {
	VERTEX += NORMAL * width;
}

void fragment() {
	ALBEDO = color.rgb;
}
