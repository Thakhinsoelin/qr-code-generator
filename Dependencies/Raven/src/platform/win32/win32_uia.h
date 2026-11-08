/**
 * Raven - platform/win32/win32_uia.h
 * UI Automation for the screen readers of Windows (win32_uia.c).
 */

#ifndef RVN_WIN32_UIA_H
#define RVN_WIN32_UIA_H

#include <windows.h>
#include "../platform.h"

/** WM_GETOBJECT: R_TRUE with *result when the window answered with its provider. `user`: the window of Raven. */
int  rvn_uia_get_object(HWND hwnd, void* user, WPARAM wparam, LPARAM lparam, LRESULT* result);
/** A call of a screen reader sent to the UI thread: R_TRUE when the message was one. */
int  rvn_uia_handle_message(UINT message, WPARAM wparam, LPARAM lparam);
void rvn_uia_window_destroyed(HWND hwnd);
void rvn_uia_shutdown(void);

void rvn_uia_event(HWND hwnd, int event, uint64_t node);
void rvn_uia_announce(HWND hwnd, void* user, const char* text, int assertive);
int  rvn_uia_active(void);
void rvn_uia_settings(struct RAccessibilitySettings* settings);

#endif
