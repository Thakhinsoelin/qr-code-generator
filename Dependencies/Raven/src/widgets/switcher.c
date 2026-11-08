/**
 * Raven - widgets/switcher.c
 * Switcher (Slate SWidgetSwitcher): shows one of its children. The others
 * are neither laid out, painted nor reachable by the mouse or the keyboard.
 */

#include "common.h"

typedef struct rvn_switcher_t {
	rvn_widget_t base;
	int index;
} rvn_switcher_t;

static rvn_widget_t* rvn_switcher_active(rvn_widget_t* widget) {
	rvn_switcher_t* switcher = (rvn_switcher_t*)widget;
	if (switcher->index < 0 || switcher->index >= (int)widget->child_count) return NULL;
	return widget->children[switcher->index];
}

static rvn_vec2_t rvn_switcher_measure(rvn_widget_t* widget) {
	rvn_widget_t* active = rvn_switcher_active(widget);
	return active ? rvn_widget_measure_slot(active) : rvn_vec2(0.0f, 0.0f);
}

static void rvn_switcher_arrange(rvn_widget_t* widget) {
	rvn_widget_t* active = rvn_switcher_active(widget);
	if (active && !rvn_widget_collapsed(active)) {
		rvn_widget_arrange_slot(active, rvn_rect(0.0f, 0.0f, widget->rect.w, widget->rect.h));
	}
}

static int rvn_switcher_child_active(rvn_widget_t* widget, rvn_widget_t* child) {
	return rvn_switcher_active(widget) == child;
}

static void rvn_switcher_set(rvn_widget_t* widget, int index) {
	rvn_switcher_t* switcher = (rvn_switcher_t*)widget;
	rvn_widget_t* previous = rvn_switcher_active(widget);
	if (switcher->index == index) return;
	/* the page being hidden may hold the focus, the hover or an open popup */
	if (previous && widget->window) rvn_window_forget_widget(widget->window, previous);
	switcher->index = index;
	rvn_widget_invalidate_layout(widget);
}

static int rvn_switcher_set_attribute(rvn_widget_t* widget, const char* name, const char* value) {
	int index;
	if (RVN_ATTR("Index")) {
		if (!rvn_parse_int(value, &index)) return R_FALSE;
		rvn_switcher_set(widget, index);
		return R_TRUE;
	}
	return R_FALSE;
}

const rvn_widget_class_t rvn_class_switcher = {
	.type = R_WIDGET_SWITCHER,
	.name = "Switcher",
	.size = sizeof(rvn_switcher_t),
	.flags = RVN_CF_NO_HIT,
	.measure = rvn_switcher_measure,
	.arrange = rvn_switcher_arrange,
	.child_active = rvn_switcher_child_active,
	.set_attribute = rvn_switcher_set_attribute
};

void Rswitcher_set_index(Rid id, int index) {
	RVN_WIDGET_VOID(widget, id, R_WIDGET_SWITCHER);
	rvn_switcher_set(widget, index);
}

int Rswitcher_get_index(Rid id) {
	RVN_WIDGET(widget, id, R_WIDGET_SWITCHER, -1);
	return ((rvn_switcher_t*)widget)->index;
}
