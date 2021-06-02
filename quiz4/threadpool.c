#include <stddef.h>
#include <pthread.h>

typedef struct __tpool_future *tpool_future_t;
typedef struct __threadpool *tpool_t;
typedef struct __jobqueue jobqueue_t;

/**
 * Create a thread pool containing specified number of threads.
 * If successful, the thread pool is returned. Otherwise, it
 * returns NULL.
 */
tpool_t tpool_create(size_t count);

/**
 * Schedules the specific function to be executed.
 * If successful, a future object representing the execution of
 * the task is returned. Otherwise, it returns NULL.
 */
tpool_future_t tpool_apply(tpool_t pool, void *(*func)(void *), void *arg);

/**
 * Wait for all pending tasks to complete before destroying the thread pool.
 */
int tpool_join(tpool_t pool);

/**
 * Return the result when it becomes available.
 * If @seconds is non-zero and the result does not arrive within specified time,
 * NULL is returned. Each tpool_future_get() resets the timeout status on
 * @future.
 */
void *tpool_future_get(tpool_future_t future, unsigned int milliseconds,
                       jobqueue_t *jobqueue);

/**
 * Destroy the future object and free resources once it is no longer used.
 * It is an error to refer to a destroyed future object. Note that destroying
 * a future object does not prevent a pending task from being executed.
 */
int tpool_future_destroy(tpool_future_t future);

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

enum __future_flags {
  __FUTURE_RUNNING = 01,
  __FUTURE_FINISHED = 02,
  __FUTURE_TIMEOUT = 04,
  __FUTURE_CANCELLED = 010,
  __FUTURE_DESTROYED = 020,
};

typedef struct __threadtask {
  void *(*func)(void *);
  void *arg;
  struct __tpool_future *future;
  struct __threadtask *next;
} threadtask_t;

typedef struct __jobqueue {
  threadtask_t *head, *tail;
  pthread_cond_t cond_nonempty;
  pthread_mutex_t rwlock;
} jobqueue_t;

struct __tpool_future {
  int flag;
  void *result;
  pthread_mutex_t mutex;
  pthread_cond_t cond_finished;
};

struct __threadpool {
  size_t count;
  pthread_t *workers;
  jobqueue_t *jobqueue;
};

static struct __tpool_future *tpool_future_create(void) {
  struct __tpool_future *future = malloc(sizeof(struct __tpool_future));
  if (future) {
    future->flag = 0;
    future->result = NULL;
    pthread_mutex_init(&future->mutex, NULL);
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&future->cond_finished, &attr);
    pthread_condattr_destroy(&attr);
  }
  return future;
}

int tpool_future_destroy(struct __tpool_future *future) {
  if (future) {
    pthread_mutex_lock(&future->mutex);
    if (future->flag & __FUTURE_FINISHED || future->flag & __FUTURE_CANCELLED) {
      pthread_mutex_unlock(&future->mutex);
      pthread_mutex_destroy(&future->mutex);
      pthread_cond_destroy(&future->cond_finished);
      free(future);
    } else {
      future->flag |= __FUTURE_DESTROYED;
      pthread_mutex_unlock(&future->mutex);
    }
  }
  return 0;
}

void *tpool_future_get(struct __tpool_future *future, unsigned int milliseconds,
                       jobqueue_t *jobqueue) {
  pthread_mutex_lock(&future->mutex);
  /* turn off the timeout bit set previously */
  future->flag &= ~__FUTURE_TIMEOUT;
  while ((future->flag & __FUTURE_FINISHED) == 0) {
    if (milliseconds) {
#define NANOSECOND 1000000000UL
      struct timespec expire_time;
      clock_gettime(CLOCK_MONOTONIC, &expire_time);
      expire_time.tv_nsec += (milliseconds % 1000) * NANOSECOND / 1000;
      if (expire_time.tv_nsec/NANOSECOND){
        expire_time.tv_nsec %= 1000000000;
        ++expire_time.tv_sec;
      }
      expire_time.tv_sec += milliseconds/1000;
#undef NANOSECOND

      int status = pthread_cond_timedwait(&future->cond_finished,
                                          &future->mutex, &expire_time);

      if (status == ETIMEDOUT) {
        future->flag |= __FUTURE_TIMEOUT;
        if (future->flag & __FUTURE_RUNNING)
          goto wait;

        /* find the corresponding task from job queue */
        pthread_mutex_lock(&jobqueue->rwlock);
        threadtask_t *task = jobqueue->head, *prev_task = NULL;
        while (task) {
          if (task->future == future)
            break;
          prev_task = task;
          task = task->next;
        }
        if (!task) {
          /* maybe it will fail */
          pthread_mutex_unlock(&jobqueue->rwlock);
          goto wait;
        }

        /* remove the task and the future */
        if (!prev_task)
          jobqueue->head = task->next; // head
        else
          prev_task->next = task->next;
        if (!task->next)
          jobqueue->tail = prev_task; // tail

        pthread_mutex_unlock(&future->mutex);
        pthread_mutex_destroy(&task->future->mutex);
        pthread_cond_destroy(&task->future->cond_finished);
        free(task->future);
        free(task);
        pthread_mutex_unlock(&jobqueue->rwlock);
        return NULL;
      }
    } else {
    /* wait until the task is completed */
    wait:
      pthread_cond_wait(&future->cond_finished, &future->mutex); // FFF;
    }
  }
  pthread_mutex_unlock(&future->mutex);
  return future->result;
}

static jobqueue_t *jobqueue_create(void) {
  jobqueue_t *jobqueue = malloc(sizeof(jobqueue_t));
  if (jobqueue) {
    jobqueue->head = jobqueue->tail = NULL;
    pthread_cond_init(&jobqueue->cond_nonempty, NULL);
    pthread_mutex_init(&jobqueue->rwlock, NULL);
  }
  return jobqueue;
}

static void jobqueue_destroy(jobqueue_t *jobqueue) {
  pthread_mutex_destroy(&jobqueue->rwlock);
  pthread_cond_destroy(&jobqueue->cond_nonempty);
  free(jobqueue);
}

static void __jobqueue_fetch_cleanup(void *arg) {
  pthread_mutex_t *mutex = (pthread_mutex_t *)arg;
  pthread_mutex_unlock(mutex);
}

static void *jobqueue_fetch(void *queue) {
  jobqueue_t *jobqueue = (jobqueue_t *)queue;
  threadtask_t *task;
  int old_state;

  pthread_cleanup_push(__jobqueue_fetch_cleanup, (void *)&jobqueue->rwlock);

  while (1) {
    pthread_mutex_lock(&jobqueue->rwlock);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &old_state);
    pthread_testcancel();

    while (!jobqueue->tail) pthread_cond_wait(&jobqueue->cond_nonempty, &jobqueue->rwlock); // GGG
    
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_state);
    if (jobqueue->head == jobqueue->tail) {
      task = jobqueue->tail;
      jobqueue->head = jobqueue->tail = NULL;
    } else {
      threadtask_t *tmp;
      for (tmp = jobqueue->head; tmp->next != jobqueue->tail; tmp = tmp->next)
        ;
      task = tmp->next;
      tmp->next = NULL;
      jobqueue->tail = tmp;
    }
    pthread_mutex_unlock(&jobqueue->rwlock);

    if (task->func) {
      pthread_mutex_lock(&task->future->mutex);
      if (task->future->flag & __FUTURE_CANCELLED) {
        pthread_mutex_unlock(&task->future->mutex);
        free(task);
        continue;
      } else {
        task->future->flag |= __FUTURE_RUNNING;
        pthread_mutex_unlock(&task->future->mutex);
      }

      void *ret_value = task->func(task->arg);
      pthread_mutex_lock(&task->future->mutex);
      if (task->future->flag & __FUTURE_DESTROYED) {
        pthread_mutex_unlock(&task->future->mutex);
        pthread_mutex_destroy(&task->future->mutex);
        pthread_cond_destroy(&task->future->cond_finished);
        free(task->future);
      } else {
        task->future->flag |= __FUTURE_FINISHED; // KKK
        task->future->result = ret_value;
        pthread_cond_broadcast(&task->future->cond_finished); // LLL;
        pthread_mutex_unlock(&task->future->mutex);
      }
      free(task);
    } else {
      pthread_mutex_destroy(&task->future->mutex);
      pthread_cond_destroy(&task->future->cond_finished);
      free(task->future);
      free(task);
      break;
    }
  }

  pthread_cleanup_pop(0);
  pthread_exit(NULL);
}

struct __threadpool *tpool_create(size_t count) {
  jobqueue_t *jobqueue = jobqueue_create();
  struct __threadpool *pool = malloc(sizeof(struct __threadpool));
  if (!jobqueue || !pool) {
    if (jobqueue)
      jobqueue_destroy(jobqueue);
    free(pool);
    return NULL;
  }

  pool->count = count, pool->jobqueue = jobqueue;
  if ((pool->workers = malloc(count * sizeof(pthread_t)))) {
    for (int i = 0; i < count; i++) {
      if (pthread_create(&pool->workers[i], NULL, jobqueue_fetch,
                         (void *)jobqueue)) {
        for (int j = 0; j < i; j++)
          pthread_cancel(pool->workers[j]);
        for (int j = 0; j < i; j++)
          pthread_join(pool->workers[j], NULL);
        free(pool->workers);
        jobqueue_destroy(jobqueue);
        free(pool);
        return NULL;
      }
    }
    return pool;
  }

  jobqueue_destroy(jobqueue);
  free(pool);
  return NULL;
}

struct __tpool_future *tpool_apply(struct __threadpool *pool,
                                   void *(*func)(void *), void *arg) {
  jobqueue_t *jobqueue = pool->jobqueue;
  threadtask_t *new_head = malloc(sizeof(threadtask_t));
  struct __tpool_future *future = tpool_future_create();
  if (new_head && future) {
    new_head->func = func, new_head->arg = arg, new_head->future = future;
    pthread_mutex_lock(&jobqueue->rwlock);
    if (jobqueue->head) {
      new_head->next = jobqueue->head;
      jobqueue->head = new_head;
    } else {
      jobqueue->head = jobqueue->tail = new_head;
      pthread_cond_broadcast(&jobqueue->cond_nonempty); // HHH
    }
    pthread_mutex_unlock(&jobqueue->rwlock);
  } else if (new_head) {
    free(new_head);
    return NULL;
  } else if (future) {
    tpool_future_destroy(future);
    return NULL;
  }
  return future;
}

int tpool_join(struct __threadpool *pool) {
  size_t num_threads = pool->count;
  for (int i = 0; i < num_threads; i++)
    tpool_apply(pool, NULL, NULL);
  for (int i = 0; i < num_threads; i++)
    pthread_join(pool->workers[i], NULL);
  free(pool->workers);
  jobqueue_destroy(pool->jobqueue);
  free(pool);
  return 0;
}
