/**
 * Raven - core/thread_posix.c
 * pthread implementation of core/thread.h (Linux, BSD, macOS).
 */

#if !defined(_WIN32)

#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* pthread_setname_np */
#endif

#include "thread.h"

#include <errno.h>
#include <time.h>
#include <unistd.h>

typedef struct rvn_thread_start_t {
	rvn_thread_fn function;
	void* arg;
	char name[16];
} rvn_thread_start_t;

static void* rvn_thread_entry(void* param) {
	rvn_thread_start_t start = *(rvn_thread_start_t*)param;
	rvn_free(param);
#if defined(__linux__)
	if (start.name[0]) pthread_setname_np(pthread_self(), start.name);
#elif defined(__APPLE__)
	if (start.name[0]) pthread_setname_np(start.name);
#endif
	start.function(start.arg);
	return NULL;
}

int rvn_thread_create(rvn_thread_t* thread, rvn_thread_fn function, void* arg, const char* name) {
	rvn_thread_start_t* start = (rvn_thread_start_t*)rvn_calloc(sizeof(rvn_thread_start_t));
	start->function = function;
	start->arg = arg;
	if (name) {
		strncpy(start->name, name, sizeof(start->name) - 1);
	}
	if (pthread_create(&thread->handle, NULL, rvn_thread_entry, start) != 0) {
		rvn_free(start);
		return R_FALSE;
	}
	return R_TRUE;
}

void rvn_thread_join(rvn_thread_t* thread) {
	pthread_join(thread->handle, NULL);
}

uint64_t rvn_thread_id(void) {
	/* the address of a thread-local object is unique per live thread */
	static _Thread_local char marker;
	return (uint64_t)(uintptr_t)&marker;
}

int rvn_cpu_count(void) {
	long count = sysconf(_SC_NPROCESSORS_ONLN);
	return count > 0 ? (int)count : 1;
}

void rvn_sleep_ms(int milliseconds) {
	struct timespec ts;
	if (milliseconds <= 0) return;
	ts.tv_sec = milliseconds / 1000;
	ts.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
	while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}

double rvn_clock_seconds(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

void rvn_mutex_init(rvn_mutex_t* mutex) { pthread_mutex_init(&mutex->handle, NULL); }
void rvn_mutex_destroy(rvn_mutex_t* mutex) { pthread_mutex_destroy(&mutex->handle); }
void rvn_mutex_lock(rvn_mutex_t* mutex) { pthread_mutex_lock(&mutex->handle); }
void rvn_mutex_unlock(rvn_mutex_t* mutex) { pthread_mutex_unlock(&mutex->handle); }

void rvn_rwlock_init(rvn_rwlock_t* lock) { pthread_rwlock_init(&lock->handle, NULL); }
void rvn_rwlock_destroy(rvn_rwlock_t* lock) { pthread_rwlock_destroy(&lock->handle); }
void rvn_rwlock_read_lock(rvn_rwlock_t* lock) { pthread_rwlock_rdlock(&lock->handle); }
void rvn_rwlock_read_unlock(rvn_rwlock_t* lock) { pthread_rwlock_unlock(&lock->handle); }
void rvn_rwlock_write_lock(rvn_rwlock_t* lock) { pthread_rwlock_wrlock(&lock->handle); }
void rvn_rwlock_write_unlock(rvn_rwlock_t* lock) { pthread_rwlock_unlock(&lock->handle); }

/* macOS has no pthread_condattr_setclock: timed waits use the realtime clock there */
#if defined(__APPLE__)
	#define RVN_COND_CLOCK CLOCK_REALTIME
#else
	#define RVN_COND_CLOCK CLOCK_MONOTONIC
#endif

void rvn_cond_init(rvn_cond_t* cond) {
#if defined(__APPLE__)
	pthread_cond_init(&cond->handle, NULL);
#else
	pthread_condattr_t attr;
	pthread_condattr_init(&attr);
	pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
	pthread_cond_init(&cond->handle, &attr);
	pthread_condattr_destroy(&attr);
#endif
}

void rvn_cond_destroy(rvn_cond_t* cond) { pthread_cond_destroy(&cond->handle); }
void rvn_cond_wait(rvn_cond_t* cond, rvn_mutex_t* mutex) { pthread_cond_wait(&cond->handle, &mutex->handle); }

int rvn_cond_wait_timeout(rvn_cond_t* cond, rvn_mutex_t* mutex, double seconds) {
	struct timespec ts;
	double whole;
	if (seconds < 0.0) seconds = 0.0;
	clock_gettime(RVN_COND_CLOCK, &ts);
	whole = floor(seconds);
	ts.tv_sec += (time_t)whole;
	ts.tv_nsec += (long)((seconds - whole) * 1e9);
	if (ts.tv_nsec >= 1000000000L) {
		ts.tv_sec += 1;
		ts.tv_nsec -= 1000000000L;
	}
	return pthread_cond_timedwait(&cond->handle, &mutex->handle, &ts) != ETIMEDOUT;
}

void rvn_cond_signal(rvn_cond_t* cond) { pthread_cond_signal(&cond->handle); }
void rvn_cond_broadcast(rvn_cond_t* cond) { pthread_cond_broadcast(&cond->handle); }

#else
typedef int rvn_thread_posix_unused_t;
#endif /* !_WIN32 */
