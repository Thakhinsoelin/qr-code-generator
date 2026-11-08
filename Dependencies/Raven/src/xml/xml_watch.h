/**
 * Raven - xml/xml_watch.h
 * Files that are watched, and live reload of the interfaces built from files
 * (xml_watch.c).
 */

#ifndef RVN_XML_WATCH_H
#define RVN_XML_WATCH_H

#include "xml.h"

/**
 * Called when a watched file was written. R_FALSE asks to be called again a
 * moment later (the file was caught half written).
 */
typedef int (*rvn_watch_callback_t)(const char* path, void* user);

/** Watches a file. Adding the same file with the same callback and user twice does nothing. */
void rvn_watch_add(const char* path, rvn_watch_callback_t callback, void* user);

/** R_TRUE while a file that could not be used is tried again: its errors should not be reported yet. */
int rvn_watch_quiet(void);

/** Starts noting the files read by the document being loaded (<Include>). Calls nest. */
void rvn_live_begin(void);
void rvn_live_end(void);
void rvn_live_dependency(const char* path);
/**
 * Remembers what was built from a file: `target` is the window or the
 * widget, `document` the expanded document it was built from, which is taken
 * (the caller must not free it). To call between rvn_live_begin and rvn_live_end.
 */
void rvn_live_add(const char* path, int is_window, Rid target, rvn_xml_document_t* document);
/** Watches a file of <Widget> declarations (Rxml_register_widgets_from_file). */
void rvn_live_watch_widgets(const char* path);
void rvn_live_shutdown(void);

#endif /* RVN_XML_WATCH_H */
