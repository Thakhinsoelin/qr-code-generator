#version 450
#extension GL_GOOGLE_include_directive : require
#include "raven_effect.glsl"

/* Plasma: bands of color that flow into each other.
 * color, color2: the two ends of the palette. params: speed, scale, contrast.
 * The pointer: the bands swirl around it, faster when it presses. */
vec4 raven_effect(RavenEffect fx) {
	float speed = fx.params.x, scale = max(fx.params.y, 0.05), contrast = max(fx.params.z, 0.1);
	float aspect = fx.size.x / max(fx.size.y, 1.0);
	vec2 p = (fx.uv - 0.5) * vec2(aspect, 1.0) * 5.0 * scale;
	float t = fx.time * 0.6 * speed;
	if (raven_has_pointer(fx)) {
		/* a whirl centered on the pointer, strongest at its middle */
		vec2 m = (fx.pointer / max(fx.size, vec2(1.0)) - 0.5) * vec2(aspect, 1.0) * 5.0 * scale;
		vec2 r = p - m;
		float turn = fx.hover * (1.6 + fx.press * 2.0) * exp(-dot(r, r) * 0.35);
		float cs = cos(turn), sn = sin(turn);
		p = m + mat2(cs, sn, -sn, cs) * r;
	}
	float v = sin(p.x + t);
	v += sin((p.y + t) * 0.7);
	v += sin((p.x + p.y + t) * 0.6);
	v += sin(length(p + vec2(sin(t * 0.33), cos(t * 0.5)) * 2.0) * 1.4 - t);
	v = 0.5 + 0.5 * sin(v * 0.8 * contrast);
	/* the middle of the palette goes through a third color: the two mixed and brightened */
	vec3 middle = clamp(mix(fx.color.rgb, fx.color2.rgb, 0.5) * 1.35 + 0.08, 0.0, 1.0);
	vec3 color = v < 0.5 ? mix(fx.color.rgb, middle, v * 2.0) : mix(middle, fx.color2.rgb, v * 2.0 - 1.0);
	return vec4(color, mix(fx.color.a, fx.color2.a, v));
}
