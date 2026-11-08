/**
 * Raven - platform/atspi.h
 * AT-SPI 2 for the screen readers of Linux (platform/atspi.c), used by the
 * X11 platform (and by any Unix platform that wants it).
 */

#ifndef RVN_ATSPI_H
#define RVN_ATSPI_H

#include "platform.h"

/** Connects to the accessibility bus when assistive technologies are on. R_FALSE otherwise (nothing to do). */
int  rvn_atspi_init(void);
void rvn_atspi_shutdown(void);
/** The socket to wait on with the window system (-1 when not connected). */
int  rvn_atspi_fd(void);
/** Answers the requests that came in. */
void rvn_atspi_dispatch(void);

/* the accessibility entries of rvn_platform_t */
void rvn_atspi_event(rvn_pwindow_t* window, int event, uint64_t node);
void rvn_atspi_announce(rvn_pwindow_t* window, const char* text, int assertive);
int  rvn_atspi_active(void);
void rvn_atspi_settings(struct RAccessibilitySettings* settings);

#endif
