#include "thread_pool.h"
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdatomic.h>
#include <malloc.h>

typedef enum {
	TINIT,
	TWAITING,
	TRUNNING,
	TFINISHED
} ThreadStatus_t;

struct thread_task {
	thread_task_f function;
	void *arg;

	void *result;
	struct thread_task *prev;
	struct thread_task *next;
	ThreadStatus_t status;
	pthread_mutex_t runMutex;
	pthread_mutex_t detachMutex;
	bool isDetached;
};

struct thread_pool {
	pthread_t *threads;

	atomic_int createdThreadCount;
	atomic_int runningThreadCount;
	int taskCount;
	int maxThreads;
	struct thread_task *current;
	bool exit;
};
 
static void *threadRunner(void *voidPool) {
	struct thread_pool *pool = voidPool;
	++pool->createdThreadCount;
	while (!pool->exit) {
		struct thread_task *tp = pool->current;
		while (tp) {
			if (tp->status == TWAITING && pthread_mutex_trylock(&tp->runMutex) == 0) {
				tp->status = TRUNNING;
				++pool->runningThreadCount;
				tp->result = tp->function(tp->arg);
				--pool->runningThreadCount;
				if (tp->next == NULL && tp->prev == NULL) {
					pool->current = NULL;
				} else {
					tp->prev->next = tp->next;
					tp->next = NULL;
					tp->prev = NULL;
				}
				pthread_mutex_lock(&tp->detachMutex);
				if (tp->isDetached) {
					thread_task_delete(tp);
				} else {
					tp->status = TFINISHED;
					pthread_mutex_unlock(&tp->runMutex);
					pthread_mutex_unlock(&tp->detachMutex);
				}
				
			}
			tp = tp->next;
		}
		usleep(10000);
	}
	--pool->createdThreadCount;
	return NULL;
}

int
thread_pool_new(int max_thread_count, struct thread_pool **pool)
{
	if (max_thread_count <= 0 || max_thread_count > TPOOL_MAX_THREADS) {
		return TPOOL_ERR_INVALID_ARGUMENT;
	}
	*pool = calloc(1, sizeof(struct thread_pool));
	(*pool)->maxThreads = max_thread_count;
	(*pool)->threads = malloc(max_thread_count * sizeof(pthread_t));
	return 0;
}

int
thread_pool_thread_count(const struct thread_pool *pool)
{
	return pool->createdThreadCount;
}

int
thread_pool_delete(struct thread_pool *pool)
{
	if (pool->current != NULL) {
		return TPOOL_ERR_HAS_TASKS;
	}
	pool->exit = true;
	while (pool->createdThreadCount) {
		usleep(10000);
	}
	free(pool->threads);
	free(pool);
	return 0;
}

int
thread_pool_push_task(struct thread_pool *pool, struct thread_task *task)
{
	if (pool->taskCount >= TPOOL_MAX_TASKS) {
		return TPOOL_ERR_TOO_MANY_TASKS;
	}
	struct thread_task *tp = pool->current;
	if (tp != NULL) {
		while (tp->next) {
			tp = tp->next;
		}
		tp->next = task;
		task->prev = tp;
	} else {
		pool->current = task;
		task->prev = NULL;
		task->next = NULL;
	}
	task->status = TWAITING;
	
	if (pool->createdThreadCount - pool->runningThreadCount == 0) {
		pthread_create(&pool->threads[pool->createdThreadCount], NULL, threadRunner, pool);
	}
	++pool->taskCount;
	return 0;
}

int
thread_task_new(struct thread_task **task, thread_task_f function, void *arg)
{
	*task = calloc(1, sizeof(struct thread_task));
	(*task)->function = function;
	(*task)->arg = arg;
	pthread_mutex_init(&(*task)->runMutex, NULL);
	pthread_mutex_init(&(*task)->detachMutex, NULL);
	return 0;
}

bool
thread_task_is_finished(const struct thread_task *task)
{
	return task->status == TFINISHED;
}

bool
thread_task_is_running(const struct thread_task *task)
{
	return task->status == TRUNNING;
}

int
thread_task_join(struct thread_task *task, void **result)
{
	if (task->status == TINIT) {
		return TPOOL_ERR_TASK_NOT_PUSHED;
	}
	while (task->status != TFINISHED) {
		usleep(10000);
	}
	*result = task->result;
	task->status = TINIT;
	return 0;
}

int
thread_task_delete(struct thread_task *task)
{
	if (task->status != TINIT) {
		return TPOOL_ERR_TASK_IN_POOL;
	}
	pthread_mutex_destroy(&task->runMutex);
	pthread_mutex_destroy(&task->detachMutex);
	free(task);
	return 0;
}

int
thread_task_detach(struct thread_task *task)
{
	if (task->status == TINIT) {
		return TPOOL_ERR_TASK_NOT_PUSHED;
	}
	pthread_mutex_lock(&task->detachMutex);
	if (task->status == TFINISHED) {
		thread_task_delete(task);
		return 0;
	}
	task->isDetached = true;
	pthread_mutex_unlock(&task->detachMutex);
	return 0;
}