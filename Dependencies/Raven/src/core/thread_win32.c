/**
 * Raven - core/thread_win32.c
 * Win32 implementation of core/thread.h (SRW locks, condition variables).
 */

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <process.h>

#include "thread.h"

typedef struct rvn_thread_start_t {
	rvn_thread_fn function;
	void* arg;
} rvn_thread_start_t;

static unsigned __stdcall rvn_thread_entry(void* param) {
	rvn_thread_start_t start = *(rvn_thread_start_t*)param;
	rvn_free(param);
	start.function(start.arg);
	return 0;
}

int rvn_thread_create(rvn_thread_t* thread, rvn_thread_fn function, void* arg, const char* name) {
	rvn_thread_start_t* start = (rvn_thread_start_t*)rvn_calloc(sizeof(rvn_thread_start_t));
	uintptr_t handle;
	RVN_UNUSED(name);
	start->function = function;
	start->arg = arg;
	handle = _beginthreadex(NULL, 0, rvn_thread_entry, start, 0, NULL);
	if (!handle) {
		rvn_free(start);
		return R_FALSE;
	}
	thread->handle = (void*)handle;
	return R_TRUE;
}

void rvn_thread_join(rvn_thread_t* thread) {
	if (!thread->handle) return;
	WaitForSingleObject((HANDLE)thread->handle, INFINITE);
	CloseHandle((HANDLE)thread->handle);
	thread->handle = NULL;
}

uint64_t rvn_thread_id(void) {
	return (uint64_t)GetCurrentThreadId();
}

int rvn_cpu_count(void) {
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	return info.dwNumberOfProcessors > 0 ? (int)info.dwNumberOfProcessors : 1;
}

void rvn_sleep_ms(int milliseconds) {
	if (milliseconds > 0) Sleep((DWORD)milliseconds);
}

double rvn_clock_seconds(void) {
	static LARGE_INTEGER frequency;
	LARGE_INTEGER counter;
	if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&counter);
	return (double)counter.QuadPart / (double)frequency.QuadPart;
}

void rvn_mutex_init(rvn_mutex_t* mutex) { InitializeSRWLock((PSRWLOCK)&mutex->handle); }
void rvn_mutex_destroy(rvn_mutex_t* mutex) { RVN_UNUSED(mutex); }
void rvn_mutex_lock(rvn_mutex_t* mutex) { AcquireSRWLockExclusive((PSRWLOCK)&mutex->handle); }
void rvn_mutex_unlock(rvn_mutex_t* mutex) { ReleaseSRWLockExclusive((PSRWLOCK)&mutex->handle); }

void rvn_rwlock_init(rvn_rwlock_t* lock) { InitializeSRWLock((PSRWLOCK)&lock->handle); }
void rvn_rwlock_destroy(rvn_rwlock_t* lock) { RVN_UNUSED(lock); }
void rvn_rwlock_read_lock(rvn_rwlock_t* lock) { AcquireSRWLockShared((PSRWLOCK)&lock->handle); }
void rvn_rwlock_read_unlock(rvn_rwlock_t* lock) { ReleaseSRWLockShared((PSRWLOCK)&lock->handle); }
void rvn_rwlock_write_lock(rvn_rwlock_t* lock) { AcquireSRWLockExclusive((PSRWLOCK)&lock->handle); }
void rvn_rwlock_write_unlock(rvn_rwlock_t* lock) { ReleaseSRWLockExclusive((PSRWLOCK)&lock->handle); }

void rvn_cond_init(rvn_cond_t* cond) { InitializeConditionVariable((PCONDITION_VARIABLE)&cond->handle); }
void rvn_cond_destroy(rvn_cond_t* cond) { RVN_UNUSED(cond); }

void rvn_cond_wait(rvn_cond_t* cond, rvn_mutex_t* mutex) {
	SleepConditionVariableSRW((PCONDITION_VARIABLE)&cond->handle, (PSRWLOCK)&mutex->handle, INFINITE, 0);
}

int rvn_cond_wait_timeout(rvn_cond_t* cond, rvn_mutex_t* mutex, double seconds) {
	DWORD milliseconds = seconds <= 0.0 ? 0 : (DWORD)(seconds * 1000.0 + 0.5);
	return SleepConditionVariableSRW((PCONDITION_VARIABLE)&cond->handle, (PSRWLOCK)&mutex->handle, milliseconds, 0) != 0;
}

void rvn_cond_signal(rvn_cond_t* cond) { WakeConditionVariable((PCONDITION_VARIABLE)&cond->handle); }
void rvn_cond_broadcast(rvn_cond_t* cond) { WakeAllConditionVariable((PCONDITION_VARIABLE)&cond->handle); }

#else
typedef int rvn_thread_win32_unused_t;
#endif /* _WIN32 */
