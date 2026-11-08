/**
 * Raven - render/bidi.c
 * Text written from right to left: the order in which a line is drawn, and
 * the shapes of the Arabic letters.
 *
 * A line is kept in its logical order (the order it is typed in) everywhere
 * in Raven; only drawing and measuring see it in visual order:
 *
 *   1. Arabic letters take their joined shape (isolated, initial, medial,
 *      final, from the presentation forms of Unicode) and lam + alef become
 *      one ligature: Arabic is readable with any font that has those forms.
 *   2. The bidirectional algorithm of Unicode (UAX #9) gives each character a
 *      level, from the strong characters around it: a paragraph that starts
 *      with Arabic or Hebrew is right to left, English inside it stays left to
 *      right, numbers stay in their order, the punctuation between two runs
 *      takes the direction of the paragraph. This is the implicit part of the
 *      algorithm (rules W1-W7, N1-N2, I1-I2, L2, L4), without the explicit
 *      embeddings (U+202A..U+202E and the isolates are ignored and not drawn).
 *   3. The runs of odd level are reversed, and the brackets in them mirrored.
 */

#include "font.h"
#include "bidi.h"

#include <string.h>

typedef enum rvn_bidi_class_t {
	RVN_BIDI_L = 0,   /* strong left to right */
	RVN_BIDI_R,       /* strong right to left (Hebrew, Arabic letters) */
	RVN_BIDI_EN,      /* European digit */
	RVN_BIDI_AN,      /* Arabic digit */
	RVN_BIDI_ES,      /* + - */
	RVN_BIDI_CS,      /* , . : / */
	RVN_BIDI_ET,      /* # $ % ° currency */
	RVN_BIDI_NSM,     /* combining mark: the class of what it is on */
	RVN_BIDI_WS,      /* space */
	RVN_BIDI_ON,      /* other neutral */
	RVN_BIDI_AL       /* Arabic letter (strong right to left, makes digits Arabic) */
} rvn_bidi_class_t;

static int rvn_bidi_is_rtl_codepoint(uint32_t c) {
	return (c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFE) ||
		(c >= 0x10800 && c <= 0x10FFF) || (c >= 0x1E800 && c <= 0x1EFFF);
}

static rvn_bidi_class_t rvn_bidi_class(uint32_t c) {
	if (c >= '0' && c <= '9') return RVN_BIDI_EN;
	if (c == ' ' || c == '\t' || c == 0x00A0 || c == 0x2007 || c == 0x202F || (c >= 0x2000 && c <= 0x200A) || c == 0x3000) return RVN_BIDI_WS;
	if (c == '+' || c == '-' || c == 0x2212) return RVN_BIDI_ES;
	if (c == ',' || c == '.' || c == ':' || c == '/' || c == 0x060C || c == 0x00A0) return RVN_BIDI_CS;
	if (c == '#' || c == '$' || c == '%' || c == 0x00B0 || c == 0x00A2 || c == 0x00A3 || c == 0x00A5 || c == 0x20AC || (c >= 0x20A0 && c <= 0x20CF) || c == 0x066A)
		return RVN_BIDI_ET;
	if ((c >= 0x0300 && c <= 0x036F) || (c >= 0x0591 && c <= 0x05BD) || c == 0x05BF || c == 0x05C1 || c == 0x05C2 || c == 0x05C4 || c == 0x05C5 || c == 0x05C7 ||
		(c >= 0x0610 && c <= 0x061A) || (c >= 0x064B && c <= 0x065F) || c == 0x0670 || (c >= 0x06D6 && c <= 0x06DC) || (c >= 0x06DF && c <= 0x06E4) ||
		c == 0x06E7 || c == 0x06E8 || (c >= 0x06EA && c <= 0x06ED) || (c >= 0xFE00 && c <= 0xFE0F) || (c >= 0x20D0 && c <= 0x20FF))
		return RVN_BIDI_NSM;
	if ((c >= 0x0660 && c <= 0x0669) || c == 0x066B || c == 0x066C) return RVN_BIDI_AN;
	if (c >= 0x06F0 && c <= 0x06F9) return RVN_BIDI_EN;
	if ((c >= 0x0600 && c <= 0x07BF) || (c >= 0x0860 && c <= 0x08FF) || (c >= 0xFB50 && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFE)) return RVN_BIDI_AL;
	if (rvn_bidi_is_rtl_codepoint(c)) return RVN_BIDI_R;
	if (c < 0x80) {
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return RVN_BIDI_L;
		return RVN_BIDI_ON;
	}
	/* general punctuation, symbols, arrows, box drawing, emoji: neutral */
	if ((c >= 0x2010 && c <= 0x2BFF) || (c >= 0x3001 && c <= 0x3003) || (c >= 0xFE10 && c <= 0xFE6F) || (c >= 0x1F000 && c <= 0x1FAFF) ||
		c == 0x00AB || c == 0x00BB || c == 0x00A1 || c == 0x00BF || c == 0x00A7 || c == 0x00B7 || c == 0x00D7 || c == 0x00F7)
		return RVN_BIDI_ON;
	return RVN_BIDI_L;
}

/* Explicit formatting characters: not drawn (their effect is not applied). */
static int rvn_bidi_is_format(uint32_t c) {
	return c == 0x200E || c == 0x200F || c == 0x061C || (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069);
}

int rvn_bidi_needed(const char* text, int length) {
	const char* cursor = text;
	const char* end = text + length;
	while (cursor < end) {
		/* everything below U+0590 is two bytes or less with a lead byte below 0xD6 */
		if ((uint8_t)*cursor < 0xD6) {
			cursor++;
			continue;
		}
		{
			uint32_t c = rvn_utf8_decode(&cursor, end);
			if (rvn_bidi_is_rtl_codepoint(c) || rvn_bidi_is_format(c)) return R_TRUE;
		}
	}
	return R_FALSE;
}

/* ---------------------------------------------------------------------------
 *  Arabic shaping
 * ------------------------------------------------------------------------- */

/* Joining of a letter: 0 none, 1 joins on its right only (to the letter before it), 2 on both sides. */
typedef struct rvn_arabic_form_t {
	uint16_t letter;
	uint16_t isolated; /* the final form is isolated + 1; initial + 2 and medial + 3 for dual-joining letters */
	uint8_t joining;
} rvn_arabic_form_t;

static const rvn_arabic_form_t g_arabic_forms[] = {
	{ 0x0621, 0xFE80, 0 }, { 0x0622, 0xFE81, 1 }, { 0x0623, 0xFE83, 1 }, { 0x0624, 0xFE85, 1 }, { 0x0625, 0xFE87, 1 },
	{ 0x0626, 0xFE89, 2 }, { 0x0627, 0xFE8D, 1 }, { 0x0628, 0xFE8F, 2 }, { 0x0629, 0xFE93, 1 }, { 0x062A, 0xFE95, 2 },
	{ 0x062B, 0xFE99, 2 }, { 0x062C, 0xFE9D, 2 }, { 0x062D, 0xFEA1, 2 }, { 0x062E, 0xFEA5, 2 }, { 0x062F, 0xFEA9, 1 },
	{ 0x0630, 0xFEAB, 1 }, { 0x0631, 0xFEAD, 1 }, { 0x0632, 0xFEAF, 1 }, { 0x0633, 0xFEB1, 2 }, { 0x0634, 0xFEB5, 2 },
	{ 0x0635, 0xFEB9, 2 }, { 0x0636, 0xFEBD, 2 }, { 0x0637, 0xFEC1, 2 }, { 0x0638, 0xFEC5, 2 }, { 0x0639, 0xFEC9, 2 },
	{ 0x063A, 0xFECD, 2 }, { 0x0641, 0xFED1, 2 }, { 0x0642, 0xFED5, 2 }, { 0x0643, 0xFED9, 2 }, { 0x0644, 0xFEDD, 2 },
	{ 0x0645, 0xFEE1, 2 }, { 0x0646, 0xFEE5, 2 }, { 0x0647, 0xFEE9, 2 }, { 0x0648, 0xFEED, 1 }, { 0x0649, 0xFEEF, 1 },
	{ 0x064A, 0xFEF1, 2 },
	/* Persian and Urdu letters */
	{ 0x067E, 0xFB56, 2 }, { 0x0686, 0xFB7A, 2 }, { 0x0698, 0xFB8A, 1 }, { 0x06A9, 0xFB8E, 2 }, { 0x06AF, 0xFB92, 2 },
	{ 0x06CC, 0xFBFC, 2 }, { 0x0679, 0xFB66, 2 }, { 0x0688, 0xFB88, 1 }, { 0x0691, 0xFB8C, 1 }, { 0x06BA, 0xFB9E, 1 },
	{ 0x06BE, 0xFBAA, 2 }, { 0x06C1, 0xFBA6, 2 }, { 0x06D2, 0xFBAE, 1 }
};

static const rvn_arabic_form_t* rvn_arabic_form(uint32_t c) {
	size_t lo = 0, hi = RVN_COUNT_OF(g_arabic_forms);
	/* (sorted in two parts: search both) */
	size_t i;
	if (c >= 0x0621 && c <= 0x064A) {
		while (lo < 36 && g_arabic_forms[lo].letter < c) lo++;
		return lo < 36 && g_arabic_forms[lo].letter == c ? &g_arabic_forms[lo] : NULL;
	}
	for (i = 36; i < hi; i++)
		if (g_arabic_forms[i].letter == c) return &g_arabic_forms[i];
	return NULL;
}

/* Letters that join to the next one (dual joining) and the tatweel. */
static int rvn_arabic_joins_left(uint32_t c) {
	const rvn_arabic_form_t* form;
	if (c == 0x0640) return R_TRUE;
	form = rvn_arabic_form(c);
	return form && form->joining == 2;
}

static int rvn_arabic_joins_right(uint32_t c) {
	if (c == 0x0640) return R_TRUE;
	return rvn_arabic_form(c) != NULL && rvn_arabic_form(c)->joining > 0;
}

static int rvn_arabic_transparent(uint32_t c) {
	return rvn_bidi_class(c) == RVN_BIDI_NSM;
}

/* Shapes src[0..n) (logical order, unshaped letters) into dst; a lam-alef pair
 * leaves out its alef. Returns the number of characters written. */
static int rvn_arabic_shape(const uint32_t* src, uint32_t* dst, int n) {
	int i, k, out = 0;
	for (i = 0; i < n; i++) {
		const rvn_arabic_form_t* form = rvn_arabic_form(src[i]);
		uint32_t before = 0, after = 0;
		int join_before, join_after, after_index;
		if (!form) {
			/* (an alef eaten by the lam before it is marked 0) */
			dst[i] = src[i];
			continue;
		}
		for (k = i - 1; k >= 0 && rvn_arabic_transparent(src[k]); k--) {}
		if (k >= 0) before = src[k];
		for (k = i + 1; k < n && rvn_arabic_transparent(src[k]); k++) {}
		after_index = k;
		if (k < n) after = src[k];
		join_before = before && rvn_arabic_joins_left(before);
		join_after = after && form->joining == 2 && rvn_arabic_joins_right(after);

		/* lam + alef: one ligature */
		if (src[i] == 0x0644 && after_index < n && (after == 0x0622 || after == 0x0623 || after == 0x0625 || after == 0x0627)) {
			uint32_t ligature = after == 0x0622 ? 0xFEF5 : after == 0x0623 ? 0xFEF7 : after == 0x0625 ? 0xFEF9 : 0xFEFB;
			dst[i] = join_before ? ligature + 1 : ligature;
			/* the marks between them stay; the alef goes */
			for (k = i + 1; k < after_index; k++) dst[k] = src[k];
			dst[after_index] = 0;
			i = after_index;
			continue;
		}
		if (form->joining == 0) dst[i] = form->isolated;
		else if (form->joining == 1) dst[i] = join_before ? form->isolated + 1u : form->isolated;
		else if (join_before && join_after) dst[i] = form->isolated + 3u;
		else if (join_before) dst[i] = form->isolated + 1u;
		else if (join_after) dst[i] = form->isolated + 2u;
		else dst[i] = form->isolated;
	}
	for (i = 0; i < n; i++)
		if (dst[i]) dst[out++] = dst[i];
	return out;
}

/* rvn_arabic_joins_left must also know the shaped forms (the letter before is shaped first). */
static uint32_t rvn_arabic_unshape(uint32_t c) {
	size_t i;
	if (c < 0xFB50) return c;
	for (i = 0; i < RVN_COUNT_OF(g_arabic_forms); i++) {
		uint32_t base = g_arabic_forms[i].isolated, span = g_arabic_forms[i].joining == 2 ? 4u : (g_arabic_forms[i].joining == 1 ? 2u : 1u);
		if (c >= base && c < base + span) return g_arabic_forms[i].letter;
	}
	if (c >= 0xFEF5 && c <= 0xFEFC) return 0x0627; /* a lam-alef joins like an alef: on its right only */
	return c;
}

/* ---------------------------------------------------------------------------
 *  Bidirectional algorithm (implicit levels)
 * ------------------------------------------------------------------------- */

static uint32_t rvn_bidi_mirror(uint32_t c) {
	switch (c) {
		case '(': return ')'; case ')': return '(';
		case '[': return ']'; case ']': return '[';
		case '{': return '}'; case '}': return '{';
		case '<': return '>'; case '>': return '<';
		case 0x00AB: return 0x00BB; case 0x00BB: return 0x00AB;
		case 0x2039: return 0x203A; case 0x203A: return 0x2039;
		case 0x2264: return 0x2265; case 0x2265: return 0x2264;
		default: return c;
	}
}

static int rvn_bidi_strong_rtl(rvn_bidi_class_t t) { return t == RVN_BIDI_R || t == RVN_BIDI_AL; }

int rvn_bidi_paragraph_rtl(const char* text, int length) {
	const char* cursor = text;
	const char* end = text + length;
	while (cursor < end) {
		rvn_bidi_class_t t = rvn_bidi_class(rvn_utf8_decode(&cursor, end));
		if (t == RVN_BIDI_L) return R_FALSE;
		if (rvn_bidi_strong_rtl(t)) return R_TRUE;
	}
	return R_FALSE;
}

static void rvn_bidi_levels(const uint32_t* cp, int n, int base, uint8_t* levels, rvn_bidi_class_t* t) {
	int i, j;
	rvn_bidi_class_t sor = base ? RVN_BIDI_R : RVN_BIDI_L;
	for (i = 0; i < n; i++) t[i] = rvn_bidi_class(cp[i]);

	/* W1: a mark takes the class of what it is on */
	for (i = 0; i < n; i++) if (t[i] == RVN_BIDI_NSM) t[i] = i ? t[i - 1] : sor;
	/* W2: European digits after an Arabic letter are Arabic digits; W3: AL -> R */
	{
		rvn_bidi_class_t last = sor;
		for (i = 0; i < n; i++) {
			if (t[i] == RVN_BIDI_L || t[i] == RVN_BIDI_R || t[i] == RVN_BIDI_AL) last = t[i];
			else if (t[i] == RVN_BIDI_EN && last == RVN_BIDI_AL) t[i] = RVN_BIDI_AN;
		}
		for (i = 0; i < n; i++) if (t[i] == RVN_BIDI_AL) t[i] = RVN_BIDI_R;
	}
	/* W4: a single separator between two numbers of the same kind joins them */
	for (i = 1; i + 1 < n; i++) {
		if (t[i] == RVN_BIDI_ES && t[i - 1] == RVN_BIDI_EN && t[i + 1] == RVN_BIDI_EN) t[i] = RVN_BIDI_EN;
		else if (t[i] == RVN_BIDI_CS && t[i - 1] == t[i + 1] && (t[i - 1] == RVN_BIDI_EN || t[i - 1] == RVN_BIDI_AN)) t[i] = t[i - 1];
	}
	/* W5: terminators next to European digits are European digits */
	for (i = 0; i < n; i++) {
		if (t[i] != RVN_BIDI_ET) continue;
		for (j = i; j < n && t[j] == RVN_BIDI_ET; j++) {}
		if ((i > 0 && t[i - 1] == RVN_BIDI_EN) || (j < n && t[j] == RVN_BIDI_EN)) {
			int k;
			for (k = i; k < j; k++) t[k] = RVN_BIDI_EN;
		}
		i = j - 1;
	}
	/* W6: the other separators and terminators are neutral */
	for (i = 0; i < n; i++) if (t[i] == RVN_BIDI_ES || t[i] == RVN_BIDI_CS || t[i] == RVN_BIDI_ET) t[i] = RVN_BIDI_ON;
	/* W7: European digits after a strong L are L */
	{
		rvn_bidi_class_t last = sor;
		for (i = 0; i < n; i++) {
			if (t[i] == RVN_BIDI_L || t[i] == RVN_BIDI_R) last = t[i];
			else if (t[i] == RVN_BIDI_EN && last == RVN_BIDI_L) t[i] = RVN_BIDI_L;
		}
	}
	/* N1, N2: a run of neutrals between two of the same direction takes it, else the paragraph's */
	for (i = 0; i < n; i++) {
		rvn_bidi_class_t before, after, side;
		if (t[i] != RVN_BIDI_ON && t[i] != RVN_BIDI_WS) continue;
		for (j = i; j < n && (t[j] == RVN_BIDI_ON || t[j] == RVN_BIDI_WS); j++) {}
		before = i ? t[i - 1] : sor;
		after = j < n ? t[j] : sor;
		if (before == RVN_BIDI_EN || before == RVN_BIDI_AN) before = RVN_BIDI_R;
		if (after == RVN_BIDI_EN || after == RVN_BIDI_AN) after = RVN_BIDI_R;
		side = before == after ? before : (base ? RVN_BIDI_R : RVN_BIDI_L);
		{
			int k;
			for (k = i; k < j; k++) t[k] = side;
		}
		i = j - 1;
	}
	/* I1, I2 */
	for (i = 0; i < n; i++) {
		if (!base) levels[i] = t[i] == RVN_BIDI_R ? 1 : (t[i] == RVN_BIDI_AN || t[i] == RVN_BIDI_EN) ? 2 : 0;
		else levels[i] = t[i] == RVN_BIDI_L || t[i] == RVN_BIDI_EN || t[i] == RVN_BIDI_AN ? 2 : 1;
	}
	/* L1: the spaces at the end of the line go back to the paragraph level */
	for (i = n - 1; i >= 0 && rvn_bidi_class(cp[i]) == RVN_BIDI_WS; i--) levels[i] = (uint8_t)base;
}

static void rvn_bidi_reverse(uint32_t* cp, int from, int to) {
	while (from < to) {
		uint32_t swap = cp[from];
		cp[from++] = cp[to];
		cp[to--] = swap;
	}
}

static void rvn_bidi_reverse_levels(uint8_t* levels, int from, int to) {
	while (from < to) {
		uint8_t swap = levels[from];
		levels[from++] = levels[to];
		levels[to--] = swap;
	}
}

char* rvn_bidi_visual(const char* text, int length, int* out_length) {
	const char* cursor = text;
	const char* end = text + length;
	uint32_t* cp;
	uint32_t* original;
	uint8_t* levels;
	rvn_bidi_class_t* classes;
	char* out;
	int n = 0, i, base, highest = 0, level, size = 0;

	cp = (uint32_t*)rvn_alloc(((size_t)length + 1) * sizeof(uint32_t));
	while (cursor < end) {
		uint32_t c = rvn_utf8_decode(&cursor, end);
		if (rvn_bidi_is_format(c)) continue;
		cp[n++] = c;
	}
	original = (uint32_t*)rvn_alloc(((size_t)n + 1) * sizeof(uint32_t));
	memcpy(original, cp, (size_t)n * sizeof(uint32_t));

	/* the paragraph direction, from the first strong character */
	base = 0;
	for (i = 0; i < n; i++) {
		rvn_bidi_class_t t = rvn_bidi_class(cp[i]);
		if (t == RVN_BIDI_L) break;
		if (rvn_bidi_strong_rtl(t)) { base = 1; break; }
	}

	/* shaping (logical order), from the base letters (a text may already hold presentation forms) */
	for (i = 0; i < n; i++) original[i] = rvn_arabic_unshape(cp[i]);
	n = rvn_arabic_shape(original, cp, n);
	rvn_free(original);

	levels = (uint8_t*)rvn_alloc((size_t)n + 1);
	classes = (rvn_bidi_class_t*)rvn_alloc(((size_t)n + 1) * sizeof(rvn_bidi_class_t));
	/* (the classes of the shaped forms are the classes of their letters: AL) */
	rvn_bidi_levels(cp, n, base, levels, classes);

	/* mirrored brackets in the right-to-left runs */
	for (i = 0; i < n; i++) {
		if (levels[i] > highest) highest = levels[i];
		if (levels[i] & 1) cp[i] = rvn_bidi_mirror(cp[i]);
	}
	/* L2: from the highest level down to the lowest odd one, reverse every run at or above it */
	for (level = highest; level >= 1; level--) {
		for (i = 0; i < n; i++) {
			int j;
			if (levels[i] < level) continue;
			for (j = i; j < n && levels[j] >= level; j++) {}
			rvn_bidi_reverse(cp, i, j - 1);
			rvn_bidi_reverse_levels(levels, i, j - 1);
			i = j;
		}
	}

	out = (char*)rvn_alloc((size_t)n * 4 + 1);
	for (i = 0; i < n; i++) size += rvn_utf8_encode(cp[i], out + size);
	out[size] = 0;
	*out_length = size;
	rvn_free(cp);
	rvn_free(levels);
	rvn_free(classes);
	return out;
}
