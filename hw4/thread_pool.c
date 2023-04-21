#include "thread_pool.h"
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

typedef enum {
	TINIT,
	TWAITING,
	TRUNNING,
	TFINISHED,
} ThreadStatus_t;

struct thread_task {
	thread_task_f function;
	void *arg;

	void *result;
	struct thread_task *next;
	struct thread_task *prev;
	ThreadStatus_t status;
	pthread_mutex_t detachMutex;
	pthread_mutex_t resultMutex;
	pthread_cond_t resultCond;
	bool isDetached;
};

struct thread_pool {
	pthread_t *threads;

	atomic_int createdThreadCount;
	atomic_int runningThreadCount;
	atomic_int taskCount;
	int maxThreads;
	struct thread_task *current; // head
	struct thread_task *last; // tail
	pthread_mutex_t currentMutex;
	pthread_cond_t currentCond;
	bool exit;
};

static void *threadRunner(void *voidPool) {
	struct thread_pool *pool = voidPool;
	while (true) {
		pthread_mutex_lock(&pool->currentMutex);
		if (pool->current == NULL) {
			struct timespec t = {.tv_nsec = 100000}; 
			pthread_cond_timedwait(&pool->currentCond, &pool->currentMutex, &t);
			if (pool->exit) {
				pthread_mutex_unlock(&pool->currentMutex);
				return NULL;
			}
		}
		if (pool->current == NULL) {
			pthread_mutex_unlock(&pool->currentMutex);
			continue;
		}
		if (pool->current->status == TINIT) {
			if (pool->last != NULL && pool->current == pool->last) {
				pool->last = pool->last->prev;
			}
			pool->current->prev = NULL;
			pool->current->next = NULL;
			pool->current = pool->current->next;
			pthread_mutex_unlock(&pool->currentMutex);
			continue;
		}
		if (pool->current->status != TWAITING) {
			pthread_mutex_unlock(&pool->currentMutex);
			continue;
		}
		struct thread_task *tp = pool->current;
		if (pool->last != NULL && pool->current == pool->last) {
			pool->last = pool->last->prev;
		}
		pool->current = pool->current->next;
		++pool->runningThreadCount;
		pthread_mutex_unlock(&pool->currentMutex);
		pthread_mutex_lock(&tp->resultMutex);
		tp->result = tp->function(tp->arg);
		--pool->taskCount;
		--pool->runningThreadCount;
		if (pthread_mutex_trylock(&tp->detachMutex) == 0) {
			if (tp->isDetached) {
				pthread_mutex_unlock(&tp->detachMutex);
				pthread_mutex_unlock(&tp->resultMutex);
				thread_task_delete(tp);
				continue;
			} else {
				pthread_mutex_unlock(&tp->detachMutex);
			}
		}
		tp->status = TFINISHED;
		pthread_cond_signal(&tp->resultCond);
		pthread_mutex_unlock(&tp->resultMutex);
	}
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
	(*pool)->threads = calloc(max_thread_count, sizeof(pthread_t));
	pthread_mutex_init(&(*pool)->currentMutex, NULL);
	pthread_cond_init(&(*pool)->currentCond, NULL);
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
	if (pool->taskCount != 0) {
		return TPOOL_ERR_HAS_TASKS;
	}
	pthread_mutex_lock(&pool->currentMutex);
	pool->exit = true;
	pool->current = NULL;
	pool->last = NULL;
	pthread_mutex_unlock(&pool->currentMutex);
	pthread_cond_broadcast(&pool->currentCond);
	ssize_t tCount = pool->createdThreadCount;
	for (size_t i = 0; i < pool->createdThreadCount; ++i) {
		pthread_join(pool->threads[i], NULL);
	}
	free(pool->threads);
	pthread_cond_destroy(&pool->currentCond);
	pthread_mutex_destroy(&pool->currentMutex);
	free(pool);
	return 0;
}

int
thread_pool_push_task(struct thread_pool *pool, struct thread_task *task)
{
	if (pool->taskCount >= TPOOL_MAX_TASKS) {
		return TPOOL_ERR_TOO_MANY_TASKS;
	}
	if (pool->createdThreadCount < pool->maxThreads && pool->createdThreadCount - pool->runningThreadCount == 0) {
		if (pthread_create(&pool->threads[pool->createdThreadCount], NULL, threadRunner, pool) == 0) {
			pool->createdThreadCount++;
		}
	}
	pthread_mutex_lock(&pool->currentMutex);
	pthread_mutex_lock(&task->resultMutex);
	task->status = TWAITING;
	struct thread_task *tp = pool->current;
	if (tp != NULL) {
		if (pool->last != NULL) {
			pool->last->next = task;
		}
		task->prev = pool->last;
		pool->last = task;
	} else {
		pool->current = task;
		pool->last = task;
		task->next = NULL;
		task->prev = NULL;
	}
	pthread_cond_signal(&pool->currentCond);
	pthread_mutex_unlock(&pool->currentMutex);
	pthread_mutex_unlock(&task->resultMutex);
	
	++pool->taskCount;
	return 0;
}

int
thread_task_new(struct thread_task **task, thread_task_f function, void *arg)
{
	*task = calloc(1, sizeof(struct thread_task));
	(*task)->function = function;
	(*task)->arg = arg;
	pthread_mutex_init(&(*task)->detachMutex, NULL);
	pthread_mutex_init(&(*task)->resultMutex, NULL);
	pthread_cond_init(&(*task)->resultCond, NULL);
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
	pthread_mutex_lock(&task->resultMutex);
	if (task->status == TINIT) {
		pthread_mutex_unlock(&task->resultMutex);
		return TPOOL_ERR_TASK_NOT_PUSHED;
	}
	if (task->status != TFINISHED) {
		pthread_cond_wait(&task->resultCond, &task->resultMutex);
		*result = task->result;
	} else {
		*result = task->result;
	}
	task->status = TINIT;
	task->next = NULL;
	task->prev = NULL;
	pthread_mutex_unlock(&task->resultMutex);
	return 0;
}

int
thread_task_delete(struct thread_task *task)
{
	pthread_mutex_lock(&task->resultMutex);
	if (task->status != TINIT) {
		pthread_mutex_unlock(&task->resultMutex);
		return TPOOL_ERR_TASK_IN_POOL;
	}
	pthread_mutex_destroy(&task->detachMutex);
	pthread_cond_broadcast(&task->resultCond);
	pthread_cond_destroy(&task->resultCond);
	pthread_mutex_unlock(&task->resultMutex);
	pthread_mutex_destroy(&task->resultMutex);
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
		pthread_mutex_unlock(&task->detachMutex);
		thread_task_delete(task);
		return 0;
	}
	task->isDetached = true;
	pthread_mutex_unlock(&task->detachMutex);
	return 0;
}