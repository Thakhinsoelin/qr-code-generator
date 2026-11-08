/**
 * Raven - render/font_system.c
 * The fonts of the system, for the characters that the fonts of the
 * application do not have: other alphabets (Arabic, Hebrew, Cyrillic,
 * Greek, Devanagari, Thai...), Chinese, Japanese, Korean, symbols and emoji.
 *
 * For a character, the candidates are the files made for its script, then
 * the broad fonts. font.c opens them on first need, keeps those that load,
 * and remembers which font has each character.
 *
 *   Windows  %WINDIR%\Fonts: Segoe UI, Segoe UI Emoji (color, COLR),
 *            Segoe UI Symbol, Microsoft YaHei, Yu Gothic, Malgun Gothic,
 *            Nirmala UI, Ebrima, Tahoma, Arial...
 *   macOS    /System/Library/Fonts (and Supplemental): Geeza Pro, Arial
 *            Hebrew, PingFang, Hiragino, Apple SD Gothic Neo, Kohinoor,
 *            Thonburi, Apple Symbols, Arial Unicode...
 *   Linux    every .ttf / .otf / .ttc in the font folders, chosen by name
 *            (Noto Sans <Script>, Noto Color Emoji in its COLR version,
 *            DejaVu Sans, FreeSans...)
 */

#include "font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

typedef enum rvn_script_t {
	RVN_SCRIPT_LATIN = 0, RVN_SCRIPT_GREEK, RVN_SCRIPT_CYRILLIC, RVN_SCRIPT_ARMENIAN, RVN_SCRIPT_HEBREW, RVN_SCRIPT_ARABIC,
	RVN_SCRIPT_SYRIAC, RVN_SCRIPT_THAANA, RVN_SCRIPT_INDIC, RVN_SCRIPT_THAI, RVN_SCRIPT_LAO, RVN_SCRIPT_GEORGIAN,
	RVN_SCRIPT_ETHIOPIC, RVN_SCRIPT_KHMER, RVN_SCRIPT_MYANMAR, RVN_SCRIPT_SINHALA, RVN_SCRIPT_TIBETAN,
	RVN_SCRIPT_HANGUL, RVN_SCRIPT_KANA, RVN_SCRIPT_HAN, RVN_SCRIPT_SYMBOL, RVN_SCRIPT_EMOJI, RVN_SCRIPT_COUNT
} rvn_script_t;

static rvn_script_t rvn_script_of(uint32_t c) {
	if (c < 0x0370) return RVN_SCRIPT_LATIN;
	if (c < 0x0400) return RVN_SCRIPT_GREEK;
	if (c < 0x0530) return RVN_SCRIPT_CYRILLIC;
	if (c < 0x0590) return RVN_SCRIPT_ARMENIAN;
	if (c < 0x0600) return RVN_SCRIPT_HEBREW;
	if (c < 0x0700) return RVN_SCRIPT_ARABIC;
	if (c < 0x0780) return RVN_SCRIPT_SYRIAC;
	if (c < 0x07C0) return RVN_SCRIPT_THAANA;
	if (c >= 0x0860 && c < 0x0900) return RVN_SCRIPT_ARABIC;
	if (c >= 0x0900 && c < 0x0D80) return RVN_SCRIPT_INDIC;
	if (c >= 0x0D80 && c < 0x0E00) return RVN_SCRIPT_SINHALA;
	if (c >= 0x0E00 && c < 0x0E80) return RVN_SCRIPT_THAI;
	if (c >= 0x0E80 && c < 0x0F00) return RVN_SCRIPT_LAO;
	if (c >= 0x0F00 && c < 0x1000) return RVN_SCRIPT_TIBETAN;
	if (c >= 0x1000 && c < 0x10A0) return RVN_SCRIPT_MYANMAR;
	if (c >= 0x10A0 && c < 0x1100) return RVN_SCRIPT_GEORGIAN;
	if (c >= 0x1100 && c < 0x1200) return RVN_SCRIPT_HANGUL;
	if (c >= 0x1200 && c < 0x13A0) return RVN_SCRIPT_ETHIOPIC;
	if (c >= 0x1780 && c < 0x1800) return RVN_SCRIPT_KHMER;
	if (c >= 0x1E00 && c < 0x2000) return c < 0x1F00 ? RVN_SCRIPT_LATIN : RVN_SCRIPT_GREEK;
	if (c >= 0x2000 && c < 0x2E80) return RVN_SCRIPT_SYMBOL;
	if (c >= 0x3040 && c < 0x3100) return RVN_SCRIPT_KANA;
	if (c >= 0x31F0 && c < 0x3200) return RVN_SCRIPT_KANA;
	if (c >= 0x3130 && c < 0x3190) return RVN_SCRIPT_HANGUL;
	if (c >= 0xAC00 && c < 0xD7B0) return RVN_SCRIPT_HANGUL;
	if ((c >= 0x2E80 && c < 0xA000) || (c >= 0xF900 && c < 0xFB00) || (c >= 0x20000 && c < 0x32000)) return RVN_SCRIPT_HAN;
	if (c >= 0xFB1D && c < 0xFB50) return RVN_SCRIPT_HEBREW;
	if (c >= 0xFB50 && c < 0xFE00) return RVN_SCRIPT_ARABIC;
	if (c >= 0xFE70 && c < 0xFF00) return RVN_SCRIPT_ARABIC;
	if (c >= 0xFF00 && c < 0xFFF0) return RVN_SCRIPT_HAN;
	if ((c >= 0x1F000 && c < 0x1FB00) || c == 0x2764 || (c >= 0x2600 && c < 0x27C0)) return RVN_SCRIPT_EMOJI;
	if (c >= 0x1D400 && c < 0x1D800) return RVN_SCRIPT_SYMBOL;
	return RVN_SCRIPT_LATIN;
}

static struct {
	int ready;
	/* every candidate path, per script, then the broad ones */
	char** lists[RVN_SCRIPT_COUNT + 1];
	int counts[RVN_SCRIPT_COUNT + 1];
} g_system;

static void rvn_system_add(int list, const char* path) {
	int i;
	FILE* f;
	for (i = 0; i < g_system.counts[list]; i++)
		if (strcmp(g_system.lists[list][i], path) == 0) return;
	f = fopen(path, "rb");
	if (!f) return;
	fclose(f);
	g_system.lists[list] = (char**)rvn_realloc(g_system.lists[list], (size_t)(g_system.counts[list] + 1) * sizeof(char*));
	g_system.lists[list][g_system.counts[list]++] = rvn_strdup(path);
}

#if defined(_WIN32) || defined(__APPLE__)

typedef struct rvn_system_font_t {
	int script; /* RVN_SCRIPT_COUNT: broad */
	const char* file;
} rvn_system_font_t;

static void rvn_system_add_table(const char* folder, const rvn_system_font_t* table, size_t count) {
	size_t i;
	char path[1024];
	for (i = 0; i < count; i++) {
		snprintf(path, sizeof(path), "%s%s", folder, table[i].file);
		rvn_system_add(table[i].script, path);
	}
}

#endif

#if defined(_WIN32)

static const rvn_system_font_t g_windows_fonts[] = {
	{ RVN_SCRIPT_EMOJI, "seguiemj.ttf" }, { RVN_SCRIPT_EMOJI, "seguisym.ttf" },
	{ RVN_SCRIPT_SYMBOL, "seguisym.ttf" }, { RVN_SCRIPT_SYMBOL, "segoeui.ttf" }, { RVN_SCRIPT_SYMBOL, "cambria.ttc" },
	{ RVN_SCRIPT_ARABIC, "segoeui.ttf" }, { RVN_SCRIPT_ARABIC, "tahoma.ttf" }, { RVN_SCRIPT_ARABIC, "arial.ttf" },
	{ RVN_SCRIPT_HEBREW, "segoeui.ttf" }, { RVN_SCRIPT_HEBREW, "arial.ttf" },
	{ RVN_SCRIPT_GREEK, "segoeui.ttf" }, { RVN_SCRIPT_CYRILLIC, "segoeui.ttf" }, { RVN_SCRIPT_LATIN, "segoeui.ttf" },
	{ RVN_SCRIPT_ARMENIAN, "segoeui.ttf" }, { RVN_SCRIPT_ARMENIAN, "sylfaen.ttf" }, { RVN_SCRIPT_GEORGIAN, "segoeui.ttf" },
	{ RVN_SCRIPT_THAI, "leelawui.ttf" }, { RVN_SCRIPT_THAI, "tahoma.ttf" }, { RVN_SCRIPT_LAO, "leelawui.ttf" },
	{ RVN_SCRIPT_KHMER, "leelawui.ttf" }, { RVN_SCRIPT_MYANMAR, "mmrtext.ttf" },
	{ RVN_SCRIPT_INDIC, "Nirmala.ttc" }, { RVN_SCRIPT_INDIC, "Nirmala.ttf" }, { RVN_SCRIPT_SINHALA, "Nirmala.ttc" }, { RVN_SCRIPT_SINHALA, "Nirmala.ttf" },
	{ RVN_SCRIPT_ETHIOPIC, "ebrima.ttf" }, { RVN_SCRIPT_THAANA, "mvboli.ttf" }, { RVN_SCRIPT_SYRIAC, "estre.ttf" }, { RVN_SCRIPT_TIBETAN, "himalaya.ttf" },
	{ RVN_SCRIPT_HANGUL, "malgun.ttf" },
	{ RVN_SCRIPT_KANA, "YuGothM.ttc" }, { RVN_SCRIPT_KANA, "meiryo.ttc" }, { RVN_SCRIPT_KANA, "msgothic.ttc" },
	{ RVN_SCRIPT_HAN, "msyh.ttc" }, { RVN_SCRIPT_HAN, "YuGothM.ttc" }, { RVN_SCRIPT_HAN, "simsun.ttc" }, { RVN_SCRIPT_HAN, "msjh.ttc" },
	{ RVN_SCRIPT_COUNT, "segoeui.ttf" }, { RVN_SCRIPT_COUNT, "arial.ttf" }, { RVN_SCRIPT_COUNT, "seguisym.ttf" }, { RVN_SCRIPT_COUNT, "msyh.ttc" }
};

static void rvn_system_scan(void) {
	char windows[MAX_PATH], folder[MAX_PATH + 16];
	UINT n = GetWindowsDirectoryA(windows, MAX_PATH);
	if (n == 0 || n >= MAX_PATH) strcpy(windows, "C:\\Windows");
	snprintf(folder, sizeof(folder), "%s\\Fonts\\", windows);
	rvn_system_add_table(folder, g_windows_fonts, RVN_COUNT_OF(g_windows_fonts));
}

#elif defined(__APPLE__)

static const rvn_system_font_t g_mac_fonts[] = {
	{ RVN_SCRIPT_ARABIC, "GeezaPro.ttc" }, { RVN_SCRIPT_ARABIC, "Supplemental/GeezaPro.ttc" }, { RVN_SCRIPT_ARABIC, "SFArabic.ttf" },
	{ RVN_SCRIPT_HEBREW, "SFHebrew.ttf" }, { RVN_SCRIPT_HEBREW, "Supplemental/Arial Hebrew.ttc" }, { RVN_SCRIPT_HEBREW, "ArialHB.ttc" },
	{ RVN_SCRIPT_GREEK, "Helvetica.ttc" }, { RVN_SCRIPT_CYRILLIC, "Helvetica.ttc" }, { RVN_SCRIPT_LATIN, "Helvetica.ttc" },
	{ RVN_SCRIPT_ARMENIAN, "SFArmenian.ttf" }, { RVN_SCRIPT_GEORGIAN, "SFGeorgian.ttf" },
	{ RVN_SCRIPT_THAI, "Thonburi.ttc" }, { RVN_SCRIPT_THAI, "Supplemental/Thonburi.ttc" }, { RVN_SCRIPT_LAO, "Supplemental/Lao Sangam MN.ttf" },
	{ RVN_SCRIPT_INDIC, "Kohinoor.ttc" }, { RVN_SCRIPT_INDIC, "Supplemental/DevanagariMT.ttc" }, { RVN_SCRIPT_INDIC, "Supplemental/Tamil Sangam MN.ttc" },
	{ RVN_SCRIPT_KHMER, "Supplemental/Khmer Sangam MN.ttf" }, { RVN_SCRIPT_MYANMAR, "Supplemental/Myanmar Sangam MN.ttc" },
	{ RVN_SCRIPT_SINHALA, "Supplemental/Sinhala Sangam MN.ttc" }, { RVN_SCRIPT_ETHIOPIC, "Supplemental/Kefa.ttc" },
	{ RVN_SCRIPT_TIBETAN, "Supplemental/Kailasa.ttc" }, { RVN_SCRIPT_THAANA, "Supplemental/Arial Unicode.ttf" },
	{ RVN_SCRIPT_HANGUL, "AppleSDGothicNeo.ttc" },
	{ RVN_SCRIPT_KANA, "\xE3\x83\x92\xE3\x83\xA9\xE3\x82\xAE\xE3\x83\x8E\xE8\xA7\x92\xE3\x82\xB4\xE3\x82\xB7\xE3\x83\x83\xE3\x82\xAF W3.ttc" }, { RVN_SCRIPT_KANA, "Hiragino Sans GB.ttc" },
	{ RVN_SCRIPT_HAN, "PingFang.ttc" }, { RVN_SCRIPT_HAN, "Hiragino Sans GB.ttc" }, { RVN_SCRIPT_HAN, "STHeiti Light.ttc" },
	{ RVN_SCRIPT_SYMBOL, "Apple Symbols.ttf" }, { RVN_SCRIPT_SYMBOL, "Supplemental/Arial Unicode.ttf" },
	/* (Apple Color Emoji is made of bitmaps: the outlines of Apple Symbols stand in) */
	{ RVN_SCRIPT_EMOJI, "Apple Symbols.ttf" }, { RVN_SCRIPT_EMOJI, "Supplemental/Arial Unicode.ttf" },
	{ RVN_SCRIPT_COUNT, "Supplemental/Arial Unicode.ttf" }, { RVN_SCRIPT_COUNT, "Helvetica.ttc" }, { RVN_SCRIPT_COUNT, "Apple Symbols.ttf" }
};

static void rvn_system_scan(void) {
	rvn_system_add_table("/System/Library/Fonts/", g_mac_fonts, RVN_COUNT_OF(g_mac_fonts));
	rvn_system_add_table("/Library/Fonts/", g_mac_fonts, RVN_COUNT_OF(g_mac_fonts));
}

#else

/* Names of files that cover a script, the best first (they are searched for in the file names). */
static const char* const* rvn_linux_patterns(int script) {
	static const char* const latin[] = { "NotoSans-Regular", "DejaVuSans.", "LiberationSans-Regular", "FreeSans.", NULL };
	static const char* const arabic[] = { "NotoSansArabic-Regular", "NotoNaskhArabic-Regular", "NotoSansArabicUI-Regular", "DejaVuSans.", "FreeSerif.", NULL };
	static const char* const hebrew[] = { "NotoSansHebrew-Regular", "DejaVuSans.", "FreeSans.", NULL };
	static const char* const armenian[] = { "NotoSansArmenian-Regular", "DejaVuSans.", "FreeSans.", NULL };
	static const char* const georgian[] = { "NotoSansGeorgian-Regular", "DejaVuSans.", "FreeSans.", NULL };
	static const char* const indic[] = { "NotoSansDevanagari-Regular", "NotoSansBengali-Regular", "NotoSansTamil-Regular", "NotoSansTelugu-Regular",
		"NotoSansGujarati-Regular", "NotoSansKannada-Regular", "NotoSansMalayalam-Regular", "NotoSansGurmukhi-Regular", "NotoSansOriya-Regular",
		"Lohit", "FreeSans.", NULL };
	static const char* const thai[] = { "NotoSansThai-Regular", "NotoSansThaiUI-Regular", "Garuda", "FreeSerif.", NULL };
	static const char* const lao[] = { "NotoSansLao-Regular", NULL };
	static const char* const khmer[] = { "NotoSansKhmer-Regular", NULL };
	static const char* const myanmar[] = { "NotoSansMyanmar-Regular", NULL };
	static const char* const sinhala[] = { "NotoSansSinhala-Regular", NULL };
	static const char* const tibetan[] = { "NotoSerifTibetan-Regular", "NotoSansTibetan", NULL };
	static const char* const ethiopic[] = { "NotoSansEthiopic-Regular", "Abyssinica", "FreeSerif.", NULL };
	static const char* const syriac[] = { "NotoSansSyriac", NULL };
	static const char* const thaana[] = { "NotoSansThaana-Regular", "FreeSerif.", NULL };
	static const char* const cjk[] = { "NotoSansCJK-Regular", "NotoSansCJKsc-Regular", "NotoSansCJKjp-Regular", "NotoSansSC-Regular",
		"NotoSansJP-Regular", "NotoSansKR-Regular", "wqy-microhei", "wqy-zenhei", "DroidSansFallback", "ukai", "uming", NULL };
	static const char* const symbol[] = { "NotoSansSymbols-Regular", "NotoSansSymbols2-Regular", "NotoSansMath-Regular", "DejaVuSans.", "FreeSerif.", "Symbola", NULL };
	static const char* const emoji[] = { "Noto-COLRv1", "NotoColorEmoji-COLR", "Twemoji", "NotoEmoji-Regular", "NotoEmoji", "Symbola", "NotoSansSymbols2-Regular", "DejaVuSans.", NULL };
	static const char* const broad[] = { "DejaVuSans.", "NotoSans-Regular", "FreeSans.", "FreeSerif.", "unifont", NULL };
	switch (script) {
		case RVN_SCRIPT_ARABIC: return arabic;
		case RVN_SCRIPT_HEBREW: return hebrew;
		case RVN_SCRIPT_ARMENIAN: return armenian;
		case RVN_SCRIPT_GEORGIAN: return georgian;
		case RVN_SCRIPT_INDIC: return indic;
		case RVN_SCRIPT_THAI: return thai;
		case RVN_SCRIPT_LAO: return lao;
		case RVN_SCRIPT_KHMER: return khmer;
		case RVN_SCRIPT_MYANMAR: return myanmar;
		case RVN_SCRIPT_SINHALA: return sinhala;
		case RVN_SCRIPT_TIBETAN: return tibetan;
		case RVN_SCRIPT_ETHIOPIC: return ethiopic;
		case RVN_SCRIPT_SYRIAC: return syriac;
		case RVN_SCRIPT_THAANA: return thaana;
		case RVN_SCRIPT_HANGUL: case RVN_SCRIPT_KANA: case RVN_SCRIPT_HAN: return cjk;
		case RVN_SCRIPT_SYMBOL: return symbol;
		case RVN_SCRIPT_EMOJI: return emoji;
		case RVN_SCRIPT_COUNT: return broad;
		default: return latin;
	}
}

static char** g_linux_files;
static int g_linux_file_count;

static void rvn_linux_walk(const char* folder, int depth) {
	DIR* dir;
	struct dirent* entry;
	if (depth > 5 || g_linux_file_count > 20000) return;
	dir = opendir(folder);
	if (!dir) return;
	while ((entry = readdir(dir)) != NULL) {
		char path[1024];
		struct stat info;
		size_t n = strlen(entry->d_name);
		if (entry->d_name[0] == '.') continue;
		snprintf(path, sizeof(path), "%s/%s", folder, entry->d_name);
		if (stat(path, &info) != 0) continue;
		if (S_ISDIR(info.st_mode)) {
			rvn_linux_walk(path, depth + 1);
		} else if (n > 4 && (rvn_stricmp(entry->d_name + n - 4, ".ttf") == 0 || rvn_stricmp(entry->d_name + n - 4, ".otf") == 0 ||
			rvn_stricmp(entry->d_name + n - 4, ".ttc") == 0)) {
			g_linux_files = (char**)rvn_realloc(g_linux_files, (size_t)(g_linux_file_count + 1) * sizeof(char*));
			g_linux_files[g_linux_file_count++] = rvn_strdup(path);
		}
	}
	closedir(dir);
}

static void rvn_system_scan(void) {
	const char* home = getenv("HOME");
	const char* data = getenv("XDG_DATA_HOME");
	char path[1024];
	int script, i, p;
	rvn_linux_walk("/usr/share/fonts", 0);
	rvn_linux_walk("/usr/local/share/fonts", 0);
	if (data && data[0]) {
		snprintf(path, sizeof(path), "%s/fonts", data);
		rvn_linux_walk(path, 0);
	}
	if (home) {
		snprintf(path, sizeof(path), "%s/.local/share/fonts", home);
		rvn_linux_walk(path, 0);
		snprintf(path, sizeof(path), "%s/.fonts", home);
		rvn_linux_walk(path, 0);
	}
	for (script = 0; script <= RVN_SCRIPT_COUNT; script++) {
		const char* const* patterns = rvn_linux_patterns(script);
		for (p = 0; patterns[p]; p++) {
			for (i = 0; i < g_linux_file_count; i++) {
				const char* name = strrchr(g_linux_files[i], '/');
				name = name ? name + 1 : g_linux_files[i];
				if (strncmp(name, patterns[p], strlen(patterns[p])) == 0) rvn_system_add(script, g_linux_files[i]);
			}
		}
	}
	/* the other Noto Sans files, for the scripts named nowhere above */
	for (i = 0; i < g_linux_file_count; i++) {
		const char* name = strrchr(g_linux_files[i], '/');
		name = name ? name + 1 : g_linux_files[i];
		if (strncmp(name, "NotoSans", 8) == 0 && strstr(name, "-Regular.")) rvn_system_add(RVN_SCRIPT_COUNT, g_linux_files[i]);
	}
	for (i = 0; i < g_linux_file_count; i++) rvn_free(g_linux_files[i]);
	rvn_free(g_linux_files);
	g_linux_files = NULL;
	g_linux_file_count = 0;
}

#endif

int rvn_font_system_candidates(uint32_t codepoint, const char** paths, int max) {
	int script, count = 0, i, k;
	if (!g_system.ready) {
		g_system.ready = 1;
		rvn_system_scan();
	}
	script = (int)rvn_script_of(codepoint);
	for (k = 0; k < 2; k++) {
		int list = k == 0 ? script : RVN_SCRIPT_COUNT;
		for (i = 0; i < g_system.counts[list] && count < max; i++) {
			int j, seen = 0;
			for (j = 0; j < count; j++) if (paths[j] == g_system.lists[list][i] || strcmp(paths[j], g_system.lists[list][i]) == 0) seen = 1;
			if (!seen) paths[count++] = g_system.lists[list][i];
		}
	}
	return count;
}

void rvn_font_system_shutdown(void) {
	int list, i;
	for (list = 0; list <= RVN_SCRIPT_COUNT; list++) {
		for (i = 0; i < g_system.counts[list]; i++) rvn_free(g_system.lists[list][i]);
		rvn_free(g_system.lists[list]);
	}
	memset(&g_system, 0, sizeof(g_system));
}
