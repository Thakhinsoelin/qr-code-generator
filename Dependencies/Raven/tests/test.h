/**
 * Raven tests - minimal test harness: no dependency, one executable per file.
 */

#ifndef RVN_TEST_H
#define RVN_TEST_H

#include <Raven.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_test_checks = 0;
static int g_test_failures = 0;
static int g_test_errors = 0;
static int g_test_expect_errors = 0;

#define CHECK(condition) do { \
	g_test_checks++; \
	if (!(condition)) { \
		g_test_failures++; \
		printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
	} \
} while (0)

#define CHECK_NEAR(actual, expected) do { \
	double test_a_ = (double)(actual), test_e_ = (double)(expected); \
	g_test_checks++; \
	if (fabs(test_a_ - test_e_) > 0.51) { \
		g_test_failures++; \
		printf("  FAIL %s:%d: %s = %g, expected %g\n", __FILE__, __LINE__, #actual, test_a_, test_e_); \
	} \
} while (0)

#define CHECK_STR(actual, expected) do { \
	const char* test_a_ = (actual); \
	const char* test_e_ = (expected); \
	g_test_checks++; \
	if (!test_a_ || strcmp(test_a_, test_e_) != 0) { \
		g_test_failures++; \
		printf("  FAIL %s:%d: %s = \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #actual, test_a_ ? test_a_ : "(null)", test_e_); \
	} \
} while (0)

/* Bounds of a widget in its window. */
#define CHECK_RECT(widget, ex, ey, ew, eh) do { \
	float test_x_, test_y_, test_w_, test_h_; \
	Rwidget_get_rect((widget), &test_x_, &test_y_, &test_w_, &test_h_); \
	g_test_checks++; \
	if (fabs(test_x_ - (ex)) > 0.51 || fabs(test_y_ - (ey)) > 0.51 || fabs(test_w_ - (ew)) > 0.51 || fabs(test_h_ - (eh)) > 0.51) { \
		g_test_failures++; \
		printf("  FAIL %s:%d: rect of %s = (%g, %g, %g, %g), expected (%g, %g, %g, %g)\n", __FILE__, __LINE__, #widget, \
			test_x_, test_y_, test_w_, test_h_, (double)(ex), (double)(ey), (double)(ew), (double)(eh)); \
	} \
} while (0)

/* Errors raised by the library fail the test, unless the test announced them. */
static inline void test_error_callback(RError code, RSeverity severity, const char* detail) {
	g_test_errors++;
	if (!g_test_expect_errors) {
		g_test_failures++;
		printf("  UNEXPECTED ERROR %s (%s): %s\n", Rerror_to_string(code), Rseverity_to_string(severity), detail);
	}
}

#define EXPECT_ERRORS_BEGIN() do { g_test_expect_errors = 1; g_test_errors = 0; } while (0)
#define EXPECT_ERRORS_END(minimum) do { g_test_expect_errors = 0; CHECK(g_test_errors >= (minimum)); } while (0)

/* Starts the library without a window system and without a GPU. */
static inline int test_init(int worker_threads) {
	RInitDesc desc;
	memset(&desc, 0, sizeof(desc));
	desc.backend = R_BACKEND_NULL;
	desc.headless = R_TRUE;
	desc.worker_threads = worker_threads;
	Rerror_set_callback(test_error_callback);
	if (!Rinit_ex(&desc)) return 0;
	/* the tests read the state right after an input: no animation in between */
	Rset_animations_enabled(R_FALSE);
	return 1;
}

#define RUN(function) do { printf("%s\n", #function); function(); } while (0)

/* Called after Rshutdown: besides the checks of the test, the library must have freed everything. */
static inline int test_report(const char* name) {
	RMemoryStats memory;
	Rmemory_get_stats(&memory);
	g_test_checks++;
	if (memory.allocation_count != 0) {
		g_test_failures++;
		printf("  FAIL: %u allocation(s) of the library are still alive after Rshutdown\n", (unsigned)memory.allocation_count);
	}
	g_test_checks++;
	if (!Rmemory_check()) {
		g_test_failures++;
		printf("  FAIL: the heap of the library is corrupted\n");
	}
	printf("%s: %d checks, %d failures\n", name, g_test_checks, g_test_failures);
	return g_test_failures == 0 ? 0 : 1;
}

/* Moves the cursor to the middle of a widget and clicks. */
static inline void test_click(Rid window, Rid widget) {
	float x, y, w, h;
	Rwidget_get_rect(widget, &x, &y, &w, &h);
	Rwindow_inject_mouse_move(window, x + w * 0.5f, y + h * 0.5f);
	Rwindow_inject_mouse_button(window, R_MOUSE_LEFT, R_TRUE);
	Rwindow_inject_mouse_button(window, R_MOUSE_LEFT, R_FALSE);
}

static inline void test_click_at(Rid window, float x, float y) {
	Rwindow_inject_mouse_move(window, x, y);
	Rwindow_inject_mouse_button(window, R_MOUSE_LEFT, R_TRUE);
	Rwindow_inject_mouse_button(window, R_MOUSE_LEFT, R_FALSE);
}

static inline void test_key(Rid window, RKey key, Ruint modifiers) {
	Rwindow_inject_key(window, key, R_TRUE, modifiers);
	Rwindow_inject_key(window, key, R_FALSE, modifiers);
}

#endif /* RVN_TEST_H */
