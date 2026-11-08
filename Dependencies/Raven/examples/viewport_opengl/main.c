/**
 * Raven example - viewport_opengl
 * An OpenGL scene inside a Raven window, using the native child window of a
 * viewport (mode 3 of raven_viewport.h).
 *
 * Raven itself renders with Vulkan. For any other graphics API, a viewport can
 * own a real OS window (HWND / X11 Window) that Raven keeps exactly on the
 * widget. The application creates its context or swapchain on that window
 * and draws whenever it wants: OpenGL here, but Direct3D or a second Vulkan
 * swapchain work the same way.
 *
 * What this mode cannot do: the OS draws the child window above the UI, so
 * widgets cannot overlap the scene and the scene is not clipped by scroll
 * boxes (Raven hides the child when the widget is hidden). To get OpenGL
 * *under* the UI, render to a texture instead and give the pixels to Raven
 * (Rtexture_update, or zero-copy with Rvulkan_texture_create_shared and
 * GL_EXT_memory_object).
 *
 * The scene uses OpenGL 1.1 calls only, so that the example needs no loader.
 */

#include "../common/example.h"

#include <math.h>

/* ---------------------------------------------------------------------------
 *  OpenGL context on a native window: the only platform-specific part
 * ------------------------------------------------------------------------- */

#if defined(_WIN32)

#include <windows.h>
#include <GL/gl.h>

static HDC g_dc;
static HGLRC g_context;

static int gl_create(const RNativeWindow* native) {
	PIXELFORMATDESCRIPTOR descriptor;
	int format;

	memset(&descriptor, 0, sizeof(descriptor));
	descriptor.nSize = sizeof(descriptor);
	descriptor.nVersion = 1;
	descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	descriptor.iPixelType = PFD_TYPE_RGBA;
	descriptor.cColorBits = 32;
	descriptor.cDepthBits = 24;

	/* the child windows of Raven have their own device context (CS_OWNDC) */
	g_dc = GetDC((HWND)native->window);
	format = ChoosePixelFormat(g_dc, &descriptor);
	if (!format || !SetPixelFormat(g_dc, format, &descriptor)) return 0;
	g_context = wglCreateContext(g_dc);
	return g_context && wglMakeCurrent(g_dc, g_context);
}

static void gl_swap(void) {
	SwapBuffers(g_dc);
}

static void gl_destroy(void) {
	wglMakeCurrent(NULL, NULL);
	if (g_context) wglDeleteContext(g_context);
	g_context = NULL;
}

#else /* X11 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/gl.h>
#include <GL/glx.h>

static Display* g_display;
static Window g_drawable;
static GLXContext g_context;

static int gl_create(const RNativeWindow* native) {
	XWindowAttributes attributes;
	XVisualInfo wanted;
	XVisualInfo* visual;
	int count = 0;

	g_display = (Display*)native->display;
	g_drawable = (Window)native->window;

	/* the context must use the visual the window was created with */
	if (!XGetWindowAttributes(g_display, g_drawable, &attributes)) return 0;
	wanted.visualid = XVisualIDFromVisual(attributes.visual);
	visual = XGetVisualInfo(g_display, VisualIDMask, &wanted, &count);
	if (!visual) return 0;
	g_context = glXCreateContext(g_display, visual, NULL, True);
	XFree(visual);
	return g_context && glXMakeCurrent(g_display, g_drawable, g_context);
}

static void gl_swap(void) {
	glXSwapBuffers(g_display, g_drawable);
}

static void gl_destroy(void) {
	if (!g_display) return;
	glXMakeCurrent(g_display, None, NULL);
	if (g_context) glXDestroyContext(g_display, g_context);
	g_context = NULL;
}

#endif

/* ---------------------------------------------------------------------------
 *  Scene
 * ------------------------------------------------------------------------- */

static struct {
	Rid window, viewport, speed, wireframe, info;
	int context_ready;
	float angle;
} app;

static void scene_draw(int width, int height) {
	static const float colors[6][3] = {
		{ 1.0f, 0.35f, 0.2f }, { 0.25f, 0.85f, 0.4f }, { 0.25f, 0.5f, 1.0f },
		{ 1.0f, 0.8f, 0.2f }, { 0.8f, 0.3f, 0.9f }, { 0.2f, 0.85f, 0.9f }
	};
	static const float faces[6][4][3] = {
		{ { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } }, { { 1, -1, -1 }, { -1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 } },
		{ { -1, 1, 1 }, { 1, 1, 1 }, { 1, 1, -1 }, { -1, 1, -1 } }, { { -1, -1, -1 }, { 1, -1, -1 }, { 1, -1, 1 }, { -1, -1, 1 } },
		{ { 1, -1, 1 }, { 1, -1, -1 }, { 1, 1, -1 }, { 1, 1, 1 } }, { { -1, -1, -1 }, { -1, -1, 1 }, { -1, 1, 1 }, { -1, 1, -1 } }
	};
	const double aspect = (double)width / (double)height;
	int face, corner;

	glViewport(0, 0, width, height);
	glClearColor(0.05f, 0.06f, 0.10f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glPolygonMode(GL_FRONT_AND_BACK, Rcheckbox_is_checked(app.wireframe) ? GL_LINE : GL_FILL);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glFrustum(-0.1 * aspect, 0.1 * aspect, -0.1, 0.1, 0.2, 20.0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glTranslatef(0.0f, 0.0f, -5.0f);
	glRotatef(app.angle * 40.0f, 0.3f, 1.0f, 0.2f);

	glBegin(GL_QUADS);
	for (face = 0; face < 6; face++) {
		glColor3fv(colors[face]);
		for (corner = 0; corner < 4; corner++) glVertex3fv(faces[face][corner]);
	}
	glEnd();
}

/* Called once per frame. */
static void on_frame(double delta_seconds, void* user_data) {
	int width = 0, height = 0;
	(void)user_data;
	if (!app.context_ready || !Rwindow_is_valid(app.window)) return;

	app.angle += (float)delta_seconds * Rslider_get_value(app.speed);
	/* the child window has exactly the size of the widget, in pixels */
	Rviewport_get_pixel_size(app.viewport, &width, &height);
	if (width <= 0 || height <= 0) return;
	scene_draw(width, height);
	gl_swap();
}

static const char g_layout[] =
	"<Window Title=\"Raven - OpenGL viewport\" Width=\"900\" Height=\"540\">"
	"  <HBox>"
	"    <VBox Width=\"240\" Spacing=\"10\" Slot.Padding=\"12\">"
	"      <Label TextSize=\"16\">OpenGL in a native viewport</Label>"
	"      <Label Wrap=\"true\" Opacity=\"0.7\">The cube is drawn by an OpenGL context created on a child window that Raven keeps on the viewport widget.</Label>"
	"      <Label>Speed</Label>"
	"      <Slider Name=\"speed\" Min=\"0\" Max=\"4\" Value=\"1\"/>"
	"      <Checkbox Name=\"wireframe\" Text=\"Wireframe\"/>"
	"      <Label Name=\"info\" Wrap=\"true\" Opacity=\"0.7\"/>"
	"    </VBox>"
	"    <Separator Orientation=\"Vertical\"/>"
	"    <Viewport Name=\"scene\" Slot.Fill=\"1\" Realtime=\"true\"/>"
	"  </HBox>"
	"</Window>";

int main(void) {
	RNativeWindow native;

	if (!example_init("viewport_opengl")) return 1;

	app.window = Rwindow_create_from_xml(g_layout, sizeof(g_layout) - 1);
	app.viewport = Rwidget_find(app.window, "scene");
	app.speed = Rwidget_find(app.window, "speed");
	app.wireframe = Rwidget_find(app.window, "wireframe");
	app.info = Rwidget_find(app.window, "info");

	/* asks Raven for a child window on the viewport, then puts an OpenGL context on it */
	memset(&native, 0, sizeof(native));
	if (Rviewport_set_native(app.viewport, R_TRUE) && Rviewport_get_native_handle(app.viewport, &native) && gl_create(&native)) {
		app.context_ready = 1;
		Rlabel_set_text(app.info, (const char*)glGetString(GL_RENDERER));
	} else {
		Rlabel_set_text(app.info, "Cannot create an OpenGL context on the viewport.");
	}

	Rset_frame_callback(on_frame, NULL);
	/* the loop belongs to the application: Raven draws one frame per call,
	 * and Rwait_frame sleeps until there is something to draw */
	if (!example_automated(app.window)) {
		while (Rwait_frame(-1.0)) {
		}
	}

	/* the context goes before the window it was created on */
	Rset_frame_callback(NULL, NULL);
	if (app.context_ready) gl_destroy();
	Rshutdown();
	return 0;
}
