/**
 * Raven - core/fs_watch.h
 * Native directory notifications. Callbacks are raised on a system worker
 * thread and must marshal UI work with Rtask_post.
 */
#ifndef RVN_FS_WATCH_H
#define RVN_FS_WATCH_H

#include "base.h"

typedef void (*rvn_fs_watch_callback_t)(const char* absolute_path, void* user);

int  rvn_fs_watch_start(rvn_fs_watch_callback_t callback, void* user);
int  rvn_fs_watch_add(const char* path);
void rvn_fs_watch_stop(void);

#endif /* RVN_FS_WATCH_H */
