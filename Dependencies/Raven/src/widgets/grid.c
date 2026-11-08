/**
 * Raven - widgets/grid.c
 * Grid (Slate SGridPanel): rows and columns sized to their content, with
 * spans and fill coefficients, and UniformGrid (SUniformGridPanel) where all
 * cells share one size.
 */

#include "common.h"

/* ===========================================================================
 *  Grid
 * ========================================================================= */

typedef struct rvn_grid_t {
	rvn_widget_t base;
	float spacing;
	/** Fill coefficient per column / row; 0 = sized to content. */
	float* column_fill;
	uint32_t column_fill_count;
	uint32_t column_fill_cap;
	float* row_fill;
	uint32_t row_fill_count;
	uint32_t row_fill_cap;
	/** Track sizes computed by the last measure, then stretched by arrange. */
	float* column_size;
	float* row_size;
	uint32_t track_cap;
	uint32_t columns;
	uint32_t rows;
} rvn_grid_t;

static void rvn_grid_destroy(rvn_widget_t* widget) {
	rvn_grid_t* grid = (rvn_grid_t*)widget;
	rvn_free(grid->column_fill);
	rvn_free(grid->row_fill);
	rvn_free(grid->column_size);
	rvn_free(grid->row_size);
}

/* Makes the tracks covered by a spanning child large enough for it. */
static void rvn_grid_fit_span(float* sizes, uint32_t first, uint32_t span, float spacing, float needed) {
	float total = spacing * (float)(span - 1);
	uint32_t i;
	for (i = 0; i < span; i++) total += sizes[first + i];
	/* the missing space goes to the last track of the span */
	if (needed > total) sizes[first + span - 1] += needed - total;
}

/* Natural size of every column and row, from the desired sizes of the children. */
static void rvn_grid_compute_tracks(rvn_grid_t* grid) {
	rvn_widget_t* widget = &grid->base;
	uint32_t i, columns = 0, rows = 0, needed;

	for (i = 0; i < widget->child_count; i++) {
		rvn_widget_t* child = widget->children[i];
		if (rvn_widget_collapsed(child)) continue;
		columns = rvn_maxu(columns, (uint32_t)(child->slot.column + child->slot.column_span));
		rows = rvn_maxu(rows, (uint32_t)(child->slot.row + child->slot.row_span));
	}
	grid->columns = columns;
	grid->rows = rows;
	needed = rvn_maxu(columns, rows);
	if (needed > grid->track_cap) {
		grid->column_size = (float*)rvn_realloc(grid->column_size, sizeof(float) * needed);
		grid->row_size = (float*)rvn_realloc(grid->row_size, sizeof(float) * needed);
		grid->track_cap = needed;
	}
	for (i = 0; i < columns; i++) grid->column_size[i] = 0.0f;
	for (i = 0; i < rows; i++) grid->row_size[i] = 0.0f;

	/* cells of one track first, then the spanning cells */
	for (i = 0; i < widget->child_count; i++) {
		rvn_widget_t* child = widget->children[i];
		rvn_vec2_t size;
		if (rvn_widget_collapsed(child)) continue;
		size = rvn_widget_measure_slot(child);
		if (child->slot.column_span == 1) {
			grid->column_size[child->slot.column] = rvn_maxf(grid->column_size[child->slot.column], size.x);
		}
		if (child->slot.row_span == 1) {
			grid->row_size[child->slot.row] = rvn_maxf(grid->row_size[child->slot.row], size.y);
		}
	}
	for (i = 0; i < widget->child_count; i++) {
		rvn_widget_t* child = widget->children[i];
		rvn_vec2_t size;
		if (rvn_widget_collapsed(child)) continue;
		if (child->slot.column_span == 1 && child->slot.row_span == 1) continue;
		size = rvn_widget_measure_slot(child);
		if (child->slot.column_span > 1) {
			rvn_grid_fit_span(grid->column_size, (uint32_t)child->slot.column, (uint32_t)child->slot.column_span, grid->spacing, size.x);
		}
		if (child->slot.row_span > 1) {
			rvn_grid_fit_span(grid->row_size, (uint32_t)child->slot.row, (uint32_t)child->slot.row_span, grid->spacing, size.y);
		}
	}
}

static float rvn_grid_total(const float* sizes, uint32_t count, float spacing) {
	float total = count > 1 ? spacing * (float)(count - 1) : 0.0f;
	uint32_t i;
	for (i = 0; i < count; i++) total += sizes[i];
	return total;
}

static rvn_vec2_t rvn_grid_measure(rvn_widget_t* widget) {
	rvn_grid_t* grid = (rvn_grid_t*)widget;
	rvn_grid_compute_tracks(grid);
	return rvn_vec2(rvn_grid_total(grid->column_size, grid->columns, grid->spacing),
		rvn_grid_total(grid->row_size, grid->rows, grid->spacing));
}

/* Gives the space left over to the tracks that have a fill coefficient. */
static void rvn_grid_stretch(float* sizes, uint32_t count, const float* fill, uint32_t fill_count, float spacing, float available) {
	float extra = available - rvn_grid_total(sizes, count, spacing);
	float total = 0.0f;
	uint32_t i;
	for (i = 0; i < count && i < fill_count; i++) total += fill[i];
	if (extra <= 0.0f || total <= 0.0f) return;
	for (i = 0; i < count && i < fill_count; i++) sizes[i] += extra * fill[i] / total;
}

static float rvn_grid_offset(const float* sizes, uint32_t index, float spacing) {
	float offset = 0.0f;
	uint32_t i;
	for (i = 0; i < index; i++) offset += sizes[i] + spacing;
	return offset;
}

static void rvn_grid_arrange(rvn_widget_t* widget) {
	rvn_grid_t* grid = (rvn_grid_t*)widget;
	uint32_t i;

	/* start again from the natural sizes: the previous arrange stretched them */
	rvn_grid_compute_tracks(grid);
	rvn_grid_stretch(grid->column_size, grid->columns, grid->column_fill, grid->column_fill_count, grid->spacing, widget->rect.w);
	rvn_grid_stretch(grid->row_size, grid->rows, grid->row_fill, grid->row_fill_count, grid->spacing, widget->rect.h);

	for (i = 0; i < widget->child_count; i++) {
		rvn_widget_t* child = widget->children[i];
		rvn_rect_t cell;
		uint32_t column, row, span, k;
		if (rvn_widget_collapsed(child)) continue;
		column = (uint32_t)child->slot.column;
		row = (uint32_t)child->slot.row;
		cell.x = rvn_grid_offset(grid->column_size, column, grid->spacing);
		cell.y = rvn_grid_offset(grid->row_size, row, grid->spacing);
		span = (uint32_t)child->slot.column_span;
		cell.w = grid->spacing * (float)(span - 1);
		for (k = 0; k < span; k++) cell.w += grid->column_size[column + k];
		span = (uint32_t)child->slot.row_span;
		cell.h = grid->spacing * (float)(span - 1);
		for (k = 0; k < span; k++) cell.h += grid->row_size[row + k];
		rvn_widget_arrange_slot(child, cell);
	}
}

static void rvn_grid_set_fill(float** fill, uint32_t* count, uint32_t* cap, int index, float coefficient) {
	uint32_t i;
	if (index < 0 || index > 4095) return;
	if ((uint32_t)index >= *count) {
		RVN_ARRAY_RESERVE(*fill, *cap, (uint32_t)index + 1);
		for (i = *count; i <= (uint32_t)index; i++) (*fill)[i] = 0.0f;
		*count = (uint32_t)index + 1;
	}
	(*fill)[index] = coefficient > 0.0f ? coefficient : 0.0f;
}

/* "auto,1,2": one entry per track, "auto" or a fill coefficient. */
static void rvn_grid_parse_tracks(float** fill, uint32_t* count, uint32_t* cap, const char* value) {
	int index = 0;
	*count = 0;
	while (*value) {
		char token[32];
		size_t length = strcspn(value, ",");
		float coefficient = 0.0f;
		size_t copy = length < sizeof(token) - 1 ? length : sizeof(token) - 1;
		memcpy(token, value, copy);
		token[copy] = '\0';
		if (!rvn_parse_float(token, &coefficient)) coefficient = 0.0f;
		rvn_grid_set_fill(fill, count, cap, index++, coefficient);
		value += length;
		if (*value == ',') value++;
	}
}

static int rvn_grid_set_attribute(rvn_widget_t* widget, const char* name, const char* value) {
	rvn_grid_t* grid = (rvn_grid_t*)widget;
	if (RVN_ATTR("Spacing")) {
		if (!rvn_parse_float(value, &grid->spacing)) return R_FALSE;
	} else if (RVN_ATTR("Columns")) {
		rvn_grid_parse_tracks(&grid->column_fill, &grid->column_fill_count, &grid->column_fill_cap, value);
	} else if (RVN_ATTR("Rows")) {
		rvn_grid_parse_tracks(&grid->row_fill, &grid->row_fill_count, &grid->row_fill_cap, value);
	} else {
		return R_FALSE;
	}
	rvn_widget_invalidate_layout(widget);
	return R_TRUE;
}

const rvn_widget_class_t rvn_class_grid = {
	.type = R_WIDGET_GRID,
	.name = "Grid",
	.size = sizeof(rvn_grid_t),
	.flags = RVN_CF_NO_HIT,
	.destroy = rvn_grid_destroy,
	.measure = rvn_grid_measure,
	.arrange = rvn_grid_arrange,
	.set_attribute = rvn_grid_set_attribute
};

void Rgrid_set_column_fill(Rid id, int column, float coefficient) {
	RVN_WIDGET_VOID(widget, id, R_WIDGET_GRID);
	rvn_grid_t* grid = (rvn_grid_t*)widget;
	rvn_grid_set_fill(&grid->column_fill, &grid->column_fill_count, &grid->column_fill_cap, column, coefficient);
	rvn_widget_invalidate_layout(widget);
}

void Rgrid_set_row_fill(Rid id, int row, float coefficient) {
	RVN_WIDGET_VOID(widget, id, R_WIDGET_GRID);
	rvn_grid_t* grid = (rvn_grid_t*)widget;
	rvn_grid_set_fill(&grid->row_fill, &grid->row_fill_count, &grid->row_fill_cap, row, coefficient);
	rvn_widget_invalidate_layout(widget);
}

/* ===========================================================================
 *  UniformGrid
 * ========================================================================= */

typedef struct rvn_uniform_grid_t {
	rvn_widget_t base;
	float spacing;
	/** > 0: children flow in this many columns; 0: each slot gives its cell. */
	int flow_columns;
	rvn_vec2_t min_cell;
	rvn_vec2_t cell;
	uint32_t columns;
	uint32_t rows;
} rvn_uniform_grid_t;

static void rvn_uniform_grid_cell(const rvn_uniform_grid_t* grid, uint32_t visible_index, const rvn_widget_t* child,
	uint32_t* column, uint32_t* row) {
	if (grid->flow_columns > 0) {
		*column = visible_index % (uint32_t)grid->flow_columns;
		*row = visible_index / (uint32_t)grid->flow_columns;
	} else {
		*column = (uint32_t)child->slot.column;
		*row = (uint32_t)child->slot.row;
	}
}

static rvn_vec2_t rvn_uniform_grid_measure(rvn_widget_t* widget) {
	rvn_uniform_grid_t* grid = (rvn_uniform_grid_t*)widget;
	uint32_t i, visible = 0;

	grid->cell = grid->min_cell;
	grid->columns = 0;
	grid->rows = 0;
	for (i = 0; i < widget->child_count; i++) {
		rvn_widget_t* child = widget->children[i];
		rvn_vec2_t size;
		uint32_t column, row;
		if (rvn_widget_collapsed(child)) continue;
		size = rvn_widget_measure_slot(child);
		grid->cell.x = rvn_maxf(grid->cell.x, size.x);
		grid->cell.y = rvn_maxf(grid->cell.y, size.y);
		rvn_uniform_grid_cell(grid, visible++, child, &column, &row);
		grid->columns = rvn_maxu(grid->columns, column + 1);
		grid->rows = rvn_maxu(grid->rows, row + 1);
	}
	if (grid->columns == 0) return rvn_vec2(0.0f, 0.0f);
	return rvn_vec2(grid->cell.x * (float)grid->columns + grid->spacing * (float)(grid->columns - 1),
		grid->cell.y * (float)grid->rows + grid->spacing * (float)(grid->rows - 1));
}

static void rvn_uniform_grid_arrange(rvn_widget_t* widget) {
	rvn_uniform_grid_t* grid = (rvn_uniform_grid_t*)widget;
	uint32_t i, visible = 0;
	float cell_w, cell_h;

	if (grid->columns == 0 || grid->rows == 0) return;
	/* the cells share the whole panel equally */
	cell_w = (widget->rect.w - grid->spacing * (float)(grid->columns - 1)) / (float)grid->columns;
	cell_h = (widget->rect.h - grid->spacing * (float)(grid->rows - 1)) / (float)grid->rows;
	for (i = 0; i < widget->child_count; i++) {
		rvn_widget_t* child = widget->children[i];
		uint32_t column, row;
		if (rvn_widget_collapsed(child)) continue;
		rvn_uniform_grid_cell(grid, visible++, child, &column, &row);
		rvn_widget_arrange_slot(child, rvn_rect((cell_w + grid->spacing) * (float)column,
			(cell_h + grid->spacing) * (float)row, rvn_maxf(cell_w, 0.0f), rvn_maxf(cell_h, 0.0f)));
	}
}

static int rvn_uniform_grid_set_attribute(rvn_widget_t* widget, const char* name, const char* value) {
	rvn_uniform_grid_t* grid = (rvn_uniform_grid_t*)widget;
	float values[2];
	if (RVN_ATTR("Spacing")) {
		if (!rvn_parse_float(value, &grid->spacing)) return R_FALSE;
	} else if (RVN_ATTR("Columns")) {
		if (!rvn_parse_int(value, &grid->flow_columns) || grid->flow_columns < 0) return R_FALSE;
	} else if (RVN_ATTR("MinCellSize")) {
		int count = rvn_parse_floats(value, values, 2);
		if (count < 1) return R_FALSE;
		grid->min_cell = rvn_vec2(values[0], count > 1 ? values[1] : values[0]);
	} else {
		return R_FALSE;
	}
	rvn_widget_invalidate_layout(widget);
	return R_TRUE;
}

const rvn_widget_class_t rvn_class_uniform_grid = {
	.type = R_WIDGET_UNIFORM_GRID,
	.name = "UniformGrid",
	.size = sizeof(rvn_uniform_grid_t),
	.flags = RVN_CF_NO_HIT,
	.measure = rvn_uniform_grid_measure,
	.arrange = rvn_uniform_grid_arrange,
	.set_attribute = rvn_uniform_grid_set_attribute
};

void Runiform_grid_set_columns(Rid id, int columns) {
	RVN_WIDGET_VOID(widget, id, R_WIDGET_UNIFORM_GRID);
	((rvn_uniform_grid_t*)widget)->flow_columns = columns > 0 ? columns : 0;
	rvn_widget_invalidate_layout(widget);
}

void Runiform_grid_set_min_cell_size(Rid id, float width, float height) {
	RVN_WIDGET_VOID(widget, id, R_WIDGET_UNIFORM_GRID);
	((rvn_uniform_grid_t*)widget)->min_cell = rvn_vec2(rvn_maxf(width, 0.0f), rvn_maxf(height, 0.0f));
	rvn_widget_invalidate_layout(widget);
}
