/**
 * Raven Web
 * raven_web.h : web pages inside a Raven interface.
 *
 * A separate, optional library (target `raven_web`, CMake option
 * RAVEN_BUILD_WEB). Raven itself knows nothing about it; it only uses the
 * public functions of Raven, and an application that does not link it pays
 * nothing.
 *
 * It does not contain a browser. It drives one that is already installed:
 * any browser of the Chromium family (Microsoft Edge, which comes with
 * Windows 10 and 11, Google Chrome, Chromium, Brave...). The browser runs
 * hidden, in its own process, with a profile of its own; Raven Web talks to
 * it through a pipe (the DevTools protocol) and:
 *
 *   - receives the picture of the page each time it changes, and shows it
 *     in a texture, as any image of the interface: it can be clipped,
 *     scrolled, put under glass, animated;
 *   - sends it the mouse, the keys and the text the widget receives;
 *   - runs JavaScript in the page and receives messages from it.
 *
 * So HTML, CSS and JavaScript are those of a real, up to date browser, and
 * the library weighs a few thousand lines. The price: a browser must be
 * installed (Rweb_is_available tells), pages are pictures that arrive a
 * frame late, and the cursor does not change over links.
 *
 *     Rweb_register_xml();                       // once, after Rinit: <WebView> exists
 *     Rid view = Rweb_view_create();
 *     Rweb_view_load_url(view, "https://example.org");
 *
 *     <WebView Name="docs" Url="https://example.org" Slot.Fill="1"/>
 *     <WebView Source="pages/help.html"/>
 *     <WebView><![CDATA[<h1>Hello</h1><p>From <b>HTML</b>.</p>]]></WebView>
 *
 * Everything here is to be called from the UI thread, after Rinit.
 */

#ifndef RAVEN_WEB_H
#define RAVEN_WEB_H

#include <Raven.h>

/* The web functions are part of the Raven library itself (raven.dll, libraven.a...). */
#define RAVEN_WEB_API RAVEN_API

#ifdef __cplusplus
extern "C" {
#endif

	/* ============================================================================
	 *  THE BROWSER
	 *
	 *  One browser process serves every web view. It starts with the first
	 *  view and stops when the last one is deleted.
	 * ============================================================================ */

	/**
	 * @return R_TRUE when a browser that can be driven was found: the one
	 * given to Rweb_set_browser_path, else the one named by the environment
	 * variable RAVEN_WEB_BROWSER, else one found where they are usually
	 * installed. Without one, web views show a message instead of pages.
	 */
	RAVEN_WEB_API int Rweb_is_available(void);
	/** @brief Path of the browser program to use (NULL: look for one). To call before the first view is created. */
	RAVEN_WEB_API void Rweb_set_browser_path(const char* path);
	/** @return The browser that is (or would be) used, or "" when there is none. */
	RAVEN_WEB_API const char* Rweb_get_browser_path(void);
	/**
	 * @brief Folder where the browser keeps its profile (cookies, storage,
	 * cache). By default a temporary folder, deleted when the browser
	 * stops: nothing is remembered from one run to the next. To call before
	 * the first view is created.
	 */
	RAVEN_WEB_API void Rweb_set_profile_folder(const char* path);
	/**
	 * @brief More arguments for the command line of the browser, separated
	 * by spaces (e.g. "--proxy-server=localhost:8080"). Without a call to
	 * this function, those of the environment variable RAVEN_WEB_ARGUMENTS.
	 */
	RAVEN_WEB_API void Rweb_set_browser_arguments(const char* arguments);
	/** @brief Makes the tag <WebView> known to the XML loader. To call once after Rinit. */
	RAVEN_WEB_API void Rweb_register_xml(void);
	/**
	 * @brief Stops the browser now and forgets every setting. The views
	 * that remain show their last picture. Rshutdown deletes the views, and
	 * that stops the browser too: this is only for stopping it earlier.
	 */
	RAVEN_WEB_API void Rweb_shutdown(void);

	/* ============================================================================
	 *  WEB VIEW
	 * ============================================================================ */

	/** @brief Creates a web view: a widget like the others (size, slot, visibility...). */
	RAVEN_WEB_API Rid Rweb_view_create(void);
	/** @return R_TRUE when the widget is a web view. */
	RAVEN_WEB_API int Rweb_view_is_valid(Rid view);

	/** @brief Goes to an address ("https://...", "file:///...", "about:blank"). */
	RAVEN_WEB_API void Rweb_view_load_url(Rid view, const char* url);
	/** @brief Shows a file of the computer (an .html file and what it refers to). */
	RAVEN_WEB_API void Rweb_view_load_file(Rid view, const char* path);
	/**
	 * @brief Shows HTML given as text.
	 * @param base_url Where the relative addresses of the text are looked
	 * for ("file:///C:/app/pages/"), or NULL.
	 */
	RAVEN_WEB_API void Rweb_view_load_html(Rid view, const char* html, const char* base_url);

	RAVEN_WEB_API void Rweb_view_reload(Rid view);
	RAVEN_WEB_API void Rweb_view_stop(Rid view);
	RAVEN_WEB_API void Rweb_view_go_back(Rid view);
	RAVEN_WEB_API void Rweb_view_go_forward(Rid view);
	RAVEN_WEB_API int Rweb_view_can_go_back(Rid view);
	RAVEN_WEB_API int Rweb_view_can_go_forward(Rid view);

	/** @return The address of the page shown. Valid until it changes. */
	RAVEN_WEB_API const char* Rweb_view_get_url(Rid view);
	RAVEN_WEB_API const char* Rweb_view_get_title(Rid view);
	RAVEN_WEB_API int Rweb_view_is_loading(Rid view);
	/** @return R_TRUE once the browser shows pages in this view (it takes a moment to start). */
	RAVEN_WEB_API int Rweb_view_is_ready(Rid view);
	/** @return What went wrong ("no browser found", "the browser stopped"...), or "" when all is well. */
	RAVEN_WEB_API const char* Rweb_view_get_error(Rid view);

	/**
	 * @brief The texture that holds the last picture of the page, or
	 * R_INVALID_ID before the first one arrived. It belongs to the view; use
	 * it to draw the page somewhere else as well (Rdraw_image, an Image).
	 */
	RAVEN_WEB_API Rid Rweb_view_get_texture(Rid view);

	/** @brief Size of the content of the page: 1 = as the page asks, 1.25 = a quarter larger. */
	RAVEN_WEB_API void Rweb_view_set_zoom(Rid view, float zoom);
	RAVEN_WEB_API float Rweb_view_get_zoom(Rid view);

	typedef enum RWebFrames {
		/** Exact pictures (default): text stays sharp. */
		R_WEB_FRAMES_PNG = 0x0480,
		/** Lighter pictures, slightly blurred: for pages that move a lot (video, animation). */
		R_WEB_FRAMES_JPEG
	} RWebFrames;
	/** @param quality For JPEG, 1..100. */
	RAVEN_WEB_API void Rweb_view_set_frames(Rid view, RWebFrames format, int quality);

	/* ---- JavaScript ---- */

	/**
	 * @brief Receives the result of a script: `result` is its value written
	 * as JSON ("42", "\"text\"", "{\"a\":1}", "null"), or NULL when the
	 * script failed, `error` then telling why.
	 */
	typedef void (*RWebResultCallback)(Rid view, const char* result, const char* error, void* user_data);
	/**
	 * @brief Runs JavaScript in the page. If the script gives a promise, the
	 * result is what the promise resolves to.
	 * @param callback May be NULL.
	 */
	RAVEN_WEB_API void Rweb_view_run_javascript(Rid view, const char* script, RWebResultCallback callback, void* user_data);

	/**
	 * @brief Receives what the page sends with
	 * `window.raven.postMessage(value)`: a string as it is, anything else
	 * written as JSON.
	 */
	typedef void (*RWebMessageCallback)(Rid view, const char* message, void* user_data);
	RAVEN_WEB_API void Rweb_view_set_message_callback(Rid view, RWebMessageCallback callback, void* user_data);

	/* ---- what happens in the page ---- */

	typedef enum RWebEvent {
		/** The view shows another address (Rweb_view_get_url). */
		R_WEB_EVENT_NAVIGATED = 0x04A0,
		/** The title of the page changed (Rweb_view_get_title). */
		R_WEB_EVENT_TITLE,
		/** A page starts loading. */
		R_WEB_EVENT_LOADING,
		/** The page and what it refers to are loaded. */
		R_WEB_EVENT_LOADED,
		/** The view cannot show pages any more (Rweb_view_get_error). */
		R_WEB_EVENT_FAILED
	} RWebEvent;
	typedef void (*RWebEventCallback)(Rid view, RWebEvent event, void* user_data);
	/**
	 * @brief Called when something happens in the page. The action of the
	 * widget (Rwidget_set_action, OnChanged in XML) is fired as well each
	 * time the address changes.
	 */
	RAVEN_WEB_API void Rweb_view_set_event_callback(Rid view, RWebEventCallback callback, void* user_data);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RAVEN_WEB_H */
