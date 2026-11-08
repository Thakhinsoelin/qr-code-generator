/**
 * Raven - widgets/canvas.c
 * Canvas (Slate SConstraintCanvas): children placed freely with anchors,
 * offsets and a pivot. Children may lie outside of the canvas.
 *
 * Per axis, with the anchors given as fractions of the canvas size:
 *   - anchors equal    : the child sits at anchor + position; its size is the
 *                        slot size, or its desired size when that is <= 0;
 *                        the pivot (alignment) shifts it by -pivot * size.
 *   - anchors different: the child stretches between the anchors; position
 *                        and size are the near and far margins.
 */

#include "common.h"

static rvn_vec2_t rvn_canvas_measure(rvn_widget_t* widget) {
	uint32_t i;
	/* a canvas asks for no space, but its children still need a desired size */
	for (i = 0; i < widget->child_count; i++) rvn_widget_measure(widget->children[i]);
	return rvn_vec2(0.0f, 0.0f);
}

static void rvn_canvas_axis(float canvas, float anchor_min, float anchor_max, float position, float size,
	float desired, float pivot, float* out_position, float* out_size) {
	if (anchor_max > anchor_min) {
		float near = canvas * anchor_min + position;
		float far = canvas * anchor_max - size;
		*out_position = near;
		*out_size = rvn_maxf(0.0f, far - near);
	} else {
		float length = size > 0.0f ? size : desired;
		*out_position = canvas * anchor_min + position - pivot * length;
		*out_size = length;
	}
}

static void rvn_canvas_arrange(rvn_widget_t* widget) {
	uint32_t i;
	for (i = 0; i < widget->child_count; i++) {
		rvn_widget_t* child = widget->children[i];
		const rvn_slot_t* slot = &child->slot;
		float x, y, width, height;
		if (rvn_widget_collapsed(child)) continue;
		rvn_canvas_axis(widget->rect.w, slot->anchors[0], slot->anchors[2], slot->position[0], slot->size[0],
			child->desired.x, slot->alignment[0], &x, &width);
		rvn_canvas_axis(widget->rect.h, slot->anchors[1], slot->anchors[3], slot->position[1], slot->size[1],
			child->desired.y, slot->alignment[1], &y, &height);
		rvn_widget_arrange(child, x, y, width, height);
	}
}

const rvn_widget_class_t rvn_class_canvas = {
	.type = R_WIDGET_CANVAS,
	.name = "Canvas",
	.size = sizeof(rvn_widget_t),
	.flags = RVN_CF_NO_HIT | RVN_CF_OVERFLOW | RVN_CF_NO_MIRROR,
	.measure = rvn_canvas_measure,
	.arrange = rvn_canvas_arrange
};
