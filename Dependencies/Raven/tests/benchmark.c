/**
 * Raven - benchmark: cost of a large widget tree.
 *
 *   raven_benchmark [columns] [worker threads] [vulkan]
 *
 * Builds `columns` x 60 rows (5 widgets per row), then measures the first
 * layout, a full repaint (serial or split between the worker threads), and
 * the layout after changing a single label. By default nothing is rendered
 * (null backend): the numbers are the CPU cost of Raven itself. With "vulkan",
 * frames are also sent to the GPU and the time of the render thread is shown.
 */

#include <Raven.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROWS 60

static Rid build_tree(int columns) {
	Rid scroll = Rwidget_create(R_WIDGET_SCROLL_BOX);
	Rid row_of_columns = Rwidget_add_child(scroll, Rwidget_create(R_WIDGET_HBOX));
	int column, row;
	for (column = 0; column < columns; column++) {
		Rid box = Rwidget_add_child(row_of_columns, Rwidget_create(R_WIDGET_VBOX));
		for (row = 0; row < ROWS; row++) {
			char text[32];
			Rid border = Rwidget_add_child(box, Rwidget_create(R_WIDGET_BORDER));
			Rid line = Rwidget_add_child(border, Rwidget_create(R_WIDGET_HBOX));
			snprintf(text, sizeof(text), "Item %d.%d", column, row);
			Rwidget_add_child(line, Rlabel_create(text));
			Rwidget_add_child(line, Rbutton_create("x"));
		}
	}
	return scroll;
}

int main(int argc, char** argv) {
	int columns = argc > 1 ? atoi(argv[1]) : 40;
	int workers = argc > 2 ? atoi(argv[2]) : 0;
	int vulkan = argc > 3 && strcmp(argv[3], "vulkan") == 0;
	const int frames = 30;
	RInitDesc desc;
	RFrameStats stats;
	Rid window, label;
	double start, build_ms, best = 1e9, sum = 0.0, render = 0.0;
	int i;

	if (columns < 4) columns = 4;
	memset(&desc, 0, sizeof(desc));
	desc.backend = vulkan ? R_BACKEND_VULKAN : R_BACKEND_NULL;
	desc.headless = !vulkan;
	desc.worker_threads = workers;
	desc.disable_vsync = R_TRUE;
	if (!Rinit_ex(&desc)) {
		fprintf(stderr, "raven_benchmark: cannot start Raven\n");
		return 1;
	}

	/* large enough for every widget to be visible: nothing is culled */
	window = Rwindow_create("Raven benchmark", vulkan ? 1600 : 150 * columns, vulkan ? 900 : 2000, R_WINDOW_DEFAULT);
	start = Rget_time();
	Rwindow_set_content(window, build_tree(columns));
	build_ms = (Rget_time() - start) * 1000.0;

	Rdraw_frame();
	Rwindow_get_frame_stats(window, &stats);
	printf("%u widgets, %d worker thread(s)\n", stats.widget_count, Rget_worker_thread_count());
	printf("  build         %8.2f ms\n", build_ms);
	printf("  first layout  %8.2f ms\n", stats.layout_ms);
	printf("  first paint   %8.2f ms   (rasterizes the glyphs)\n", stats.paint_ms);

	for (i = 0; i < frames; i++) {
		Rwindow_request_redraw(window);
		Rdraw_frame();
		Rwindow_get_frame_stats(window, &stats);
		if (stats.paint_ms < best) best = stats.paint_ms;
		sum += stats.paint_ms;
		render += stats.render_ms;
	}
	printf("  repaint       %8.2f ms   best of %d (average %.2f ms): %u primitives, %u draw call(s), %u paint job(s)\n",
		best, frames, sum / frames, stats.instance_count, stats.draw_call_count, stats.paint_job_count);
	if (vulkan) printf("  render thread %8.2f ms   average per frame (upload, record, submit, present)\n", render / frames);

	/* one label changes: only its ancestors are measured and arranged again */
	label = Rwidget_get_child(Rwidget_get_child(Rwidget_get_child(Rwidget_get_child(Rwidget_get_child(Rwindow_get_content(window), 0), 3), 7), 0), 0);
	Rlabel_set_text(label, "a longer text than before");
	Rdraw_frame();
	Rwindow_get_frame_stats(window, &stats);
	printf("  relayout      %8.3f ms   after changing one label\n", stats.layout_ms);

	Rshutdown();
	return 0;
}
