/**
 * Raven - render/bidi.h
 * Right-to-left text: visual order and Arabic shapes (render/bidi.c).
 */

#ifndef RVN_BIDI_H
#define RVN_BIDI_H

#include "../core/base.h"

/** R_TRUE when the line holds right-to-left characters (or bidi controls): it must be reordered to be drawn. */
int rvn_bidi_needed(const char* text, int length);
/** R_TRUE when the first strong character of the text is right to left. */
int rvn_bidi_paragraph_rtl(const char* text, int length);
/** The line in the order it is drawn, Arabic shaped, as new UTF-8 (rvn_free). */
char* rvn_bidi_visual(const char* text, int length, int* out_length);

#endif
