/**
 * Raven examples - helpers shared by the examples (not part of the library).
 *
 *   example_init          starts Raven and prints its errors to stderr
 *   example_automated     scripted runs of the examples (see below)
 *
 * For automated runs, two environment variables make an example render a
 * fixed number of frames, optionally save the last one, and exit:
 *
 *   RAVEN_EXAMPLE_FRAMES=30 RAVEN_EXAMPLE_CAPTURE=shot.ppm ./raven_editor
 *
 * RAVEN_EXAMPLE_FILM=prefix also saves one frame out of six as prefix000.ppm,
 * prefix001.ppm..., with the animations advancing by 1/60 s per frame.
 */

#ifndef RVN_EXAMPLE_H
#define RVN_EXAMPLE_H

#include <Raven.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void example_on_error(RError code, RSeverity severity, const char* detail) {
	fprintf(stderr, "[Raven] %s (%s): %s\n", Rerror_to_string(code), Rseverity_to_string(severity), detail);
}

/** Starts Raven with the Vulkan backend. Returns R_FALSE on failure. */
static int example_init(const char* name) {
	RInitDesc desc;
	memset(&desc, 0, sizeof(desc));
	desc.backend = R_BACKEND_VULKAN;
	desc.app_name = name;
#ifndef NDEBUG
	desc.enable_validation = R_TRUE;
#endif
	Rerror_set_callback(example_on_error);
	if (!Rinit_ex(&desc)) {
		fprintf(stderr, "%s: cannot start Raven (is a Vulkan driver installed?)\n", name);
		return R_FALSE;
	}
	return R_TRUE;
}

/** Saves a window as a binary PPM image; as a PAM image, which keeps the alpha of a window that is seen through, when the name ends with ".pam". */
static void example_capture(Rid window, const char* path) {
	int width = 0, height = 0, i;
	uint8_t* pixels = Rwindow_capture(window, &width, &height);
	FILE* file = pixels ? fopen(path, "wb") : NULL;
	size_t length = strlen(path);
	if (file && length > 4 && strcmp(path + length - 4, ".pam") == 0) {
		fprintf(file, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", width, height);
		fwrite(pixels, 4, (size_t)width * (size_t)height, file);
		fclose(file);
	} else if (file) {
		fprintf(file, "P6\n%d %d\n255\n", width, height);
		for (i = 0; i < width * height; i++) fwrite(pixels + i * 4, 1, 3, file);
		fclose(file);
	}
	Rpixels_free(pixels);
}

/**
 * Automated runs (RAVEN_EXAMPLE_FRAMES): renders a fixed number of frames,
 * optionally saves the last one, and returns R_TRUE: the example then skips
 * its loop. Returns R_FALSE when the example should run normally.
 */
static int example_automated(Rid window) {
	const char* frames = getenv("RAVEN_EXAMPLE_FRAMES");
	const char* capture = getenv("RAVEN_EXAMPLE_CAPTURE");
	const char* film = getenv("RAVEN_EXAMPLE_FILM");
	int count, i;
	if (!frames) return R_FALSE;
	count = atoi(frames);
	/* a film: the animations advance by 1/60 s per frame whatever the time it takes to render one */
	if (film) Rset_animation_time_step(1.0 / 60.0);
	for (i = 0; i < count && Ris_running(); i++) {
		Rwindow_request_redraw(window);
		Rwait_frame(0.01);
		if (film && i % 6 == 0 && Rwindow_is_valid(window)) {
			char path[512];
			snprintf(path, sizeof(path), "%s%03d.ppm", film, i / 6);
			example_capture(window, path);
		}
	}
	if (capture && Rwindow_is_valid(window)) example_capture(window, capture);
	if (Rwindow_is_valid(window)) {
		RFrameStats stats;
		Rwindow_get_frame_stats(window, &stats);
		printf("last frame: %u widgets, %u primitives, %u draw calls, %u backdrop captures, paint %.2f ms, render %.2f ms\n",
			stats.widget_count, stats.instance_count, stats.draw_call_count, stats.backdrop_count,
			stats.paint_ms, stats.render_ms);
	}
	return R_TRUE;
}

#endif /* RVN_EXAMPLE_H */
