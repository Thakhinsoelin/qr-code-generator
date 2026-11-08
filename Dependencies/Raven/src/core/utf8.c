/**
 * Raven - core/utf8.c
 * UTF-8 decoding, encoding and caret movement.
 */

#include "base.h"

uint32_t rvn_utf8_decode(const char** text, const char* end) {
	const unsigned char* s = (const unsigned char*)*text;
	const unsigned char* e = (const unsigned char*)end;
	uint32_t c, cp;
	int extra, i;

	if (s >= e) return 0;
	c = *s++;
	if (c < 0x80u) {
		*text = (const char*)s;
		return c;
	}
	if ((c & 0xE0u) == 0xC0u) { cp = c & 0x1Fu; extra = 1; }
	else if ((c & 0xF0u) == 0xE0u) { cp = c & 0x0Fu; extra = 2; }
	else if ((c & 0xF8u) == 0xF0u) { cp = c & 0x07u; extra = 3; }
	else {
		*text = (const char*)s;
		return 0xFFFDu;
	}
	for (i = 0; i < extra; i++) {
		if (s >= e || (*s & 0xC0u) != 0x80u) {
			*text = (const char*)s;
			return 0xFFFDu;
		}
		cp = (cp << 6) | (*s++ & 0x3Fu);
	}
	*text = (const char*)s;
	/* reject overlong forms, surrogates and out-of-range values */
	if ((extra == 1 && cp < 0x80u) || (extra == 2 && cp < 0x800u) || (extra == 3 && cp < 0x10000u) ||
		(cp >= 0xD800u && cp <= 0xDFFFu) || cp > 0x10FFFFu) {
		return 0xFFFDu;
	}
	return cp;
}

int rvn_utf8_encode(uint32_t cp, char out[4]) {
	if (cp < 0x80u) {
		out[0] = (char)cp;
		return 1;
	}
	if (cp < 0x800u) {
		out[0] = (char)(0xC0u | (cp >> 6));
		out[1] = (char)(0x80u | (cp & 0x3Fu));
		return 2;
	}
	if (cp < 0x10000u) {
		out[0] = (char)(0xE0u | (cp >> 12));
		out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
		out[2] = (char)(0x80u | (cp & 0x3Fu));
		return 3;
	}
	out[0] = (char)(0xF0u | ((cp >> 18) & 0x07u));
	out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
	out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
	out[3] = (char)(0x80u | (cp & 0x3Fu));
	return 4;
}

int rvn_utf8_next(const char* text, int length, int offset) {
	if (offset >= length) return length;
	offset++;
	while (offset < length && ((unsigned char)text[offset] & 0xC0u) == 0x80u) offset++;
	return offset;
}

int rvn_utf8_prev(const char* text, int offset) {
	if (offset <= 0) return 0;
	offset--;
	while (offset > 0 && ((unsigned char)text[offset] & 0xC0u) == 0x80u) offset--;
	return offset;
}

int rvn_utf8_count(const char* text, int length) {
	int i, count = 0;
	for (i = 0; i < length; i++) {
		if (((unsigned char)text[i] & 0xC0u) != 0x80u) count++;
	}
	return count;
}
