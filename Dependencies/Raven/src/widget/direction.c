/**
 * Raven - widget/direction.c
 * Right-to-left layouts (Rset_layout_direction, Rwidget_set_direction).
 *
 * In a right-to-left part of a window, every container places its children
 * as in a mirror: what would be on the left is on the right (rvn_widget_arrange
 * mirrors the rectangles), and the texts that start on the left start on the
 * right (the painter carries the direction: rvn_paint_text_in and the labels
 * flip their alignment). The widgets that are a surface of their own (a
 * canvas, a node graph, a timeline, a text editor...) keep their inside as
 * it is: their class has RVN_CF_NO_MIRROR.
 *
 * The text itself is ordered by the bidirectional algorithm whatever the
 * direction of the layout (render/bidi.c).
 */

#include "widget.h"

static RDirection g_direction = R_DIRECTION_AUTO;

int rvn_direction_global_rtl(void) {
	if (g_direction == R_DIRECTION_RTL) return R_TRUE;
	if (g_direction == R_DIRECTION_LTR) return R_FALSE;
	return rvn_locale_rtl();
}

int rvn_widget_is_rtl(const rvn_widget_t* widget) {
	const rvn_widget_t* walk;
	for (walk = widget; walk; walk = walk->parent) {
		if (walk->direction == R_DIRECTION_RTL) return R_TRUE;
		if (walk->direction == R_DIRECTION_LTR) return R_FALSE;
	}
	return rvn_direction_global_rtl();
}

static void rvn_direction_dirty(rvn_widget_t* widget) {
	uint32_t i;
	widget->flags |= RVN_WF_LAYOUT_DIRTY;
	for (i = 0; i < widget->child_count; i++) rvn_direction_dirty(widget->children[i]);
}

/* Every window is laid out again, entirely: positions depend on the direction of the parents. */
static void rvn_direction_relayout(void) {
	uint32_t i;
	for (i = 0; i < g_rvn.widgets.used; i++) {
		rvn_widget_t* widget = (rvn_widget_t*)rvn_pool_at(&g_rvn.widgets, i);
		if (widget && !widget->parent && widget->window) {
			rvn_direction_dirty(widget);
			rvn_widget_invalidate_layout(widget);
		}
	}
	rvn_invalidate_all_windows();
}

void rvn_direction_refresh(void) {
	static int last = -1;
	int rtl = rvn_direction_global_rtl();
	if (rtl == last) return;
	last = rtl;
	rvn_direction_relayout();
}

void Rset_layout_direction(RDirection direction) {
	RVN_API_GUARD_VOID();
	if (direction != R_DIRECTION_AUTO && direction != R_DIRECTION_LTR && direction != R_DIRECTION_RTL) {
		rvn_error(R_ERROR_INVALID_ENUM, R_SEVERITY_ERROR, "Rset_layout_direction: R_DIRECTION_AUTO, _LTR or _RTL");
		return;
	}
	g_direction = direction;
	rvn_direction_refresh();
}

RDirection Rget_layout_direction(void) {
	RVN_API_GUARD(R_DIRECTION_LTR);
	return rvn_direction_global_rtl() ? R_DIRECTION_RTL : R_DIRECTION_LTR;
}

void Rwidget_set_direction(Rid id, RDirection direction) {
	rvn_widget_t* widget;
	RVN_API_GUARD_VOID();
	widget = rvn_widget_check(id, 0, __func__);
	if (!widget) return;
	if (direction != R_DIRECTION_INHERIT && direction != R_DIRECTION_LTR && direction != R_DIRECTION_RTL) {
		rvn_error(R_ERROR_INVALID_ENUM, R_SEVERITY_ERROR, "Rwidget_set_direction: R_DIRECTION_INHERIT, _LTR or _RTL");
		return;
	}
	widget->direction = (uint16_t)direction;
	rvn_direction_dirty(widget);
	rvn_widget_invalidate_layout(widget);
}

RDirection Rwidget_get_direction(Rid id) {
	rvn_widget_t* widget;
	RVN_API_GUARD(R_DIRECTION_LTR);
	widget = rvn_widget_check(id, 0, __func__);
	if (!widget) return R_DIRECTION_LTR;
	return rvn_widget_is_rtl(widget) ? R_DIRECTION_RTL : R_DIRECTION_LTR;
}
