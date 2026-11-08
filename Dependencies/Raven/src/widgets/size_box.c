/**
 * Raven - widgets/size_box.c
 * SizeBox (Slate SBox): one child with size constraints. The fixed, minimum
 * and maximum sizes are the constraints every widget has (Rwidget_set_size
 * ...); this panel adds a fixed aspect ratio, typically around a viewport.
 */

#include "common.h"

typedef struct rvn_size_box_t {
	rvn_widget_t base;
	/** Width / height. 0 = no constraint. */
	float aspect_ratio;
} rvn_size_box_t;

static void rvn_size_box_arrange(rvn_widget_t* widget) {
	rvn_size_box_t* box = (rvn_size_box_t*)widget;
	rvn_rect_t area = rvn_rect(0.0f, 0.0f, widget->rect.w, widget->rect.h);
	rvn_widget_t* child;

	if (widget->child_count == 0 || rvn_widget_collapsed(widget->children[0])) return;
	child = widget->children[0];
	area = rvn_rect_inset(area, child->slot.padding);

	if (box->aspect_ratio > 0.0f && area.w > 0.0f && area.h > 0.0f) {
		/* largest rectangle of the requested ratio that fits, placed by the slot alignment */
		float width = area.w, height = area.w / box->aspect_ratio;
		float fx = 0.5f, fy = 0.5f;
		if (height > area.h) {
			height = area.h;
			width = area.h * box->aspect_ratio;
		}
		if (child->slot.halign == R_HALIGN_LEFT) fx = 0.0f;
		else if (child->slot.halign == R_HALIGN_RIGHT) fx = 1.0f;
		if (child->slot.valign == R_VALIGN_TOP) fy = 0.0f;
		else if (child->slot.valign == R_VALIGN_BOTTOM) fy = 1.0f;
		rvn_widget_arrange(child, area.x + (area.w - width) * fx, area.y + (area.h - height) * fy, width, height);
		return;
	}
	rvn_widget_arrange_slot(child, rvn_rect(0.0f, 0.0f, widget->rect.w, widget->rect.h));
}

static int rvn_size_box_set_attribute(rvn_widget_t* widget, const char* name, const char* value) {
	rvn_size_box_t* box = (rvn_size_box_t*)widget;
	if (RVN_ATTR("AspectRatio")) {
		float values[2];
		int count = rvn_parse_floats(value, values, 2);
		/* "1.777" or "16,9" */
		if (count == 1) box->aspect_ratio = values[0];
		else if (count == 2 && values[1] > 0.0f) box->aspect_ratio = values[0] / values[1];
		else return R_FALSE;
		rvn_widget_invalidate_layout(widget);
		return R_TRUE;
	}
	return R_FALSE;
}

const rvn_widget_class_t rvn_class_size_box = {
	.type = R_WIDGET_SIZE_BOX,
	.name = "SizeBox",
	.size = sizeof(rvn_size_box_t),
	.flags = RVN_CF_SINGLE_CHILD | RVN_CF_NO_HIT,
	.arrange = rvn_size_box_arrange,
	.set_attribute = rvn_size_box_set_attribute
};

void Rsize_box_set_aspect_ratio(Rid id, float ratio) {
	RVN_WIDGET_VOID(widget, id, R_WIDGET_SIZE_BOX);
	((rvn_size_box_t*)widget)->aspect_ratio = ratio > 0.0f ? ratio : 0.0f;
	rvn_widget_invalidate_layout(widget);
}
