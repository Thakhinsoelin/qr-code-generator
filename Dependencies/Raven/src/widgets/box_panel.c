/**
 * Raven - widgets/box_panel.c
 * VBox and HBox: children stacked along one axis (Slate SVerticalBox /
 * SHorizontalBox).
 *
 *   auto slot : takes the desired size of the child along the axis
 *   fill slot : shares the remaining space in proportion to its coefficient
 * Across the axis, every slot spans the whole panel and the child is placed
 * in it according to its alignment.
 */

#include "common.h"

static int rvn_box_is_vertical(const rvn_widget_t* widget) {
	return widget->cls->type == R_WIDGET_VBOX;
}

/* Size of a child along the axis, slot padding excluded, limited by the slot maximum. */
static float rvn_box_child_main(const rvn_widget_t* child, int vertical) {
	float size = vertical ? child->desired.y : child->desired.x;
	if (child->slot.max_size > 0.0f && size > child->slot.max_size) size = child->slot.max_size;
	return size;
}

static float rvn_box_padding_main(const rvn_widget_t* child, int vertical) {
	return vertical ? child->slot.padding.t + child->slot.padding.b : child->slot.padding.l + child->slot.padding.r;
}

static rvn_vec2_t rvn_box_measure(rvn_widget_t* widget) {
	rvn_spaced_panel_t* panel = (rvn_spaced_panel_t*)widget;
	int vertical = rvn_box_is_vertical(widget);
	float main = 0.0f, cross = 0.0f;
	uint32_t i, visible = 0;

	for (i = 0; i < widget->child_count; i++) {
		rvn_widget_t* child = widget->children[i];
		rvn_vec2_t size;
		if (rvn_widget_collapsed(child)) continue;
		size = rvn_widget_measure_slot(child);
		main += rvn_box_child_main(child, vertical) + rvn_box_padding_main(child, vertical);
		cross = rvn_maxf(cross, vertical ? size.x : size.y);
		visible++;
	}
	if (visible > 1) main += panel->spacing * (float)(visible - 1);
	return vertical ? rvn_vec2(cross, main) : rvn_vec2(main, cross);
}

static void rvn_box_arrange(rvn_widget_t* widget) {
	rvn_spaced_panel_t* panel = (rvn_spaced_panel_t*)widget;
	int vertical = rvn_box_is_vertical(widget);
	float main_size = vertical ? widget->rect.h : widget->rect.w;
	float cross_size = vertical ? widget->rect.w : widget->rect.h;
	float fixed = 0.0f, fill_total = 0.0f, remaining, position = 0.0f;
	uint32_t i, visible = 0;

	/* first pass: space taken by the auto slots, the paddings and the gaps */
	for (i = 0; i < widget->child_count; i++) {
		rvn_widget_t* child = widget->children[i];
		if (rvn_widget_collapsed(child)) continue;
		fixed += rvn_box_padding_main(child, vertical);
		if (child->slot.is_fill) fill_total += child->slot.fill;
		else fixed += rvn_box_child_main(child, vertical);
		visible++;
	}
	if (visible > 1) fixed += panel->spacing * (float)(visible - 1);
	remaining = rvn_maxf(0.0f, main_size - fixed);

	/* second pass: place the children */
	for (i = 0; i < widget->child_count; i++) {
		rvn_widget_t* child = widget->children[i];
		float size, padding;
		rvn_rect_t slot;
		if (rvn_widget_collapsed(child)) continue;
		padding = rvn_box_padding_main(child, vertical);
		if (child->slot.is_fill) {
			size = fill_total > 0.0f ? remaining * child->slot.fill / fill_total : 0.0f;
			if (child->slot.max_size > 0.0f && size > child->slot.max_size) size = child->slot.max_size;
		} else {
			size = rvn_box_child_main(child, vertical);
		}
		slot = vertical ? rvn_rect(0.0f, position, cross_size, size + padding)
			: rvn_rect(position, 0.0f, size + padding, cross_size);
		rvn_widget_arrange_slot(child, slot);
		position += size + padding + panel->spacing;
	}
}

static int rvn_box_set_attribute(rvn_widget_t* widget, const char* name, const char* value) {
	rvn_spaced_panel_t* panel = (rvn_spaced_panel_t*)widget;
	if (RVN_ATTR("Spacing")) {
		if (!rvn_parse_float(value, &panel->spacing)) return R_FALSE;
		rvn_widget_invalidate_layout(widget);
		return R_TRUE;
	}
	return R_FALSE;
}

const rvn_widget_class_t rvn_class_vbox = {
	.type = R_WIDGET_VBOX,
	.name = "VBox",
	.size = sizeof(rvn_spaced_panel_t),
	.flags = RVN_CF_NO_HIT,
	.measure = rvn_box_measure,
	.arrange = rvn_box_arrange,
	.set_attribute = rvn_box_set_attribute
};

const rvn_widget_class_t rvn_class_hbox = {
	.type = R_WIDGET_HBOX,
	.name = "HBox",
	.size = sizeof(rvn_spaced_panel_t),
	.flags = RVN_CF_NO_HIT,
	.measure = rvn_box_measure,
	.arrange = rvn_box_arrange,
	.set_attribute = rvn_box_set_attribute
};

/* ---------------------------------------------------------------------------
 *  Public API shared by the panels that have a spacing
 * ------------------------------------------------------------------------- */

float Rpanel_get_spacing(Rid id) {
	RVN_WIDGET(widget, id, 0, 0.0f);
	switch (widget->cls->type) {
		case R_WIDGET_VBOX:
		case R_WIDGET_HBOX:
		case R_WIDGET_WRAP_BOX:
		case R_WIDGET_GRID:
		case R_WIDGET_UNIFORM_GRID:
		case R_WIDGET_SCROLL_BOX:
			return ((rvn_spaced_panel_t*)widget)->spacing;
		default:
			return 0.0f;
	}
}

void Rpanel_set_spacing(Rid id, float spacing) {
	RVN_WIDGET_VOID(widget, id, 0);
	switch (widget->cls->type) {
		case R_WIDGET_VBOX:
		case R_WIDGET_HBOX:
		case R_WIDGET_WRAP_BOX:
		case R_WIDGET_GRID:
		case R_WIDGET_UNIFORM_GRID:
		case R_WIDGET_SCROLL_BOX:
			((rvn_spaced_panel_t*)widget)->spacing = spacing > 0.0f ? spacing : 0.0f;
			rvn_widget_invalidate_layout(widget);
			break;
		default:
			rvn_error(R_ERROR_INVALID_OPERATION, R_SEVERITY_ERROR, "Rpanel_set_spacing: a %s has no spacing", widget->cls->name);
			break;
	}
}
