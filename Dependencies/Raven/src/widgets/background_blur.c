/**
 * Raven - widgets/background_blur.c
 * BackgroundBlur: one child over a blurred view of what is behind it, in one
 * of four looks (RBlurStyle): plain, frosted, glass, or the blur of the OS.
 */

#include "common.h"

typedef struct rvn_background_blur_t {
	rvn_widget_t base;
	RBlurStyle style;
	/** Negative: the one of the theme. */
	float radius;
	int has_tint;
	rvn_color_t tint;
	/** Negative: the ones of the style. */
	float saturation;
	float grain;
	float radii[4];
	int has_radii;
	rvn_margin_t padding;
	float shadow;
} rvn_background_blur_t;

static void rvn_background_blur_init(rvn_widget_t* widget) {
	rvn_background_blur_t* blur = (rvn_background_blur_t*)widget;
	blur->style = R_BLUR_FROSTED;
	blur->radius = -1.0f;
	blur->saturation = -1.0f;
	blur->grain = -1.0f;
}

static rvn_vec2_t rvn_background_blur_measure(rvn_widget_t* widget) {
	rvn_background_blur_t* blur = (rvn_background_blur_t*)widget;
	rvn_vec2_t desired = rvn_vec2(0.0f, 0.0f);
	if (widget->child_count > 0) desired = rvn_widget_measure_slot(widget->children[0]);
	desired.x += blur->padding.l + blur->padding.r;
	desired.y += blur->padding.t + blur->padding.b;
	return desired;
}

static void rvn_background_blur_arrange(rvn_widget_t* widget) {
	rvn_background_blur_t* blur = (rvn_background_blur_t*)widget;
	if (widget->child_count > 0 && !rvn_widget_collapsed(widget->children[0])) {
		rvn_widget_arrange_slot(widget->children[0],
			rvn_rect_inset(rvn_rect(0.0f, 0.0f, widget->rect.w, widget->rect.h), blur->padding));
	}
}

/* The veil of the styles that have one: the tint of the widget, else the panel color of the theme, never opaque. */
static rvn_color_t rvn_background_blur_veil(const rvn_background_blur_t* blur, float strength) {
	rvn_color_t color;
	float alpha;
	if (blur->has_tint) return blur->tint;
	color = rvn_widget_color(&blur->base, R_COLOR_PANEL);
	/* (a palette made for solid panels would hide the blur: the veil keeps to its share) */
	alpha = rvn_clampf((float)rvn_color_a(color) / 255.0f, 0.0f, 1.0f);
	return (color & 0x00FFFFFFu) | ((uint32_t)(rvn_minf(alpha, 1.0f) * strength * 255.0f + 0.5f) << 24);
}

static void rvn_background_blur_paint(rvn_widget_t* widget, RPainter* painter) {
	rvn_background_blur_t* blur = (rvn_background_blur_t*)widget;
	const rvn_theme_t* theme = rvn_widget_theme(widget);
	rvn_rect_t rect = rvn_rect(0.0f, 0.0f, widget->rect.w, widget->rect.h);
	float radius = blur->radius >= 0.0f ? blur->radius : rvn_widget_metric(widget, R_METRIC_BLUR_RADIUS);
	rvn_color_t hairline = rvn_rgba(1.0f, 1.0f, 1.0f, theme->dark ? 0.14f : 0.55f);
	RBlurStyle style = blur->style;
	rvn_blur_t look;
	float radii[4];
	int i;

	for (i = 0; i < 4; i++) radii[i] = blur->has_radii ? blur->radii[i] : rvn_widget_metric(widget, R_METRIC_CORNER_RADIUS);
	/* the blur of the OS only exists behind a window that is seen through */
	if (style == R_BLUR_NATIVE && !(painter->window && rvn_window_see_through(painter->window))) style = R_BLUR_FROSTED;

	memset(&look, 0, sizeof(look));
	look.radius = radius;
	look.saturation = blur->saturation >= 0.0f ? blur->saturation : 1.0f;
	look.grain = blur->grain >= 0.0f ? blur->grain : 0.0f;
	look.shadow = blur->shadow;
	look.shadow_color = rvn_widget_color(widget, R_COLOR_SHADOW);

	switch (style) {
		case R_BLUR_GLASS: {
			rvn_glass_t glass;
			rvn_glass_style(widget, RVN_SURFACE_PANEL, 0.0f, 0.0f, &glass);
			/* (in a solid theme the widget still is glass: it is what was asked for) */
			if (!rvn_widget_glass(widget)) {
				glass.tint = rvn_background_blur_veil(blur, 0.4f);
				glass.rim = rvn_rgba(1.0f, 1.0f, 1.0f, theme->dark ? 0.5f : 0.8f);
			}
			if (blur->has_tint) glass.tint = blur->tint;
			glass.frost = rvn_clampf(radius / 22.0f, 0.0f, 1.0f);
			if (blur->saturation >= 0.0f) glass.saturation = blur->saturation;
			glass.shadow = blur->shadow;
			rvn_paint_glass(painter, rect, radii, &glass);
			break;
		}
		case R_BLUR_NATIVE:
			/* a hole in everything the window drew there, then a light veil and a hairline over what the OS shows */
			if (blur->shadow > 0.0f) rvn_paint_shadow(painter, rvn_rect(rect.x, rect.y + blur->shadow * 0.25f, rect.w, rect.h), radii[0], blur->shadow, look.shadow_color);
			rvn_paint_erase(painter, rect, radii, 1.0f);
			rvn_paint_box4(painter, rect, radii, rvn_background_blur_veil(blur, 0.3f), 1.0f, hairline);
			break;
		case R_BLUR_PLAIN:
			look.tint = blur->has_tint ? blur->tint : 0;
			rvn_paint_blur(painter, rect, radii, &look);
			break;
		case R_BLUR_FROSTED:
		default:
			look.tint = rvn_background_blur_veil(blur, 0.52f);
			if (blur->saturation < 0.0f) look.saturation = 1.35f;
			if (blur->grain < 0.0f) look.grain = 0.55f;
			look.light = theme->dark ? 0.01f : 0.03f;
			look.border_width = 1.0f;
			look.border = hairline;
			rvn_paint_blur(painter, rect, radii, &look);
			break;
	}
}

static int rvn_parse_blur_style(const char* value, RBlurStyle* style) {
	static const struct { const char* name; RBlurStyle style; } names[] = {
		{ "Plain", R_BLUR_PLAIN }, { "Gaussian", R_BLUR_PLAIN }, { "Frosted", R_BLUR_FROSTED }, { "Glass", R_BLUR_GLASS },
		{ "Glassy", R_BLUR_GLASS }, { "Native", R_BLUR_NATIVE }, { "System", R_BLUR_NATIVE }
	};
	size_t i;
	for (i = 0; i < RVN_COUNT_OF(names); i++) {
		if (rvn_stricmp(value, names[i].name) == 0) {
			*style = names[i].style;
			return R_TRUE;
		}
	}
	return R_FALSE;
}

static int rvn_background_blur_set_attribute(rvn_widget_t* widget, const char* name, const char* value) {
	rvn_background_blur_t* blur = (rvn_background_blur_t*)widget;
	float values[4];
	int count;

	if (RVN_ATTR("Style") || RVN_ATTR("Kind")) {
		if (!rvn_parse_blur_style(value, &blur->style)) return R_FALSE;
	} else if (RVN_ATTR("Radius") || RVN_ATTR("Blur")) {
		if (!rvn_parse_float(value, &blur->radius)) return R_FALSE;
	} else if (RVN_ATTR("Tint")) {
		if (!rvn_parse_color(value, &blur->tint)) return R_FALSE;
		blur->has_tint = R_TRUE;
	} else if (RVN_ATTR("Saturation")) {
		if (!rvn_parse_float(value, &blur->saturation)) return R_FALSE;
	} else if (RVN_ATTR("Grain")) {
		if (!rvn_parse_float(value, &blur->grain)) return R_FALSE;
	} else if (RVN_ATTR("Shadow")) {
		if (!rvn_parse_float(value, &blur->shadow)) return R_FALSE;
	} else if (RVN_ATTR("Padding")) {
		if (!rvn_parse_margin(value, &blur->padding)) return R_FALSE;
		rvn_widget_invalidate_layout(widget);
	} else if (RVN_ATTR("CornerRadius")) {
		count = rvn_parse_floats(value, values, 4);
		if (count != 1 && count != 4) return R_FALSE;
		blur->radii[0] = values[0];
		blur->radii[1] = count == 4 ? values[1] : values[0];
		blur->radii[2] = count == 4 ? values[2] : values[0];
		blur->radii[3] = count == 4 ? values[3] : values[0];
		blur->has_radii = R_TRUE;
	} else {
		return R_FALSE;
	}
	rvn_widget_invalidate_paint(widget);
	return R_TRUE;
}

const rvn_widget_class_t rvn_class_background_blur = {
	.type = R_WIDGET_BACKGROUND_BLUR,
	.name = "BackgroundBlur",
	.size = sizeof(rvn_background_blur_t),
	.flags = RVN_CF_SINGLE_CHILD | RVN_CF_SURFACE,
	.init = rvn_background_blur_init,
	.measure = rvn_background_blur_measure,
	.arrange = rvn_background_blur_arrange,
	.paint = rvn_background_blur_paint,
	.set_attribute = rvn_background_blur_set_attribute
};

/* ---------------------------------------------------------------------------
 *  Public API
 * ------------------------------------------------------------------------- */

#define RVN_BLUR(variable, id, result) \
	rvn_background_blur_t* variable = (rvn_background_blur_t*)rvn_widget_check((id), R_WIDGET_BACKGROUND_BLUR, __func__); \
	if (!variable) return result

#define RVN_BLUR_VOID(variable, id) \
	rvn_background_blur_t* variable = (rvn_background_blur_t*)rvn_widget_check((id), R_WIDGET_BACKGROUND_BLUR, __func__); \
	if (!variable) return

Rid Rbackground_blur_create(void) {
	rvn_widget_t* widget;
	RVN_API_GUARD(R_INVALID_ID);
	widget = rvn_widget_create(R_WIDGET_BACKGROUND_BLUR);
	return widget ? widget->id : R_INVALID_ID;
}

void Rbackground_blur_set_style(Rid id, RBlurStyle style) {
	RVN_BLUR_VOID(blur, id);
	if (style < R_BLUR_PLAIN || style > R_BLUR_NATIVE) {
		rvn_error(R_ERROR_INVALID_ENUM, R_SEVERITY_ERROR, "Rbackground_blur_set_style: invalid style");
		return;
	}
	blur->style = style;
	rvn_widget_invalidate_paint(&blur->base);
}

RBlurStyle Rbackground_blur_get_style(Rid id) {
	RVN_BLUR(blur, id, R_BLUR_FROSTED);
	return blur->style;
}

void Rbackground_blur_set_radius(Rid id, float radius) {
	RVN_BLUR_VOID(blur, id);
	blur->radius = radius;
	rvn_widget_invalidate_paint(&blur->base);
}

float Rbackground_blur_get_radius(Rid id) {
	RVN_BLUR(blur, id, 0.0f);
	return blur->radius >= 0.0f ? blur->radius : rvn_widget_metric(&blur->base, R_METRIC_BLUR_RADIUS);
}

void Rbackground_blur_set_tint(Rid id, float r, float g, float b, float a) {
	RVN_BLUR_VOID(blur, id);
	blur->tint = rvn_rgba(r, g, b, a);
	blur->has_tint = R_TRUE;
	rvn_widget_invalidate_paint(&blur->base);
}

void Rbackground_blur_clear_tint(Rid id) {
	RVN_BLUR_VOID(blur, id);
	blur->has_tint = R_FALSE;
	rvn_widget_invalidate_paint(&blur->base);
}

void Rbackground_blur_set_saturation(Rid id, float saturation) {
	RVN_BLUR_VOID(blur, id);
	blur->saturation = saturation;
	rvn_widget_invalidate_paint(&blur->base);
}

void Rbackground_blur_set_grain(Rid id, float grain) {
	RVN_BLUR_VOID(blur, id);
	blur->grain = grain;
	rvn_widget_invalidate_paint(&blur->base);
}

void Rbackground_blur_set_corner_radius(Rid id, float top_left, float top_right, float bottom_right, float bottom_left) {
	RVN_BLUR_VOID(blur, id);
	blur->radii[0] = top_left;
	blur->radii[1] = top_right;
	blur->radii[2] = bottom_right;
	blur->radii[3] = bottom_left;
	blur->has_radii = R_TRUE;
	rvn_widget_invalidate_paint(&blur->base);
}

void Rbackground_blur_set_padding(Rid id, float left, float top, float right, float bottom) {
	RVN_BLUR_VOID(blur, id);
	blur->padding = rvn_margin(left, top, right, bottom);
	rvn_widget_invalidate_layout(&blur->base);
}

void Rbackground_blur_set_shadow(Rid id, float shadow_blur) {
	RVN_BLUR_VOID(blur, id);
	blur->shadow = shadow_blur > 0.0f ? shadow_blur : 0.0f;
	rvn_widget_invalidate_paint(&blur->base);
}
