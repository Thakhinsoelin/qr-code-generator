/**
 * Raven - platform/x11/x11_internal.h
 * Shared by the files of the X11 platform.
 */

#ifndef RVN_X11_INTERNAL_H
#define RVN_X11_INTERNAL_H

#include "../platform.h"

#include <X11/Xlib.h>

/** Maps an X11 key symbol to a Raven key, or R_KEY_UNKNOWN (x11_keys.c). */
RKey rvn_x11_translate_keysym(KeySym symbol);

/** Converts the modifier bits of an X11 event state (x11_keys.c). */
uint32_t rvn_x11_translate_modifiers(unsigned int state);

#endif /* RVN_X11_INTERNAL_H */
