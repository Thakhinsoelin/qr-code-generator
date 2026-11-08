/**
 * Raven - widgets/wrap_box.c
 * WrapBox (Slate SWrapBox): children flow left to right and continue on a
 * new line when the width is used up.
 *
 * The height depends on the width the panel receives, which is only known
 * when it is arranged: the panel remembers that width and, when it changes,
 * asks for one more layout pass.
 */

#include "common.h"

typedef struct rvn_wrap_box_t {
	rvn_widget_t base;
	float spacing;
	/** Width received at the last arrange; 0 before the first one. */
	float wrap_width;
} rvn_wrap_box_t;

/**
 * Walks the children as they flow in `width`. With `place`, gives them their
 * bounds. Returns the size used.
 */
static rvn_vec2_t rvn_wrap_box_flow(rvn_widget_t* widget, float width, int place) {
	rvn_wrap_box_t* box = (rvn_wrap_box_t*)widget;
	float x = 0.0f, y = 0.0f, line_height = 0.0f, used_width = 0.0f;
	uint32_t i;

	for (i = 0; i < widget->child_count; i++) {
		rvn_widget_t* child = widget->children[i];
		rvn_vec2_t size;
		if (rvn_widget_collapsed(child)) continue;
		size = rvn_widget_measure_slot(child);
		if (x > 0.0f && width > 0.0f && x + size.x > width + 0.01f) {
			x = 0.0f;
			y += line_height + box->spacing;
			line_height = 0.0f;
		}
		if (place) rvn_widget_arrange_slot(child, rvn_rect(x, y, size.x, size.y));
		x += size.x;
		used_width = rvn_maxf(used_width, x);
		x += box->spacing;
		line_height = rvn_maxf(line_height, size.y);
	}
	return rvn_vec2(used_width, y + line_height);
}

static rvn_vec2_t rvn_wrap_box_measure(rvn_widget_t* widget) {
	rvn_wrap_box_t* box = (rvn_wrap_box_t*)widget;
	return rvn_wrap_box_flow(widget, box->wrap_width, R_FALSE);
}

static void rvn_wrap_box_arrange(rvn_widget_t* widget) {
	rvn_wrap_box_t* box = (rvn_wrap_box_t*)widget;
	if (box->wrap_width != widget->rect.w) {
		box->wrap_width = widget->rect.w;
		/* the height we announced was computed for another width */
		rvn_widget_invalidate_layout(widget);
	}
	rvn_wrap_box_flow(widget, widget->rect.w, R_TRUE);
}

static int rvn_wrap_box_set_attribute(rvn_widget_t* widget, const char* name, const char* value) {
	rvn_wrap_box_t* box = (rvn_wrap_box_t*)widget;
	if (RVN_ATTR("Spacing")) {
		if (!rvn_parse_float(value, &box->spacing)) return R_FALSE;
		rvn_widget_invalidate_layout(widget);
		return R_TRUE;
	}
	return R_FALSE;
}

const rvn_widget_class_t rvn_class_wrap_box = {
	.type = R_WIDGET_WRAP_BOX,
	.name = "WrapBox",
	.size = sizeof(rvn_wrap_box_t),
	.flags = RVN_CF_NO_HIT,
	.measure = rvn_wrap_box_measure,
	.arrange = rvn_wrap_box_arrange,
	.set_attribute = rvn_wrap_box_set_attribute
};
