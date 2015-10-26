/*-
 * Copyright (c) 2011, Joakim Johansson <jocke@tbricks.com>
 * Copyright (c) 2010, Mark Heily <mark@heily.com>
 * Copyright (c) 2009, Stacey Son <sson@freebsd.org>
 * Copyright (c) 2000-2008, Apple Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "workqueue_internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>

unsigned int PWQ_ACTIVE_CPU = 0;
int DEBUG_WORKQUEUE = 0;
char *WORKQUEUE_DEBUG_IDENT = "WQ";

static int
valid_workq(pthread_workqueue_t workq) 
{
    if (workq->sig == PTHREAD_WORKQUEUE_SIG)
        return (1);
    else
        return (0);
}

static void
_pthread_workqueue_init2(void)
{
    DEBUG_WORKQUEUE = (getenv("PWQ_DEBUG") == NULL) ? 0 : 1;

    PWQ_RT_THREADS = (getenv("PWQ_RT_THREADS") == NULL) ? 0 : 1;
    PWQ_ACTIVE_CPU = (getenv("PWQ_ACTIVE_CPU") == NULL) ? 0 : atoi(getenv("PWQ_ACTIVE_CPU"));
    
    if (getenv("PWQ_SPIN_THREADS") != NULL)
        PWQ_SPIN_THREADS =  atoi(getenv("PWQ_SPIN_THREADS"));

    if (manager_init() < 0) {
        fprintf(stderr, "FATAL: pthread_workqueue failed to initialise");
        abort();
    }

    dbg_puts("pthread_workqueue library initialized");
}

int
pthread_workqueue_init_np(void)
{
    static pthread_once_t pred = PTHREAD_ONCE_INIT;
    pthread_once(&pred, _pthread_workqueue_init2);
    return (0);
}

int
pthread_workqueue_create_np(pthread_workqueue_t *workqp,
                            const pthread_workqueue_attr_t * attr)
{
    (void)pthread_workqueue_init_np();

    pthread_workqueue_t workq;

    if ((attr != NULL) && ((attr->sig != PTHREAD_WORKQUEUE_ATTR_SIG) ||
         (attr->queueprio < 0) || (attr->queueprio >= WORKQ_NUM_PRIOQUEUE)))
        return (EINVAL);
    if ((workq = calloc(1, sizeof(*workq))) == NULL)
        return (ENOMEM);
    workq->sig = PTHREAD_WORKQUEUE_SIG;
    workq->flags = 0;
    STAILQ_INIT(&workq->item_listhead);
    pthread_spin_init(&workq->mtx, PTHREAD_PROCESS_PRIVATE);
    if (attr == NULL) {
        workq->queueprio = WORKQ_DEFAULT_PRIOQUEUE;
        workq->overcommit = 0;
    } else {
        workq->queueprio = attr->queueprio;
        workq->overcommit = attr->overcommit;
    }

    manager_workqueue_create(workq);

    dbg_printf("created queue %p", (void *) workq);

    *workqp = workq;
    return (0);
}

int
pthread_workqueue_additem_np(pthread_workqueue_t workq,
                     void (*workitem_func)(void *), void * workitem_arg,
                     pthread_workitem_handle_t * itemhandlep, 
                     unsigned int *gencountp)
{
    struct work *witem;
    
    if ((valid_workq(workq) == 0) || (workitem_func == NULL))
        return (EINVAL);

    witem = witem_alloc(workitem_func, workitem_arg);

    if (itemhandlep != NULL)
        *itemhandlep = (pthread_workitem_handle_t *) witem;
    if (gencountp != NULL)
        *gencountp = witem->gencount;

    manager_workqueue_additem(workq, witem);

    dbg_printf("added item %p to queue %p", (void *) witem, (void *) workq);

    return (0);
}

int
pthread_workqueue_attr_init_np(pthread_workqueue_attr_t *attr)
{
    attr->queueprio = WORKQ_DEFAULT_PRIOQUEUE;
    attr->sig = PTHREAD_WORKQUEUE_ATTR_SIG;
    attr->overcommit = 0;
    return (0);
}

int
pthread_workqueue_attr_destroy_np(pthread_workqueue_attr_t *attr)
{
    if (attr->sig == PTHREAD_WORKQUEUE_ATTR_SIG)
        return (0);
    else
        return (EINVAL); /* Not an attribute struct. */
}

int
pthread_workqueue_attr_getovercommit_np(
        const pthread_workqueue_attr_t *attr, int *ocommp)
{
    if (attr->sig == PTHREAD_WORKQUEUE_ATTR_SIG) {
        *ocommp = attr->overcommit;
        return (0);
    } else 
        return (EINVAL); /* Not an attribute struct. */
}

int
pthread_workqueue_attr_setovercommit_np(pthread_workqueue_attr_t *attr,
                           int ocomm)
{
    if (attr->sig == PTHREAD_WORKQUEUE_ATTR_SIG) {
        attr->overcommit = ocomm;
        return (0);
    } else
        return (EINVAL);
}

int
pthread_workqueue_attr_getqueuepriority_np(
        pthread_workqueue_attr_t *attr, int *qpriop)
{
    if (attr->sig == PTHREAD_WORKQUEUE_ATTR_SIG) {
        *qpriop = attr->queueprio;
        return (0);
    } else 
        return (EINVAL);
}

int
pthread_workqueue_attr_setqueuepriority_np(
        pthread_workqueue_attr_t *attr, int qprio)
{
    if (attr->sig == PTHREAD_WORKQUEUE_ATTR_SIG) {
        switch(qprio) {
            case WORKQ_HIGH_PRIOQUEUE:
            case WORKQ_DEFAULT_PRIOQUEUE:
            case WORKQ_LOW_PRIOQUEUE:
            case WORKQ_BG_PRIOQUEUE:
                attr->queueprio = qprio;
                return (0);
            default:
                return (EINVAL);
        }
    } else
        return (EINVAL);
}

unsigned long
pthread_workqueue_peek_np(const char *key)
{
    return manager_peek(key);
}

void
pthread_workqueue_suspend_np(void)
{
    manager_suspend();
}

void
pthread_workqueue_resume_np(void)
{
    manager_resume();
}

/* no witem cache */

int
witem_cache_init(void)
{
   return (0);
}

struct work *
witem_alloc(void (*func)(void *), void *func_arg)
{
	struct work *witem;
    
	while (!(witem = fastpath(malloc(ROUND_UP_TO_CACHELINE_SIZE(sizeof(*witem)))))) {
		sleep(1);
	}

    witem->gencount = 0;
    witem->flags = 0;
    witem->item_entry.stqe_next = 0;
    witem->func = func;
    witem->func_arg = func_arg;

	return witem;
}

void 
witem_free(struct work *wi)
{
    dbg_printf("freed work item %p", wi);
    free(wi);
}

void
witem_cache_cleanup(void *value)
{
    (void) value;
}

/* libumem based object cache */

/* Environment setting */
unsigned int PWQ_RT_THREADS = 0;
unsigned int PWQ_SPIN_THREADS = 0; // The number of threads that should be kept spinning
unsigned volatile int current_threads_spinning = 0; // The number of threads currently spinning

/* Tunable constants */

#define WORKER_IDLE_SECONDS_THRESHOLD 15

/* Function prototypes */
static unsigned int get_runqueue_length(void);
static void * worker_main(void *arg);
static void * overcommit_worker_main(void *arg);
static unsigned int get_process_limit(void);
static void manager_start(void);

static unsigned int      cpu_count;
static unsigned int      worker_min;
static unsigned int      worker_idle_threshold; // we don't go down below this if we had to increase # workers
static unsigned int      pending_thread_create;

/* Overcommit */
static struct _pthread_workqueue *ocwq[PTHREAD_WORKQUEUE_MAX];
static int               ocwq_mask;
static pthread_mutex_t   ocwq_mtx;
static pthread_cond_t    ocwq_has_work;
static unsigned int      ocwq_idle_threads;
static unsigned int      ocwq_signal_count;     // number of times OCWQ condition was signalled

/* Non-overcommit */
static struct _pthread_workqueue *wqlist[PTHREAD_WORKQUEUE_MAX];
static volatile unsigned int     wqlist_mask; // mask of currently pending workqueues, atomics used for manipulation
static pthread_mutex_t   wqlist_mtx;

static pthread_cond_t    wqlist_has_work;
static int               wqlist_has_manager;
static pthread_attr_t    detached_attr;

static struct {
    volatile unsigned int runqueue_length,
                    count,
                    idle;
    sem_t sb_sem;
    unsigned int sb_suspend;
} scoreboard;

/* Thread limits */
#define DEFAULT_PROCESS_LIMIT 100

static unsigned int 
worker_idle_threshold_per_cpu(void)
{
    switch (cpu_count)
    {
        case 0:
        case 1:
        case 2:
        case 4:
          return 2;
        case 6:
          return 4;
        case 8:
        case 12:
          return 6;
        case 16:
        case 24:
          return 8;
        case 32:
        case 64:
          return 12;
        default:
            return cpu_count / 4;
    }
    
    return 2;
}

static void
manager_reinit(void)
{
    if (manager_init() < 0)
        abort();
}

int
manager_init(void)
{
    wqlist_has_manager = 0;
    pthread_cond_init(&wqlist_has_work, NULL);

    pthread_mutex_init(&wqlist_mtx, NULL);
    wqlist_mask = 0;
    pending_thread_create = 0;
    
    pthread_cond_init(&ocwq_has_work, NULL);
    pthread_mutex_init(&ocwq_mtx, NULL);
    ocwq_mask = 0;
    ocwq_idle_threads = 0;
    ocwq_signal_count = 0;

    witem_cache_init();

    cpu_count = (PWQ_ACTIVE_CPU > 0) ? (PWQ_ACTIVE_CPU) : (unsigned int) sysconf(_SC_NPROCESSORS_ONLN);

    pthread_attr_init(&detached_attr);
    pthread_attr_setdetachstate(&detached_attr, PTHREAD_CREATE_DETACHED);

    /* Initialize the scoreboard */
    
    if (sem_init(&scoreboard.sb_sem, 0, 0) != 0) {
        dbg_perror("sem_init()");
        return (-1);
    }
    
    scoreboard.count = 0;
    scoreboard.idle = 0;
    scoreboard.sb_suspend = 0;
    
    /* Determine the initial thread pool constraints */
    worker_min = 2; // we can start with a small amount, worker_idle_threshold will be used as new dynamic low watermark
    worker_idle_threshold = (PWQ_ACTIVE_CPU > 0) ? (PWQ_ACTIVE_CPU) : worker_idle_threshold_per_cpu();

/* FIXME: should test for symbol instead of for Android */
    if (pthread_atfork(NULL, NULL, manager_reinit) < 0) {
        dbg_perror("pthread_atfork()");
        return (-1);
    }

    return (0);
}

/* FIXME: should test for symbol instead of for Android */

void
manager_workqueue_create(struct _pthread_workqueue *workq)
{
    pthread_mutex_lock(&wqlist_mtx);
    if (!workq->overcommit && !wqlist_has_manager)
        manager_start();

    if (workq->overcommit) {
        if (ocwq[workq->queueprio] == NULL) {
            ocwq[workq->queueprio] = workq;
            workq->wqlist_index = workq->queueprio;
            dbg_printf("created workqueue (ocommit=1, prio=%d)", workq->queueprio);
        } else {
            printf("oc queue %d already exists\n", workq->queueprio);
            abort();
        }
    } else {
        if (wqlist[workq->queueprio] == NULL) {
            wqlist[workq->queueprio] = workq; //FIXME: sort by priority
            workq->wqlist_index = workq->queueprio;
            dbg_printf("created workqueue (ocommit=0, prio=%d)", workq->queueprio);
        } else {
            printf("queue %d already exists\n", workq->queueprio);
            abort();
        }
    }
    pthread_mutex_unlock(&wqlist_mtx);
}

static void *
overcommit_worker_main(void *unused __attribute__ ((unused)))
{
    struct timespec ts;
    pthread_workqueue_t workq;
    void (*func)(void *);
    void *func_arg;
    struct work *witem;
    int rv, idx;
    sigset_t sigmask;

    /* Block all signals */
    sigfillset(&sigmask);
    pthread_sigmask(SIG_BLOCK, &sigmask, NULL);
     
    pthread_mutex_lock(&ocwq_mtx);

    for (;;) {
        /* Find the highest priority workqueue that is non-empty */
        idx = ffs(ocwq_mask);
        if (idx > 0) {
            workq = ocwq[idx - 1];
            witem = STAILQ_FIRST(&workq->item_listhead);
            if (witem != NULL) {
                /* Remove the first work item */
                STAILQ_REMOVE_HEAD(&workq->item_listhead, item_entry);
                if (STAILQ_EMPTY(&workq->item_listhead))
                    ocwq_mask &= ~(0x1 << workq->wqlist_index);
                /* Execute the work item */
                pthread_mutex_unlock(&ocwq_mtx);
                func = witem->func;
                func_arg = witem->func_arg;
                witem_free(witem);
                func(func_arg);    
                pthread_mutex_lock(&ocwq_mtx);
                continue;
            }
        }

        /* Wait for more work to be available. */
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 15;
        ocwq_idle_threads++;
        dbg_printf("waiting for work (idle=%d)", ocwq_idle_threads);
        rv = pthread_cond_timedwait(&ocwq_has_work, &ocwq_mtx, &ts);
        if (ocwq_signal_count > 0) {
            ocwq_signal_count--;
            continue;
        }

        if ((rv == 0) || (rv == ETIMEDOUT)) {
            /* Normally, the signaler will decrement the idle counter,
               but this path is not taken in response to a signaler.
             */
            ocwq_idle_threads--;
            pthread_mutex_unlock(&ocwq_mtx);
            break;
        }

        dbg_perror("pthread_cond_timedwait");
        abort();
        break;
    }

    dbg_printf("worker exiting (idle=%d)", ocwq_idle_threads);
    pthread_exit(NULL);
    /* NOTREACHED */
    return (NULL);
}

static inline void reset_queue_mask(unsigned int wqlist_index)
{
    unsigned int wqlist_index_bit = (0x1 << wqlist_index);
    unsigned int new_mask;
    
    // Remove this now empty wq from the mask, the only contention here is with threads performing the same
    // operation on another workqueue, so we will not be long
    // the 'bit' for this queue is protected by the spin lock, so we will only clear a bit which we have 
    // ownership for (see below for the corresponding part on the producer side)
    
    new_mask = atomic_and(&wqlist_mask, ~(wqlist_index_bit));
    
    while (slowpath(new_mask & wqlist_index_bit))
    {
        _hardware_pause();
        new_mask = atomic_and(&wqlist_mask, ~(wqlist_index_bit));
    }       
    
    return;
}

static struct work *
wqlist_scan(int *queue_priority, int skip_thread_exit_events)
{
    pthread_workqueue_t workq;
    struct work *witem = NULL;
    int idx;
    
    idx = ffs(wqlist_mask);

    if (idx == 0)
        return (NULL);
    
    workq = wqlist[idx - 1];
    
    pthread_spin_lock(&workq->mtx);

    witem = STAILQ_FIRST(&workq->item_listhead);

    if (witem)
    {
        if (!(skip_thread_exit_events && (witem->func == NULL)))
        {
            STAILQ_REMOVE_HEAD(&workq->item_listhead, item_entry);
            
            if (STAILQ_EMPTY(&workq->item_listhead))
                reset_queue_mask(workq->wqlist_index);
            
            *queue_priority = workq->queueprio;
        }
        else
            witem = NULL;
    }

    pthread_spin_unlock(&workq->mtx);
    
    return (witem); // NULL if multiple threads raced for the same queue 
}

// Optional busy loop for getting the next item for a while if so configured
// We'll only spin limited number of threads at a time (this is really mostly useful when running
// in low latency configurations using dedicated processor sets, usually a single spinner makes sense)

static struct work *
wqlist_scan_spin(int *queue_priority)
{
    struct work *witem = NULL;
    
    // Start spinning if relevant, otherwise skip and go through
    // the normal wqlist_scan_wait slowpath by returning the NULL witem.
    if (atomic_inc_nv(&current_threads_spinning) <= PWQ_SPIN_THREADS)
    {
        while ((witem = wqlist_scan(queue_priority, 1)) == NULL)
            _hardware_pause();
        
        /* Force the manager thread to wakeup if we are the last idle one */
        if (scoreboard.idle == 1)
            (void) sem_post(&scoreboard.sb_sem);        
    }
    
    atomic_dec(&current_threads_spinning);    

    return witem;
}

// Normal slowpath for waiting on the condition for new work
// here we also exit workers when needed
static struct work *
wqlist_scan_wait(int *queue_priority)
{
    struct work *witem = NULL;
    
    pthread_mutex_lock(&wqlist_mtx);
    
    while ((witem = wqlist_scan(queue_priority, 0)) == NULL)
        pthread_cond_wait(&wqlist_has_work, &wqlist_mtx);
    
    pthread_mutex_unlock(&wqlist_mtx);

    /* Force the manager thread to wakeup if we are the last idle one */
    if (scoreboard.idle == 1)
        (void) sem_post(&scoreboard.sb_sem);        

    // We only process worker exists from the slow path, wqlist_scan only returns them here
    if (slowpath(witem->func == NULL)) 
    {
        dbg_puts("worker exiting..");
        atomic_dec(&scoreboard.idle);
        atomic_dec(&scoreboard.count);
        witem_free(witem);
        pthread_exit(0);
    }
    
    return witem;
}

static void *
worker_main(void *unused __attribute__ ((unused)))
{
    struct work *witem;
    int current_thread_priority = WORKQ_DEFAULT_PRIOQUEUE;
    int queue_priority = WORKQ_DEFAULT_PRIOQUEUE;

    dbg_puts("worker thread started");
    atomic_dec(&pending_thread_create);
        
    for (;;) {

        witem = wqlist_scan(&queue_priority, 1); 

        if (!witem)
        {
            witem = wqlist_scan_spin(&queue_priority);
            
            if (!witem)
                witem = wqlist_scan_wait(&queue_priority);
        }
           
        if (PWQ_RT_THREADS && (current_thread_priority != queue_priority))
        {
            current_thread_priority = queue_priority;
            ptwq_set_current_thread_priority(current_thread_priority);
        }
        
        /* Invoke the callback function */

        atomic_dec(&scoreboard.idle);
        
        witem->func(witem->func_arg);    

        atomic_inc(&scoreboard.idle); 
        
        witem_free(witem);
    }

    /* NOTREACHED */
    return (NULL);
}

static int
worker_start(void) 
{
    pthread_t tid;

    dbg_puts("Spawning another worker");

    atomic_inc(&pending_thread_create);
    atomic_inc(&scoreboard.idle); // initialize in idle state to avoid unnecessary thread creation
    atomic_inc(&scoreboard.count);

    if (pthread_create(&tid, &detached_attr, worker_main, NULL) != 0) {
        dbg_perror("pthread_create(3)");
        atomic_dec(&scoreboard.idle);
        atomic_dec(&scoreboard.count);
        return (-1);
    }

    return (0);
}

static int
worker_stop(void) 
{
    struct work *witem;
    pthread_workqueue_t workq;
    int i;
    unsigned int wqlist_index_bit, new_mask;

    witem = witem_alloc(NULL, NULL);

    pthread_mutex_lock(&wqlist_mtx);
    for (i = 0; i < PTHREAD_WORKQUEUE_MAX; i++) {
        workq = wqlist[i];
        if (workq == NULL)
            continue;

        wqlist_index_bit = (0x1 << workq->wqlist_index);

        pthread_spin_lock(&workq->mtx);
        
        new_mask = atomic_or(&wqlist_mask, wqlist_index_bit);
        
        while (slowpath(!(new_mask & wqlist_index_bit)))
        {
            _hardware_pause();
            new_mask = atomic_or(&wqlist_mask, wqlist_index_bit);                
        }             

        STAILQ_INSERT_TAIL(&workq->item_listhead, witem, item_entry);

        pthread_spin_unlock(&workq->mtx);
        
        pthread_cond_signal(&wqlist_has_work);
        pthread_mutex_unlock(&wqlist_mtx);

        return (0);
    }

    /* FIXME: this means there are no workqueues.. should never happen */
    dbg_puts("Attempting to add a workitem without a workqueue");
    abort();

    return (-1);
}

static void *
manager_main(void *unused __attribute__ ((unused)))
{
    unsigned int runqueue_length_max = cpu_count; 
    unsigned int worker_max, threads_total = 0, current_thread_count = 0;
    unsigned int worker_idle_seconds_accumulated = 0;
    unsigned int max_threads_to_stop = 0;
    unsigned int i, idle_surplus_threads = 0;
    int sem_timedwait_rv = 0;
    sigset_t sigmask;
    struct timespec   ts;
    struct timeval    tp;

    worker_max = get_process_limit();
    scoreboard.runqueue_length = get_runqueue_length();

    /* Block all signals */
    sigfillset(&sigmask);
    pthread_sigmask(SIG_BLOCK, &sigmask, NULL);

    /* Create the minimum number of workers */
    for (i = 0; i < worker_min; i++)
        worker_start();

    for (;;) {

        if (scoreboard.sb_suspend == 0) {
            dbg_puts("manager is sleeping");

            if (gettimeofday(&tp, NULL) != 0) {
                dbg_perror("gettimeofday()"); // can only fail due to overflow of date > 2038 on 32-bit platforms...
            }

            /* Convert from timeval to timespec */
            ts.tv_sec  = tp.tv_sec;
            ts.tv_nsec = tp.tv_usec * 1000;
            ts.tv_sec += 1; // wake once per second and check if we have too many idle threads...

            // We should only sleep on the condition if there are no pending signal, spurious wakeup is also ok

            if ((sem_timedwait_rv = sem_timedwait(&scoreboard.sb_sem, &ts)) != 0)
            {
                sem_timedwait_rv = errno; // used for ETIMEDOUT below
                if (errno != ETIMEDOUT)
                    dbg_perror("sem_timedwait()");
            }

            dbg_puts("manager is awake");
        } else {
            dbg_puts("manager is suspending");
            if (sem_wait(&scoreboard.sb_sem) != 0)
                dbg_perror("sem_wait()");
            dbg_puts("manager is resuming");
        }
        
        dbg_printf("idle=%u workers=%u max_workers=%u worker_min = %u",
                   scoreboard.idle, scoreboard.count, worker_max, worker_min);
                
        // If no workers available, check if we should create a new one
        if ((scoreboard.idle == 0) && (scoreboard.count > 0) && (pending_thread_create == 0)) // last part required for an extremely unlikely race at startup
        {
            // allow cheap rampup up to worker_idle_threshold without going to /proc / checking run queue length
            if (scoreboard.count < worker_idle_threshold) 
            {
                worker_start();
            }                
            else 
            {
                // otherwise check if run queue length / stalled threads allows for new creation unless we hit worker_max ceiling
                
                if (scoreboard.count < worker_max)
                {
                    if (threads_runnable(&current_thread_count, &threads_total) != 0)
                        current_thread_count = 0;
                    
                    // only start thread if we have less runnable threads than cpus and run queue length allows it
                    
                    if (current_thread_count <= cpu_count) // <= discounts the manager thread
                    {
                        scoreboard.runqueue_length = get_runqueue_length(); 

                        if (scoreboard.runqueue_length <= runqueue_length_max) // <= discounts the manager thread
                        {
                            if (scoreboard.idle == 0) // someone might have become idle during getting thread count etc.
                                worker_start();
                            else
                                dbg_puts("skipped thread creation as we got an idle one racing us");

                        }
                        else
                        {
                            dbg_printf("Not spawning worker thread, scoreboard.runqueue_length = %d > runqueue_length_max = %d", 
                                       scoreboard.runqueue_length, runqueue_length_max);
                        }
                    }
                    else
                    {
                        dbg_printf("Not spawning worker thread, thread_runnable = %d > cpu_count = %d", 
                                   current_thread_count, cpu_count);
                    }
                }
                else
                {
                    dbg_printf("Not spawning worker thread, scoreboard.count = %d >= worker_max = %d", 
                               scoreboard.count, worker_max);
                }                
            }
        }
        else
        {
            if (sem_timedwait_rv == ETIMEDOUT) // Only check for ramp down on the 'timer tick'
            {
                if (scoreboard.idle > worker_idle_threshold) // only accumulate if there are 'too many' idle threads
                {
                    worker_idle_seconds_accumulated += scoreboard.idle; // keep track of many idle 'thread seconds' we have
                
                    dbg_printf("worker_idle_seconds_accumulated = %d, scoreboard.idle = %d, scoreboard.count = %d\n",
                       worker_idle_seconds_accumulated, scoreboard.idle, scoreboard.count);
                }
                else
                {
                    dbg_puts("Resetting worker_idle_seconds_accumulated");
                    worker_idle_seconds_accumulated = 0;
                }
                
                // Only consider ramp down if we have accumulated enough thread 'idle seconds'
                // this logic will ensure that a large number of idle threads will ramp down faster
                max_threads_to_stop = worker_idle_seconds_accumulated / WORKER_IDLE_SECONDS_THRESHOLD;

                if (max_threads_to_stop > 0)
                {
                    worker_idle_seconds_accumulated = 0; 
                    idle_surplus_threads = scoreboard.idle - worker_idle_threshold; 
                    
                    if (max_threads_to_stop > idle_surplus_threads)
                        max_threads_to_stop = idle_surplus_threads;

                    // Only stop threads if we actually have 'too many' idle ones in the pool
                    for (i = 0; i < max_threads_to_stop; i++)
                    {
                        if (scoreboard.idle > worker_idle_threshold)
                        {
                            dbg_puts("Removing one thread from the thread pool");
                            worker_stop();
                        }                    
                    }
                }
            }            
        }        
    }

    /*NOTREACHED*/
    return (NULL);
}

static void
manager_start(void)
{
    pthread_t tid;
    int rv;

    dbg_puts("starting the manager thread");

    do {
        rv = pthread_create(&tid, &detached_attr, manager_main, NULL);
        if (rv == EAGAIN) {
            sleep(1);
        } else if (rv != 0) {
            /* FIXME: not nice */
            dbg_printf("thread creation failed, rv=%d", rv);
            abort();
        }
    } while (rv != 0);

    wqlist_has_manager = 1;
}

void 
manager_suspend(void)
{
    /* Wait for the manager thread to be initialized */
    while (wqlist_has_manager == 0) {
        sleep(1);
    }
    if (scoreboard.sb_suspend == 0) { 
        scoreboard.sb_suspend = 1;
    }
}

void
manager_resume(void)
{
    if (scoreboard.sb_suspend) { 
        scoreboard.sb_suspend = 0;
        __sync_synchronize();
        (void) sem_post(&scoreboard.sb_sem);        
    }
}

void
manager_workqueue_additem(struct _pthread_workqueue *workq, struct work *witem)
{
    unsigned int wqlist_index_bit = (0x1 << workq->wqlist_index);
    
    if (slowpath(workq->overcommit)) {
        pthread_t tid;

        pthread_mutex_lock(&ocwq_mtx);
        pthread_spin_lock(&workq->mtx);
        STAILQ_INSERT_TAIL(&workq->item_listhead, witem, item_entry);
        pthread_spin_unlock(&workq->mtx);
        ocwq_mask |= wqlist_index_bit;
        if (ocwq_idle_threads > 0) {
            dbg_puts("signaling an idle worker");
            pthread_cond_signal(&ocwq_has_work);
            ocwq_idle_threads--;
            ocwq_signal_count++;
        } else {
            (void)pthread_create(&tid, &detached_attr, overcommit_worker_main, NULL);
        }
        pthread_mutex_unlock(&ocwq_mtx);
    } else {
        pthread_spin_lock(&workq->mtx);

        // Only set the mask for the first item added to the workqueue. 
        if (STAILQ_EMPTY(&workq->item_listhead))
        {
            unsigned int new_mask;
            
            // The only possible contention here are with threads performing the same
            // operation on another workqueue, so we will not be blocked long... 
            // Threads operating on the same workqueue will be serialized by the spinlock so it is very unlikely.

            new_mask = atomic_or(&wqlist_mask, wqlist_index_bit);

            while (slowpath(!(new_mask & wqlist_index_bit)))
            {
                _hardware_pause();
                new_mask = atomic_or(&wqlist_mask, wqlist_index_bit);                
            }             
        }
        
        STAILQ_INSERT_TAIL(&workq->item_listhead, witem, item_entry);

        pthread_spin_unlock(&workq->mtx);

        // Only signal thread wakeup if there are idle threads available
        // and no other thread have managed to race us and empty the wqlist on our behalf already
        if (scoreboard.idle > 0) // && ((wqlist_mask & wqlist_index_bit) != 0)) // disabling this fringe optimization for now
        {
            pthread_cond_signal(&wqlist_has_work); // don't need to hold the mutex to signal
        }
    }
}


static unsigned int
get_process_limit(void)
{
    struct rlimit rlim;

    if (getrlimit(RLIMIT_NPROC, &rlim) < 0) {
        dbg_perror("getrlimit(2)");
        return (DEFAULT_PROCESS_LIMIT);
    } else {
        return (rlim.rlim_max);
    }
}

static unsigned int
get_runqueue_length(void)
{
    double loadavg;

    /* Prefer to use the most recent measurement of the number of running KSEs
    for Linux and the kstat unix:0:sysinfo: runque/updates ratio for Solaris . */

    return linux_get_runqueue_length();

    /* Fallback to using the 1-minute load average if proper run queue length can't be determined. */

    /* TODO: proper error handling */
    if (getloadavg(&loadavg, 1) != 1) {
        dbg_perror("getloadavg(3)");
        return (1);
    }
    if (loadavg > INT_MAX || loadavg < 0)
        loadavg = 1;

    return ((int) loadavg);
}

unsigned long 
manager_peek(const char *key)
{
    uint64_t rv;

    if (strcmp(key, "combined_idle") == 0) {
        rv = scoreboard.idle;
        if (scoreboard.idle > worker_min)
            rv -= worker_min;
        rv += ocwq_idle_threads;
    } else if (strcmp(key, "idle") == 0) {
        rv = scoreboard.idle;
        if (scoreboard.idle > worker_min)
            rv -= worker_min;
    } else if (strcmp(key, "ocomm_idle") == 0) {
        rv = ocwq_idle_threads;
    } else {
        dbg_printf("invalid key: %s", key);
        abort();
    }

    return rv;
}

// Default POSIX implementation doesn't support it, platform-specific code does though.

// Default POSIX implementation doesn't support it, platform-specific code does though.

/* Problem: does not include the length of the runqueue, and
     subtracting one from the # of actually running processes will
     always show free CPU even when there is none. */

unsigned int
linux_get_runqueue_length(void)
{
    int fd;
    char   buf[16384];
    char   *p;
    ssize_t  len = 0;
    unsigned int     runqsz = 0;

    fd = open("/proc/stat", O_RDONLY);
    if (fd <0) {
        dbg_perror("open() of /proc/stat");
        return (1);
    }

    /* Read the entire file into memory */
    len = read(fd, &buf, sizeof(buf) - 1);
    if (len < 0) {
        dbg_perror("read failed");
        goto out;
    }
    //printf("buf=%s len=%d\n", buf, (int)len);

    /* Search for 'procs_running %d' */
    p = strstr(buf, "procs_running");
    if (p != NULL) {
        runqsz = atoi(p + 14);
    }

out:
    if (runqsz == 0) {
        /* TODO: this should be an assertion */
        runqsz = 1; //WORKAROUND
    }

    (void) close(fd);

    return (runqsz);
}

/* 
 
 /proc for Linux
 
 /proc/self
 This directory refers to the process accessing the /proc filesystem, and is identical to the /proc directory named by the process ID of the same process.
 
 ----------
 
 /proc/[number]/stat
 Status information about the process. This is used by ps(1). It is defined in /usr/src/linux/fs/proc/array.c.
 The fields, in order, with their proper scanf(3) format specifiers, are:
 
 pid %d
 The process ID.
 
 comm %s
 The filename of the executable, in parentheses. This is visible whether or not the executable is swapped out.
 
 state %c
 One character from the string "RSDZTW" where R is running, S is sleeping in an interruptible wait, D is waiting in uninterruptible disk sleep, Z is zombie, T is traced or stopped (on a signal), and W is paging.
 
 ---------------
 
 /proc/[number]/task (since kernel 2.6.0-test6)
 This is a directory that contains one subdirectory for each thread in the process. The name of each subdirectory is the numerical thread ID of the thread (see gettid(2)). Within each of these subdirectories, there is a set of files with the same names and contents as under the /proc/[number] directories. For attributes that are shared by all threads, the contents for each of the files under the task/[thread-ID] subdirectories will be the same as in the corresponding file in the parent /proc/[number] directory (e.g., in a multithreaded process, all of the task/[thread-ID]/cwd files will have the same value as the /proc/[number]/cwd file in the parent directory, since all of the threads in a process share a working directory). For attributes that are distinct for each thread, the corresponding files under task/[thread-ID] may have different values (e.g., various fields in each of the task/[thread-ID]/status files may be different for each thread).
 In a multithreaded process, the contents of the /proc/[number]/task directory are not available if the main thread has already terminated (typically by calling pthread_exit(3)).
 
 ---------------

 Example:
 read data from /proc/self/task/11019/stat: [11019 (lt-dispatch_sta) D 20832 10978 20832 34819 10978 4202560 251 3489 0 0 0 2 2 5 20 0 37 0 138715543 2538807296 13818 18446744073709551615 4194304 4203988 140736876632592 139770298610200 139771956665732 0 0 0 0 0 0 0 -1 2 0 0 0 0 0

*/

#define MAX_RESULT_SIZE 4096

static int _read_file(const char *path, char *result)
{
	int read_fd, retval = -1;
    ssize_t actual_read;
    
    read_fd = open(path, O_RDONLY);
	if (read_fd == -1) 
    {
        dbg_perror("open()");
        return retval;
	}
    
	if (fcntl(read_fd, F_SETFL, O_NONBLOCK) != 0) 
    {
        dbg_perror("fcntl()");
        goto errout;
	}
        

    actual_read = read(read_fd, result, MAX_RESULT_SIZE);
	
    dbg_printf("read %zu from %s", (size_t) actual_read, path);

    if (actual_read == 0)
    {
        goto errout;
    }
    
    retval = 0;
    
errout:
    if (close(read_fd) != 0)
    {
        dbg_perror("close()");
    }
    
    return retval;
}


int threads_runnable(unsigned int *threads_running, unsigned int *threads_total)
{
    DIR             *dip;
    struct dirent   *dit;
    const char *task_path = "/proc/self/task";
    char thread_path[1024];
    char thread_data[MAX_RESULT_SIZE+1];
    char dummy[MAX_RESULT_SIZE+1];
    char state;
    int pid;
    unsigned int running_count = 0, total_count = 0;

    dbg_puts("Checking threads_runnable()");

    if ((dip = opendir(task_path)) == NULL)
    {
        dbg_perror("opendir");
        return -1;
    }
        
    while ((dit = readdir(dip)) != NULL)
    {
        memset(thread_data, 0, sizeof(thread_data));
        
        sprintf(thread_path, "%s/%s/stat",task_path, dit->d_name);

        if (_read_file(thread_path, thread_data) == 0)
        {
            if (sscanf(thread_data, "%d %s %c", &pid, dummy, &state) == 3)
            {
                total_count++;
                dbg_printf("The state for thread %s is %c", dit->d_name, state);
                switch (state)
                {
                    case 'R':
                        running_count++;
                        break;
                    default:
                        break;
                }
            }
            else
            {
                dbg_printf("Failed to scan state for thread %s (%s)", dit->d_name, thread_data);
            }
        }
    }

    if (closedir(dip) == -1)
    {
        perror("closedir");
    }

    dbg_printf("Running count is %d", running_count);
    *threads_running = running_count;
    *threads_total = total_count;
    
    return 0;
}


void ptwq_set_current_thread_priority(int priority  __attribute__ ((unused)))
{    
    return;
}
