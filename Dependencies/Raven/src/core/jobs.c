/**
 * Raven - core/jobs.c
 * Worker pool with a mutex-protected ring queue.
 */

#include "jobs.h"

#define RVN_JOBS_MAX_WORKERS 64

typedef struct rvn_job_t {
	rvn_job_fn function;
	void* arg;
	rvn_job_group_t* group;
} rvn_job_t;

typedef struct rvn_jobs_state_t {
	rvn_mutex_t mutex;
	/** Signalled when a job is queued (workers sleep on it). */
	rvn_cond_t work_cv;
	/** Broadcast when a group completes or a job is queued (waiters sleep on it). */
	rvn_cond_t done_cv;
	rvn_job_t* queue;
	uint32_t queue_cap;
	uint32_t queue_head;
	uint32_t queue_count;
	uint32_t waiters;
	rvn_thread_t workers[RVN_JOBS_MAX_WORKERS];
	int worker_count;
	int stop;
	int initialized;
} rvn_jobs_state_t;

static rvn_jobs_state_t g_jobs;

/* Caller holds the mutex. */
static int rvn_jobs_pop(rvn_job_t* job) {
	if (g_jobs.queue_count == 0) return R_FALSE;
	*job = g_jobs.queue[g_jobs.queue_head];
	g_jobs.queue_head = (g_jobs.queue_head + 1) % g_jobs.queue_cap;
	g_jobs.queue_count--;
	return R_TRUE;
}

/* Caller does not hold the mutex. */
static void rvn_jobs_execute(const rvn_job_t* job) {
	job->function(job->arg);
	if (job->group && rvn_atomic_add(&job->group->pending, -1) == 0) {
		/* Taking the mutex orders this broadcast after the waiter's check of
		 * `pending`, so the wake-up cannot be lost. */
		rvn_mutex_lock(&g_jobs.mutex);
		rvn_cond_broadcast(&g_jobs.done_cv);
		rvn_mutex_unlock(&g_jobs.mutex);
	}
}

static void rvn_jobs_worker(void* arg) {
	RVN_UNUSED(arg);
	rvn_mutex_lock(&g_jobs.mutex);
	for (;;) {
		rvn_job_t job;
		if (rvn_jobs_pop(&job)) {
			rvn_mutex_unlock(&g_jobs.mutex);
			rvn_jobs_execute(&job);
			rvn_mutex_lock(&g_jobs.mutex);
			continue;
		}
		if (g_jobs.stop) break;
		rvn_cond_wait(&g_jobs.work_cv, &g_jobs.mutex);
	}
	rvn_mutex_unlock(&g_jobs.mutex);
}

void rvn_jobs_init(int worker_count) {
	int i;
	memset(&g_jobs, 0, sizeof(g_jobs));
	rvn_mutex_init(&g_jobs.mutex);
	rvn_cond_init(&g_jobs.work_cv);
	rvn_cond_init(&g_jobs.done_cv);
	g_jobs.queue_cap = 256;
	g_jobs.queue = (rvn_job_t*)rvn_alloc(sizeof(rvn_job_t) * g_jobs.queue_cap);
	g_jobs.initialized = R_TRUE;
	worker_count = rvn_clampi(worker_count, 0, RVN_JOBS_MAX_WORKERS);
	for (i = 0; i < worker_count; i++) {
		char name[32];
		snprintf(name, sizeof(name), "raven-job-%d", i);
		if (!rvn_thread_create(&g_jobs.workers[g_jobs.worker_count], rvn_jobs_worker, NULL, name)) break;
		g_jobs.worker_count++;
	}
}

void rvn_jobs_shutdown(void) {
	int i;
	rvn_job_t job;
	if (!g_jobs.initialized) return;
	rvn_mutex_lock(&g_jobs.mutex);
	g_jobs.stop = R_TRUE;
	rvn_cond_broadcast(&g_jobs.work_cv);
	rvn_mutex_unlock(&g_jobs.mutex);
	for (i = 0; i < g_jobs.worker_count; i++) rvn_thread_join(&g_jobs.workers[i]);
	/* workers drain the queue before leaving; with zero workers, run what is left */
	rvn_mutex_lock(&g_jobs.mutex);
	while (rvn_jobs_pop(&job)) {
		rvn_mutex_unlock(&g_jobs.mutex);
		rvn_jobs_execute(&job);
		rvn_mutex_lock(&g_jobs.mutex);
	}
	rvn_mutex_unlock(&g_jobs.mutex);
	rvn_free(g_jobs.queue);
	rvn_cond_destroy(&g_jobs.done_cv);
	rvn_cond_destroy(&g_jobs.work_cv);
	rvn_mutex_destroy(&g_jobs.mutex);
	memset(&g_jobs, 0, sizeof(g_jobs));
}

int rvn_jobs_worker_count(void) {
	return g_jobs.worker_count;
}

void rvn_jobs_submit(rvn_job_group_t* group, rvn_job_fn function, void* arg) {
	rvn_job_t job;
	job.function = function;
	job.arg = arg;
	job.group = group;
	if (group) rvn_atomic_add(&group->pending, 1);

	if (!g_jobs.initialized || (g_jobs.worker_count == 0 && !group)) {
		/* nobody will ever pick up an unowned job: run it now */
		rvn_jobs_execute(&job);
		return;
	}

	rvn_mutex_lock(&g_jobs.mutex);
	if (g_jobs.queue_count == g_jobs.queue_cap) {
		/* grow the ring, unwrapping it to the start of the new storage */
		uint32_t new_cap = g_jobs.queue_cap * 2, i;
		rvn_job_t* queue = (rvn_job_t*)rvn_alloc(sizeof(rvn_job_t) * new_cap);
		for (i = 0; i < g_jobs.queue_count; i++) {
			queue[i] = g_jobs.queue[(g_jobs.queue_head + i) % g_jobs.queue_cap];
		}
		rvn_free(g_jobs.queue);
		g_jobs.queue = queue;
		g_jobs.queue_cap = new_cap;
		g_jobs.queue_head = 0;
	}
	g_jobs.queue[(g_jobs.queue_head + g_jobs.queue_count) % g_jobs.queue_cap] = job;
	g_jobs.queue_count++;
	rvn_cond_signal(&g_jobs.work_cv);
	if (g_jobs.waiters) rvn_cond_broadcast(&g_jobs.done_cv);
	rvn_mutex_unlock(&g_jobs.mutex);
}

void rvn_jobs_wait(rvn_job_group_t* group) {
	if (!g_jobs.initialized) return;
	rvn_mutex_lock(&g_jobs.mutex);
	while (rvn_atomic_load(&group->pending) > 0) {
		rvn_job_t job;
		if (rvn_jobs_pop(&job)) {
			rvn_mutex_unlock(&g_jobs.mutex);
			rvn_jobs_execute(&job);
			rvn_mutex_lock(&g_jobs.mutex);
		} else {
			g_jobs.waiters++;
			rvn_cond_wait(&g_jobs.done_cv, &g_jobs.mutex);
			g_jobs.waiters--;
		}
	}
	rvn_mutex_unlock(&g_jobs.mutex);
}

/* ---------------------------------------------------------------------------
 *  parallel_for
 * ------------------------------------------------------------------------- */

typedef struct rvn_parallel_t {
	rvn_atomic_t next;
	uint32_t count;
	uint32_t grain;
	rvn_job_range_fn function;
	void* context;
} rvn_parallel_t;

static void rvn_parallel_run(void* arg) {
	rvn_parallel_t* parallel = (rvn_parallel_t*)arg;
	for (;;) {
		uint32_t end = (uint32_t)rvn_atomic_add(&parallel->next, (int)parallel->grain);
		uint32_t begin = end - parallel->grain;
		if (begin >= parallel->count) break;
		if (end > parallel->count) end = parallel->count;
		parallel->function(begin, end, parallel->context);
	}
}

void rvn_jobs_parallel_for(uint32_t count, uint32_t grain, rvn_job_range_fn function, void* context) {
	rvn_parallel_t parallel;
	rvn_job_group_t group;
	uint32_t chunks, helpers, i;

	if (count == 0) return;
	if (grain == 0) grain = 1;
	if (grain > count) grain = count;
	chunks = (count + grain - 1) / grain;
	helpers = rvn_minu(chunks - 1, (uint32_t)g_jobs.worker_count);
	if (helpers == 0 || count > 0x3FFFFFFFu) {
		function(0, count, context);
		return;
	}

	memset(&parallel, 0, sizeof(parallel));
	memset(&group, 0, sizeof(group));
	parallel.count = count;
	parallel.grain = grain;
	parallel.function = function;
	parallel.context = context;
	for (i = 0; i < helpers; i++) rvn_jobs_submit(&group, rvn_parallel_run, &parallel);
	rvn_parallel_run(&parallel);
	rvn_jobs_wait(&group);
}
