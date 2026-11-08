/*
 * Raven - raven_effect.glsl
 * What an effect (raven_shader.h) is written with. Include it, then write
 * one function:
 *
 *     #version 450
 *     #extension GL_GOOGLE_include_directive : require
 *     #include "raven_effect.glsl"
 *
 *     vec4 raven_effect(RavenEffect fx) {
 *         return mix(fx.color, fx.color2, fx.uv.x);   // straight alpha, 0..1
 *     }
 *
 * Raven calls it for every pixel of the rectangle. The rounded corners, the
 * anti-aliased edge, the clipping of the widget and its opacity are applied
 * to what it returns: the effect only says what color the pixel is.
 *
 * Compile it to SPIR-V:
 *     glslangValidator -V -I<this folder> effect.frag -o effect.spv
 *     glslc -I<this folder> effect.frag -o effect.spv
 *
 * Everything below whose name starts with raven_ is for the effect to use;
 * the names that start with rvn_ belong to Raven and may change.
 */

#ifndef RAVEN_EFFECT_GLSL
#define RAVEN_EFFECT_GLSL

/* ---- what Raven gives (the inputs of its vertex shader: do not change) ---- */

layout(set = 0, binding = 0) uniform sampler2D rvn_texture;
layout(set = 1, binding = 0) uniform sampler2D rvn_backdrop;
layout(set = 1, binding = 1) uniform sampler2D rvn_backdrop_blur;

layout(location = 0) in vec2 rvn_v_uv;
layout(location = 1) flat in vec4 rvn_v_rect;
layout(location = 2) flat in vec4 rvn_v_clip;
layout(location = 3) flat in vec4 rvn_v_radii;
layout(location = 4) flat in vec4 rvn_v_color;
layout(location = 5) flat in vec4 rvn_v_color2;
layout(location = 6) flat in float rvn_v_time;
layout(location = 7) flat in uint rvn_v_flags;
layout(location = 8) flat in vec4 rvn_v_params;
layout(location = 9) flat in vec2 rvn_v_inv_size;
layout(location = 10) flat in vec4 rvn_v_pointer;

layout(location = 0) out vec4 rvn_out;

/* ---- what the effect receives ---- */

struct RavenEffect {
	/** Where the pixel is in the rectangle: (0, 0) top left .. (1, 1) bottom right. */
	vec2 uv;
	/** The same in pixels, from the top left corner of the rectangle. */
	vec2 pixel;
	/** Size of the rectangle, in pixels. */
	vec2 size;
	/** Where the pixel is in the window, in pixels (for raven_backdrop). */
	vec2 screen;
	/** Distance to the edge of the rounded rectangle, in pixels: negative inside, 0 on the edge. */
	float edge;
	/** Seconds: grows while the effect is animated. */
	float time;
	/**
	 * Where the pointer is, in pixels from the top left corner of the
	 * rectangle, like `pixel` (it may be outside the rectangle). Far away
	 * (below -1e5) when the pointer is not over the window, or when the
	 * effect is told not to follow it (RShaderLook::pointer, EffectPointer).
	 */
	vec2 pointer;
	/** How much the pointer is over the widget, 0..1: eased, it rises and falls in a fraction of a second. */
	float hover;
	/** How much the widget is pressed, 0..1 (eased). */
	float press;
	/** The two colors of the look, straight alpha. */
	vec4 color;
	vec4 color2;
	/** The four parameters of the look. */
	vec4 params;
};

/** The effect: the color of the pixel, straight alpha (not premultiplied). */
vec4 raven_effect(RavenEffect fx);

/* ---- helpers ---- */

const float RAVEN_PI = 3.14159265358979;
const float RAVEN_TAU = 6.28318530717959;

/** The texture of the look (RShaderLook::texture), at uv 0..1. White when there is none. */
vec4 raven_texture(vec2 uv) {
	return texture(rvn_texture, uv);
}

/**
 * What was drawn behind the rectangle, at a position of the window in
 * pixels: sharp, or blurred. Only for a shader created with
 * R_SHADER_BACKDROP (EffectBackdrop="true"); white otherwise.
 */
vec4 raven_backdrop(vec2 screen) {
	return textureLod(rvn_backdrop, screen * rvn_v_inv_size, 0.0);
}
vec4 raven_backdrop_blur(vec2 screen) {
	return textureLod(rvn_backdrop_blur, screen * rvn_v_inv_size, 0.0);
}

/** A number in 0..1 that looks random, the same for the same point. */
float raven_hash(vec2 p) {
	p = fract(p * vec2(123.34, 456.21));
	p += dot(p, p + 45.32);
	return fract(p.x * p.y);
}

/** Smooth noise in 0..1. */
float raven_noise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(raven_hash(i), raven_hash(i + vec2(1.0, 0.0)), u.x),
		mix(raven_hash(i + vec2(0.0, 1.0)), raven_hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

/** Noise of noises (fractal Brownian motion), in 0..1. */
float raven_fbm(vec2 p) {
	float sum = 0.0, amplitude = 0.5;
	for (int i = 0; i < 5; i++) {
		sum += amplitude * raven_noise(p);
		p = p * 2.03 + vec2(17.1, 9.2);
		amplitude *= 0.5;
	}
	return sum;
}

/** Hue (0..1), saturation, value -> RGB. */
vec3 raven_hsv(vec3 c) {
	vec3 rgb = clamp(abs(mod(c.x * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
	return c.z * mix(vec3(1.0), rgb, c.y);
}

/** R_TRUE when the effect knows where the pointer is (it is over the window, and the effect follows it). */
bool raven_has_pointer(RavenEffect fx) {
	return fx.pointer.x > -1e5;
}

/** Signed distance to a rounded box centered on the origin (radii: top-left, top-right, bottom-right, bottom-left). */
float raven_sd_round_box(vec2 p, vec2 half_size, vec4 radii) {
	float radius = p.x < 0.0 ? (p.y < 0.0 ? radii.x : radii.w) : (p.y < 0.0 ? radii.y : radii.z);
	radius = min(radius, min(half_size.x, half_size.y));
	vec2 q = abs(p) - half_size + radius;
	return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

/* ---- what Raven does around the effect ---- */

void main() {
	vec2 p = gl_FragCoord.xy;
	if (p.x < rvn_v_clip.x || p.y < rvn_v_clip.y || p.x >= rvn_v_clip.z || p.y >= rvn_v_clip.w) discard;
	vec2 size = max(rvn_v_rect.zw - rvn_v_rect.xy, vec2(1.0));
	vec2 center = (rvn_v_rect.xy + rvn_v_rect.zw) * 0.5;
	float d = raven_sd_round_box(p - center, size * 0.5, rvn_v_radii);
	float coverage = clamp(0.5 - d, 0.0, 1.0);
	if (coverage <= 0.0) discard;

	RavenEffect fx;
	fx.pixel = p - rvn_v_rect.xy;
	fx.uv = fx.pixel / size;
	fx.size = size;
	fx.screen = p;
	fx.edge = d;
	fx.time = rvn_v_time;
	fx.pointer = rvn_v_pointer.xy - rvn_v_rect.xy;
	fx.hover = rvn_v_pointer.z;
	fx.press = rvn_v_pointer.w;
	/* (the vertex shader premultiplies the colors: the effect gets them straight) */
	fx.color = rvn_v_color.a > 0.0 ? vec4(rvn_v_color.rgb / rvn_v_color.a, rvn_v_color.a) : vec4(0.0);
	fx.color2 = rvn_v_color2.a > 0.0 ? vec4(rvn_v_color2.rgb / rvn_v_color2.a, rvn_v_color2.a) : vec4(0.0);
	fx.params = rvn_v_params;

	vec4 color = raven_effect(fx);
	color = clamp(color, 0.0, 1.0);
	float opacity = float((rvn_v_flags >> 8) & 255u) / 255.0;
	rvn_out = vec4(color.rgb * color.a, color.a) * (coverage * opacity);
}

#endif
