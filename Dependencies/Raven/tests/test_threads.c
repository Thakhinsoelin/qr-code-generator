/**
 * Raven tests - threading: job system, tasks posted to the UI thread,
 * asynchronous textures, and parallel painting giving the same result as
 * serial painting.
 */

#include "test.h"

#include <stdlib.h>

#define ITEM_COUNT 100000

static int g_values[ITEM_COUNT];
static volatile int g_posted;
static Rid g_label;

static void fill_range(Ruint begin, Ruint end, void* user_data) {
	Ruint i;
	(void)user_data;
	for (i = begin; i < end; i++) g_values[i] += (int)i + 1;
}

/* Runs on the UI thread: it may touch widgets. */
static void posted_task(void* user_data) {
	Rlabel_set_text(g_label, (const char*)user_data);
	g_posted++;
}

/* Runs on a worker thread: it must not touch widgets, only post. */
static void async_job(void* user_data) {
	/* the test counters are not thread-safe: report through the posted task instead of CHECK */
	Rtask_post(posted_task, Ris_ui_thread() ? (void*)"job ran on the UI thread" : user_data);
}

static void wait_for_posted(int count) {
	double deadline = Rget_time() + 5.0;
	while (g_posted < count && Rget_time() < deadline) {
		Rwait_events(0.05);
		Rdraw_frame();
	}
}

static void test_jobs(void) {
	long long sum = 0;
	int i;
	CHECK(Rget_worker_thread_count() == 4);
	CHECK(Ris_ui_thread());

	memset(g_values, 0, sizeof(g_values));
	Rparallel_for(ITEM_COUNT, 512, fill_range, NULL);
	Rparallel_for(ITEM_COUNT, 0, fill_range, NULL);
	for (i = 0; i < ITEM_COUNT; i++) sum += g_values[i];
	/* every item was visited exactly once per call */
	CHECK(sum == 2LL * ((long long)ITEM_COUNT * (ITEM_COUNT + 1) / 2));

	g_posted = 0;
	for (i = 0; i < 50; i++) Rtask_run_async(async_job, "from a worker");
	wait_for_posted(50);
	CHECK(g_posted == 50);
	CHECK_STR(Rlabel_get_text(g_label), "from a worker");

	/* widget functions refuse to run on another thread: tested by the worker above
	 * through Ris_ui_thread; here the wake-up from a foreign thread */
	g_posted = 0;
	Rtask_run_async(async_job, "again");
	Rwait_events(5.0);
	wait_for_posted(1);
	CHECK(g_posted == 1);
}

static void test_async_texture(void) {
	/* a 2 x 2 BMP, 24 bits, built by hand */
	static const unsigned char bmp[] = {
		'B', 'M', 70, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 1, 0, 24, 0,
		0, 0, 0, 0, 16, 0, 0, 0, 19, 11, 0, 0, 19, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 255, 0, 255, 0, 0, 0, 255, 0, 0, 255, 255, 255, 0, 0
	};
	Rid texture = Rtexture_create_async((const char*)bmp, sizeof(bmp));
	Rid pixels;
	static const uint8_t rgba[16] = { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255 };
	int w = 0, h = 0;

	CHECK(Rtexture_is_valid(texture));
#if defined(RAVEN_TEST_NO_IMAGE_DECODER)
	/* built with RAVEN_WITH_STB_IMAGE=OFF: decoding fails, which must be reported and not hang */
	EXPECT_ERRORS_BEGIN();
	Rtexture_wait(texture);
	EXPECT_ERRORS_END(1);
	CHECK(Rtexture_is_ready(texture));
#else
	Rtexture_wait(texture);
	CHECK(Rtexture_is_ready(texture));
	Rtexture_get_size(texture, &w, &h);
	CHECK(w == 2 && h == 2);
#endif
	Rtexture_destroy(texture);
	CHECK(!Rtexture_is_valid(texture));

	pixels = Rtexture_create_from_pixels(rgba, 2, 2);
	Rtexture_set_filter(pixels, R_FILTER_NEAREST);
	Rtexture_update(pixels, rgba, 2, 2);
	Rtexture_update(pixels, rgba, 4, 1);
	Rtexture_get_size(pixels, &w, &h);
	CHECK(w == 4 && h == 1);

	/* garbage is reported, not crashed on */
	EXPECT_ERRORS_BEGIN();
	CHECK(Rtexture_create("not an image", 12) == R_INVALID_ID);
	texture = Rtexture_create_async("not an image", 12);
	Rtexture_wait(texture);
	EXPECT_ERRORS_END(2);
	CHECK(Rtexture_is_ready(texture));
	Rtexture_get_size(texture, &w, &h);
	CHECK(w == 0 && h == 0);
}

/* A deep, wide tree: 40 groups of 60 rows, each row a border holding a label and a button. */
static Rid build_big_tree(void) {
	Rid scroll = Rwidget_create(R_WIDGET_SCROLL_BOX);
	Rid columns = Rwidget_add_child(scroll, Rwidget_create(R_WIDGET_HBOX));
	int group, row;
	for (group = 0; group < 40; group++) {
		Rid box = Rwidget_add_child(columns, Rwidget_create(R_WIDGET_VBOX));
		for (row = 0; row < 60; row++) {
			char text[32];
			Rid border = Rwidget_add_child(box, Rwidget_create(R_WIDGET_BORDER));
			Rid line = Rwidget_add_child(border, Rwidget_create(R_WIDGET_HBOX));
			snprintf(text, sizeof(text), "Item %d.%d", group, row);
			Rwidget_add_child(line, Rlabel_create(text));
			Rwidget_add_child(line, Rbutton_create("x"));
			Rborder_set_outline(border, 1, 1, 1, 1, 0.2f);
		}
	}
	return scroll;
}

static RFrameStats paint_big_tree(int workers) {
	RFrameStats stats;
	Rid window;
	int frame;
	memset(&stats, 0, sizeof(stats));
	if (!test_init(workers)) return stats;
	/* large enough for every widget to be visible, so that nothing is culled */
	window = Rwindow_create("big", 6000, 2000, R_WINDOW_DEFAULT);
	Rwindow_set_content(window, build_big_tree());
	/* the first frame also rasterizes the glyphs: measure a later one */
	for (frame = 0; frame < 5; frame++) {
		Rwindow_request_redraw(window);
		Rdraw_frame();
	}
	Rwindow_get_frame_stats(window, &stats);
	Rshutdown();
	return stats;
}

static void test_parallel_paint(void) {
	RFrameStats serial = paint_big_tree(-1);
	RFrameStats parallel = paint_big_tree(4);

	CHECK(serial.widget_count > 9000);
	CHECK(serial.paint_job_count == 0);
	CHECK(parallel.paint_job_count > 0);
	/* the same widgets produce the same primitives and the same batches */
	CHECK(parallel.widget_count == serial.widget_count);
	CHECK(parallel.instance_count == serial.instance_count);
	CHECK(parallel.draw_call_count == serial.draw_call_count);
	CHECK(serial.instance_count > 20000);
	/* text and shapes share one batch: thousands of widgets, a handful of draw calls */
	CHECK(serial.draw_call_count <= 2);
	printf("  %u widgets, %u instances, %u draw call(s); paint %.2f ms serial, %.2f ms with %u jobs\n",
		serial.widget_count, serial.instance_count, serial.draw_call_count, serial.paint_ms, parallel.paint_ms,
		parallel.paint_job_count);
}

int main(void) {
	Rid window;
	if (!test_init(4)) {
		printf("cannot initialize Raven\n");
		return 1;
	}
	window = Rwindow_create("threads", 400, 300, R_WINDOW_DEFAULT);
	g_label = Rlabel_create("start");
	Rwindow_set_content(window, g_label);

	RUN(test_jobs);
	RUN(test_async_texture);
	Rshutdown();

	RUN(test_parallel_paint);
	return test_report("test_threads");
}
