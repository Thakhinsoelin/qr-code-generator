#version 450
#extension GL_GOOGLE_include_directive : require
#include "raven_effect.glsl"

/* Ripple: rings run over what is behind, like water. Needs the backdrop.
 * color: tint (its alpha is its strength). params: speed, strength (pixels), density, center x (0..1).
 * The pointer: the rings start from it, and a press makes them stronger. */
vec4 raven_effect(RavenEffect fx) {
	float speed = fx.params.x, strength = fx.params.y, density = max(fx.params.z, 0.1);
	vec2 center = vec2(fx.params.w, 0.5) * fx.size;
	if (raven_has_pointer(fx)) {
		center = mix(center, fx.pointer, fx.hover);
		strength *= 1.0 + fx.press * 1.5;
	}
	vec2 delta = fx.pixel - center;
	float dist = length(delta);
	vec2 direction = dist > 0.0 ? delta / dist : vec2(0.0);
	float scale = min(fx.size.x, fx.size.y);
	float phase = dist / scale * density * 6.0 - fx.time * speed * 4.0;
	/* the rings fade toward the edge, and come in trains */
	float fade = exp(-dist / scale * 1.2) * (0.6 + 0.4 * sin(phase * 0.25));
	float wave = sin(phase) * fade;
	vec2 offset = direction * wave * strength;
	vec3 seen = raven_backdrop(fx.screen + offset).rgb;
	/* the crests catch the light */
	float crest = max(cos(phase), 0.0) * fade;
	/* under the pointer the crests shine more, and the water glows a little */
	float lit = 1.0 + fx.hover * 2.0 + fx.press * 2.0;
	vec3 color = mix(seen, fx.color.rgb, fx.color.a) + vec3(crest * 0.08 * lit)
		+ fx.color.rgb * fx.hover * 0.14 * exp(-dist / scale * 3.0);
	return vec4(color, 1.0);
}
