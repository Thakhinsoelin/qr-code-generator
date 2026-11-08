/**
 * Raven - core/jobs.h
 * Job system: a fixed pool of worker threads fed by one shared queue.
 *
 * A thread waiting for a group of jobs executes queued jobs itself instead of
 * sleeping, so jobs may submit and wait for other jobs without deadlock and
 * the pool also works with zero workers (everything then runs in the waiter).
 */

#ifndef RVN_JOBS_H
#define RVN_JOBS_H

#include "thread.h"

typedef void (*rvn_job_fn)(void* arg);
typedef void (*rvn_job_range_fn)(uint32_t begin, uint32_t end, void* context);

/** Counts the unfinished jobs submitted with it. Zero-initialize before use. */
typedef struct rvn_job_group_t {
	rvn_atomic_t pending;
} rvn_job_group_t;

/** @param worker_count Number of threads to start; 0 runs every job in the caller. */
void rvn_jobs_init(int worker_count);
void rvn_jobs_shutdown(void);
int  rvn_jobs_worker_count(void);

/** Queues a job. `group` may be NULL for fire-and-forget jobs. Thread-safe. */
void rvn_jobs_submit(rvn_job_group_t* group, rvn_job_fn function, void* arg);

/** Returns when every job of the group is finished. Runs queued jobs meanwhile. */
void rvn_jobs_wait(rvn_job_group_t* group);

/**
 * Calls `function` for consecutive sub-ranges of [0, count), each holding at
 * most `grain` items, on the workers and on the calling thread.
 */
void rvn_jobs_parallel_for(uint32_t count, uint32_t grain, rvn_job_range_fn function, void* context);

#endif /* RVN_JOBS_H */
