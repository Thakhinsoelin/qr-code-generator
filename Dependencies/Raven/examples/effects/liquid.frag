#version 450
#extension GL_GOOGLE_include_directive : require
#include "raven_effect.glsl"

/* Liquid glass: what is behind wobbles, as seen through moving water.
 * Needs the backdrop: EffectBackdrop="true" (R_SHADER_BACKDROP). params.x = strength in pixels. */
vec4 raven_effect(RavenEffect fx) {
	float strength = fx.params.x > 0.0 ? fx.params.x : 6.0;
	vec2 flow = vec2(raven_fbm(fx.uv * 3.0 + fx.time * 0.25), raven_fbm(fx.uv * 3.0 - fx.time * 0.2 + 7.0)) - 0.5;
	vec3 seen = raven_backdrop(fx.screen + flow * strength * 2.0).rgb;
	/* the edge catches the light */
	float rim = smoothstep(-6.0, 0.0, fx.edge);
	return vec4(mix(seen, fx.color.rgb, fx.color.a) + rim * 0.12, 1.0);
}
