/*-
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

#ifndef _PTHREAD_WORKQUEUE_H
#define _PTHREAD_WORKQUEUE_H

#include <sys/queue.h>
#include <pthread.h>

#ifndef PWQ_EXPORT
#define PWQ_EXPORT extern
#endif

typedef struct _pthread_workqueue *pthread_workqueue_t;
typedef void *pthread_workitem_handle_t;

/* Pad size to 64 bytes. */
typedef struct {
	unsigned int sig;
	int queueprio;
	int overcommit;
	unsigned int pad[13];
} pthread_workqueue_attr_t;

/* Work queue priority attributes. */
#define WORKQ_HIGH_PRIOQUEUE 0
#define WORKQ_DEFAULT_PRIOQUEUE 1
#define WORKQ_LOW_PRIOQUEUE 2
#define WORKQ_BG_PRIOQUEUE 3

#if defined(__cplusplus)
extern "C" {
#endif

PWQ_EXPORT
int pthread_workqueue_create_np(pthread_workqueue_t *workqp,
								const pthread_workqueue_attr_t *attr);

PWQ_EXPORT
int pthread_workqueue_additem_np(pthread_workqueue_t workq,
								 void (*workitem_func)(void *),
								 void *workitem_arg,
								 pthread_workitem_handle_t *itemhandlep,
								 unsigned int *gencountp);

PWQ_EXPORT
int pthread_workqueue_attr_init_np(pthread_workqueue_attr_t *attrp);

PWQ_EXPORT
int pthread_workqueue_attr_destroy_np(pthread_workqueue_attr_t *attr);

PWQ_EXPORT
int pthread_workqueue_attr_getqueuepriority_np(pthread_workqueue_attr_t *attr,
											   int *qpriop);

PWQ_EXPORT
int pthread_workqueue_attr_setqueuepriority_np(pthread_workqueue_attr_t *attr,
											   int qprio);

PWQ_EXPORT
int pthread_workqueue_attr_getovercommit_np(
	const pthread_workqueue_attr_t *attr, int *ocommp);

PWQ_EXPORT
int pthread_workqueue_attr_setovercommit_np(pthread_workqueue_attr_t *attr,
											int ocomm);

PWQ_EXPORT
int pthread_workqueue_requestconcurrency_np(pthread_workqueue_t workq,
											int queue,
											int request_concurrency);

PWQ_EXPORT
int pthread_workqueue_getovercommit_np(pthread_workqueue_t workq,
									   unsigned int *ocommp);

PWQ_EXPORT
void pthread_workqueue_main_np(void);

PWQ_EXPORT
int pthread_workqueue_init_np(void);

/* NOTE: these are not part of the Darwin API */
PWQ_EXPORT
unsigned long pthread_workqueue_peek_np(const char *);
PWQ_EXPORT
void pthread_workqueue_suspend_np(void);
PWQ_EXPORT
void pthread_workqueue_resume_np(void);

#if defined(__cplusplus)
}
#endif
#endif /* _PTHREAD_WORKQUEUE_H */

#ifndef _DEBUG_H
#define _DEBUG_H

#include <assert.h>

extern int DEBUG_WORKQUEUE;
extern char *WORKQUEUE_DEBUG_IDENT;

#include <linux/unistd.h>
#include <sys/syscall.h>
#include <unistd.h>

#define THREAD_ID ((pid_t)syscall(__NR_gettid))

#define dbg_puts(str)                      \
	do {                                   \
		if (DEBUG_WORKQUEUE)               \
			fprintf(stderr,                \
					"%s [%d]: %s(): %s\n", \
					WORKQUEUE_DEBUG_IDENT, \
					THREAD_ID,             \
					__func__,              \
					str);                  \
	} while (0)

#define dbg_printf(fmt, ...)                    \
	do {                                        \
		if (DEBUG_WORKQUEUE)                    \
			fprintf(stderr,                     \
					"%s [%d]: %s(): " fmt "\n", \
					WORKQUEUE_DEBUG_IDENT,      \
					THREAD_ID,                  \
					__func__,                   \
					__VA_ARGS__);               \
	} while (0)

#define dbg_perror(str)                                   \
	do {                                                  \
		if (DEBUG_WORKQUEUE)                              \
			fprintf(stderr,                               \
					"%s [%d]: %s(): %s: %s (errno=%d)\n", \
					WORKQUEUE_DEBUG_IDENT,                \
					THREAD_ID,                            \
					__func__,                             \
					str,                                  \
					strerror(errno),                      \
					errno);                               \
	} while (0)

#define reset_errno() \
	do {              \
		errno = 0;    \
	} while (0)

#define dbg_lasterror(str) ;

#endif /* ! _DEBUG_H */

#ifndef _PTWQ_PRIVATE_H
#define _PTWQ_PRIVATE_H 1

/* The maximum number of workqueues that can be created.
   This is based on libdispatch only needing 8 workqueues.
   */
#define PTHREAD_WORKQUEUE_MAX 31

/* The total number of priority levels. */
#define WORKQ_NUM_PRIOQUEUE 4

/* Signatures/magic numbers.  */
#define PTHREAD_WORKQUEUE_SIG 0xBEBEBEBE
#define PTHREAD_WORKQUEUE_ATTR_SIG 0xBEBEBEBE

/* Whether to use real-time threads for the workers if available */

extern unsigned int PWQ_RT_THREADS;
extern unsigned int PWQ_SPIN_THREADS;

/* A limit of the number of cpu:s that we view as available, useful when e.g.
 * using processor sets */
extern unsigned int PWQ_ACTIVE_CPU;

#if __GNUC__
#define fastpath(x) ((__typeof__(x))__builtin_expect((long)(x), ~0l))
#define slowpath(x) ((__typeof__(x))__builtin_expect((long)(x), 0l))
#else
#define fastpath(x) (x)
#define slowpath(x) (x)
#endif

#define CACHELINE_SIZE 64
#define ROUND_UP_TO_CACHELINE_SIZE(x) \
	(((x) + (CACHELINE_SIZE - 1)) & ~(CACHELINE_SIZE - 1))

/* We should perform a hardware pause when using the optional busy waiting, see:
   http://software.intel.com/en-us/articles/ap949-using-spin-loops-on-intel-pentiumr-4-processor-and-intel-xeonr-processor/
 rep/nop / 0xf3+0x90 are the same as the symbolic 'pause' instruction
 */

#if defined(__i386__) || defined(__x86_64__) || defined(__i386) || \
	defined(__amd64)

#if defined(__SUNPRO_CC)

#define _hardware_pause() asm volatile("rep; nop\n");

#elif defined(__GNUC__)

#define _hardware_pause() __asm__ __volatile__("pause");

#else

#define _hardware_pause() __asm__("pause")

#endif

/* XXX-FIXME this is a stub, need to research what ARM assembly to use */
#elif defined(__ARM_EABI__)

#define _hardware_pause() __asm__("")

#else

/* XXX-FIXME this is a stub, need to research what assembly to use */
#define _hardware_pause() __asm__("")

#endif

/*
 * The work item cache, has three different optional implementations:
 * 1. No cache, just normal malloc/free using the standard malloc library in use
 * 2. Libumem based object cache, requires linkage with libumem - for
 * non-Solaris see http://labs.omniti.com/labs/portableumem
 *    this is the most balanced cache supporting migration across threads of
 * allocated/freed witems
 * 3. TSD based cache, modelled on libdispatch continuation implementation, can
 * lead to imbalance with assymetric
 *    producer/consumer threads as allocated memory is cached by the thread
 * freeing it
 */
#define WITEM_CACHE_TYPE \
	1  // Otherwise fallback to normal malloc/free - change specify witem cache
	   // implementation to use

struct work {
	STAILQ_ENTRY(work) item_entry;
	void (*func)(void *);
	void *func_arg;
	unsigned int flags;
	unsigned int gencount;
};

struct _pthread_workqueue {
	unsigned int sig; /* Unique signature for this structure */
	unsigned int flags;
	int queueprio;
	int overcommit;
	unsigned int wqlist_index;
	STAILQ_HEAD(, work) item_listhead;
	pthread_spinlock_t mtx;
#ifdef WORKQUEUE_PLATFORM_SPECIFIC
	WORKQUEUE_PLATFORM_SPECIFIC;
#endif
};

/* manager.c */
int manager_init(void);
unsigned long manager_peek(const char *);
void manager_suspend(void);
void manager_resume(void);
void manager_workqueue_create(struct _pthread_workqueue *);
void manager_workqueue_additem(struct _pthread_workqueue *, struct work *);

struct work *witem_alloc(
	void (*func)(void *),
	void *func_arg);  // returns a properly initialized witem
void witem_free(struct work *wi);
int witem_cache_init(void);
void witem_cache_cleanup(void *value);

#endif /* _PTWQ_PRIVATE_H */

#ifndef _PTWQ_POSIX_THREAD_INFO_H
#define _PTWQ_POSIX_THREAD_INFO_H 1

int threads_runnable(unsigned int *threads_running,
					 unsigned int *threads_total);

#endif /* _PTWQ_POSIX_THREAD_INFO_H */

#ifndef _PTWQ_THREAD_RT_H
#define _PTWQ_THREAD_RT_H 1

void ptwq_set_current_thread_priority(int priority);  // higher is better

#endif /* _PTWQ_THREAD_RT_H */
#ifndef _PTWQ_POSIX_PLATFORM_H
#define _PTWQ_POSIX_PLATFORM_H 1

#ifdef __FreeBSD__
/* Workaround to get visibility for _SC_NPROCESSORS_ONLN on FreeBSD */
#define __BSD_VISIBLE 1
#include <sys/types.h>
#endif

#include <sys/resource.h>
#include <sys/queue.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <pthread.h>

/* GCC atomic builtins.
 * See: http://gcc.gnu.org/onlinedocs/gcc-4.1.0/gcc/Atomic-Builtins.html
 */
#define atomic_inc(p) (void) __sync_add_and_fetch((p), 1)
#define atomic_dec(p) (void) __sync_sub_and_fetch((p), 1)
#define atomic_inc_nv(p) __sync_add_and_fetch((p), 1)
#define atomic_dec_nv(p) __sync_sub_and_fetch((p), 1)
#define atomic_and(p, v) __sync_and_and_fetch((p), (v))
#define atomic_or(p, v) __sync_or_and_fetch((p), (v))

/*
 * Android does not provide spinlocks.
 * See: http://code.google.com/p/android/issues/detail?id=21622
 */

#endif /* _PTWQ_POSIX_PLATFORM_H */

#ifndef _LIBPWQ_LINUX_PLATFORM_H
#define _LIBPWQ_LINUX_PLATFORM_H

/*
 * Platform-specific functions for Linux
 */

unsigned int linux_get_runqueue_length(void);

/*
 * Android does not provide spinlocks.
 * See: http://code.google.com/p/android/issues/detail?id=21622
 */

#endif /* _LIBPWQ_LINUX_PLATFORM_H */
