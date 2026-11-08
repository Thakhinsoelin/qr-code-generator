#version 450
#extension GL_GOOGLE_include_directive : require
#include "raven_effect.glsl"

/* Aurora: curtains of light drifting in a night sky.
 * color, color2: the colors of the lights (color low, color2 high). params: speed, scale, intensity.
 * The pointer: the curtains lean toward it and light up around it; a press makes them flare. */
vec4 raven_effect(RavenEffect fx) {
	float speed = fx.params.x, scale = max(fx.params.y, 0.05), intensity = fx.params.z;
	float aspect = fx.size.x / max(fx.size.y, 1.0);
	float x = fx.uv.x * aspect * scale;
	float t = fx.time * 0.08 * speed;

	/* the night: almost black, a little of the two colors */
	vec3 sky = mix(vec3(0.015, 0.02, 0.05), mix(fx.color.rgb, fx.color2.rgb, 0.7) * 0.08, 1.0 - fx.uv.y);
	vec3 light = vec3(0.0);
	/* how close the pointer is (across mostly: the curtains hang), and where it is */
	vec2 m = fx.pointer / max(fx.size, vec2(1.0));
	float across = (fx.uv.x - m.x) * aspect * 2.2;
	float near = raven_has_pointer(fx) ? fx.hover * exp(-across * across) : 0.0;
	for (int i = 0; i < 3; i++) {
		float k = float(i);
		/* the lower edge of a curtain: a line that waves slowly */
		float line = 0.62 - k * 0.13
			+ 0.07 * sin(x * (2.1 + k * 0.7) + t * (3.0 + k) + k * 1.7)
			+ 0.10 * (raven_fbm(vec2(x * 1.1 + t * (1.2 + k * 0.4), k * 4.3)) - 0.5);
		/* the edge leans toward the pointer */
		line = mix(line, clamp(m.y, 0.15, 0.9), near * (0.35 - k * 0.08));
		float d = fx.uv.y - line;
		/* bright at its edge, fading slowly upward and quickly downward */
		float curtain = d < 0.0 ? exp(d * (5.0 + k * 1.5)) : exp(-d * 40.0);
		/* folds: vertical streaks that come and go */
		float folds = 0.55 + 0.45 * raven_fbm(vec2(x * 9.0 + k * 13.0, t * 2.0 + k));
		/* color: the first at the edge, the second higher up */
		vec3 tone = mix(fx.color.rgb, fx.color2.rgb, clamp(-d * 3.0 + 0.15 * k, 0.0, 1.0));
		light += tone * curtain * folds * (0.55 - k * 0.12);
	}
	/* a few stars in the dark */
	vec2 cell = floor(fx.pixel / 3.0);
	float star = step(0.9975, raven_hash(cell)) * (0.5 + 0.5 * sin(fx.time * 2.0 + raven_hash(cell + 7.0) * 30.0));
	light *= 1.0 + near * (0.9 + fx.press * 1.2);
	vec3 color = sky + light * intensity + vec3(star * 0.6) * (1.0 - clamp(length(light) * 2.0, 0.0, 1.0));
	/* (a little noise keeps the gradient smooth on 8-bit screens) */
	color += (raven_hash(fx.pixel + fract(t) * 91.0) - 0.5) / 255.0;
	return vec4(color, 1.0);
}
