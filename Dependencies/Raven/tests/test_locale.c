/**
 * Raven - tests/test_locale.c
 * Translations from CSV tables, plurals, @keys in XML, right to left layouts
 * and the accessibility settings (text scale, high contrast, colour filters,
 * names and roles for the screen readers).
 */

#include "test.h"

static const char g_csv[] =
	"\xEF\xBB\xBFkey,en,fr,ar\n"
	"# a comment\n"
	"menu.file,File,Fichier,\xD9\x85\xD9\x84\xD9\x81\n"
	"greeting,\"Hello, {0}!\",\"Bonjour, {0} !\",\n"
	"quote,\"Say \"\"hi\"\"\",\"Dis \"\"salut\"\"\",\n"
	"files.one,{n} file,{n} fichier,\n"
	"files.other,{n} files,{n} fichiers,\n"
	"only.en,English only,,\n";

static int g_changes = 0;
static void on_changed(const char* language, void* user) { (void)language; (void)user; g_changes++; }

static void test_tables(void) {
	const char* args[1] = { "Ada" };
	CHECK(Rlocale_load_csv_from_memory(g_csv, sizeof(g_csv) - 1) == 6);
	CHECK(Rlocale_get_language_count() == 3);
	CHECK_STR(Rlocale_get_language_at(1), "fr");
	Rlocale_set_changed_callback(on_changed, NULL);

	CHECK(Rlocale_set_language("en"));
	CHECK_STR(Rtr("menu.file"), "File");
	CHECK_STR(Rtr_format("greeting", args, 1), "Hello, Ada!");
	CHECK_STR(Rtr("quote"), "Say \"hi\"");
	CHECK_STR(Rtr_plural("files", 1), "1 file");
	CHECK_STR(Rtr_plural("files", 3), "3 files");
	CHECK_STR(Rtr("missing.key"), "missing.key");
	CHECK(!Rtr_exists("missing.key"));

	CHECK(Rlocale_set_language("fr"));
	CHECK(g_changes >= 2);
	CHECK_STR(Rtr("menu.file"), "Fichier");
	CHECK_STR(Rtr_format("greeting", args, 1), "Bonjour, Ada !");
	/* French: 0 and 1 are singular */
	CHECK_STR(Rtr_plural("files", 0), "0 fichier");
	CHECK_STR(Rtr_plural("files", 2), "2 fichiers");
	/* falls back to the first column */
	CHECK_STR(Rtr("only.en"), "English only");
	CHECK(!Rlocale_is_rtl());

	CHECK(Rlocale_set_language("ar"));
	CHECK(Rlocale_is_rtl());
	CHECK_STR(Rtr("menu.file"), "\xD9\x85\xD9\x84\xD9\x81");
	Rlocale_set_language("en");
	Rlocale_set_changed_callback(NULL, NULL);
}

static void test_xml_keys(void) {
	const char* xml =
		"<Window Title=\"@menu.file\" Width=\"300\" Height=\"100\"><HBox>"
		"<Button Name=\"b\" Text=\"@menu.file\"/><Label Name=\"l\" Text=\"@@home\"/>"
		"</HBox></Window>";
	Rid window = Rwindow_create_from_xml(xml, strlen(xml));
	Rid button = Rwidget_find(window, "b");
	CHECK_STR(Rwidget_get_accessible_name(button), "File");
	CHECK_STR(Rlabel_get_text(Rwidget_find(window, "l")), "@home");
	Rlocale_set_language("fr");
	CHECK_STR(Rwidget_get_accessible_name(button), "Fichier");
	Rwidget_set_text_key(button, "Text", "only.en");
	CHECK_STR(Rwidget_get_accessible_name(button), "English only");
	Rlocale_set_language("en");
	Rwindow_destroy(window);
}

static void test_direction(void) {
	const char* xml =
		"<Window Width=\"300\" Height=\"100\"><HBox>"
		"<Button Name=\"a\" Width=\"100\" Text=\"A\"/><Button Name=\"b\" Width=\"100\" Text=\"B\"/>"
		"</HBox></Window>";
	Rid window = Rwindow_create_from_xml(xml, strlen(xml));
	float ax, ay, aw, ah, bx, by, bw, bh;
	Rset_layout_direction(R_DIRECTION_LTR);
	Rdraw_frame();
	Rwidget_get_rect(Rwidget_find(window, "a"), &ax, &ay, &aw, &ah);
	Rwidget_get_rect(Rwidget_find(window, "b"), &bx, &by, &bw, &bh);
	CHECK(ax < bx);
	/* right to left: the first child is on the right */
	Rset_layout_direction(R_DIRECTION_RTL);
	CHECK(Rget_layout_direction() == R_DIRECTION_RTL);
	Rdraw_frame();
	Rwidget_get_rect(Rwidget_find(window, "a"), &ax, &ay, &aw, &ah);
	Rwidget_get_rect(Rwidget_find(window, "b"), &bx, &by, &bw, &bh);
	CHECK(ax > bx);
	CHECK_NEAR(ax + aw, 300.0f);
	Rset_layout_direction(R_DIRECTION_AUTO);
	Rwindow_destroy(window);
}

static void test_accessibility(void) {
	const char* xml =
		"<Window Title=\"Login\" Width=\"300\" Height=\"200\"><VBox>"
		"<Button Name=\"b\" Text=\"Sign in\"/><Button Name=\"icon\" AccessibleName=\"Close\"/>"
		"</VBox></Window>";
	Rid window = Rwindow_create_from_xml(xml, strlen(xml));
	Rid button = Rwidget_find(window, "b");
	CHECK(Rwidget_get_accessible_role(button) == R_ROLE_BUTTON);
	CHECK_STR(Rwidget_get_accessible_name(button), "Sign in");
	CHECK_STR(Rwidget_get_accessible_name(Rwidget_find(window, "icon")), "Close");
	Rwidget_set_accessible_name(button, "Log in");
	CHECK_STR(Rwidget_get_accessible_name(button), "Log in");
	Rwidget_set_accessible_role(button, R_ROLE_LINK);
	CHECK(Rwidget_get_accessible_role(button) == R_ROLE_LINK);
	/* without a screen reader running, an announcement is harmless */
	Raccessibility_announce("Saved", R_FALSE);

	Raccessibility_set_text_scale(1.5f);
	CHECK_NEAR(Raccessibility_get_text_scale(), 1.5f);
	Rdraw_frame();
	Raccessibility_set_text_scale(1.0f);
	Raccessibility_set_high_contrast(R_TRUE);
	CHECK(Raccessibility_get_high_contrast());
	Rdraw_frame();
	Raccessibility_set_high_contrast(R_FALSE);
	Raccessibility_set_color_filter(R_COLOR_FILTER_DEUTERANOPIA);
	CHECK(Raccessibility_get_color_filter() == R_COLOR_FILTER_DEUTERANOPIA);
	Rdraw_frame();
	Raccessibility_set_color_filter(R_COLOR_FILTER_NONE);
	Rwindow_destroy(window);
}

int main(void) {
	if (!test_init(0)) {
		printf("test_locale: cannot initialize Raven\n");
		return 1;
	}
	RUN(test_tables);
	RUN(test_xml_keys);
	RUN(test_direction);
	RUN(test_accessibility);
	Rlocale_clear();
	Rshutdown();
	return test_report("test_locale");
}
