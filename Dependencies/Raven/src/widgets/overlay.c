/**
 * Raven - widgets/overlay.c
 * Overlay (Slate SOverlay): children drawn on top of each other, each one
 * aligned in the whole panel by its slot. Spacer: empty space.
 *
 * Both rely entirely on the default measure / arrange of the widget core:
 * the desired size is the largest child, and every child is placed in the
 * full bounds according to its slot padding and alignment.
 */

#include "common.h"

const rvn_widget_class_t rvn_class_overlay = {
	.type = R_WIDGET_OVERLAY,
	.name = "Overlay",
	.size = sizeof(rvn_widget_t),
	.flags = RVN_CF_NO_HIT
};

/* A spacer has no content: its size comes from Rwidget_set_size / the fill slot. */
const rvn_widget_class_t rvn_class_spacer = {
	.type = R_WIDGET_SPACER,
	.name = "Spacer",
	.size = sizeof(rvn_widget_t),
	.flags = RVN_CF_NO_HIT | RVN_CF_NO_CHILDREN
};

void Rspacer_set_size(Rid id, float width, float height) {
	RVN_WIDGET_VOID(widget, id, R_WIDGET_SPACER);
	widget->fixed_size = rvn_vec2(width, height);
	rvn_widget_invalidate_layout(widget);
}
