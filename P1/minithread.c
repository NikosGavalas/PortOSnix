/*
 * minithread.c:
 *	This file provides a few function headers for the procedures that
 *	you are required to implement for the minithread assignment.
 *
 *	EXCEPT WHERE NOTED YOUR IMPLEMENTATION MUST CONFORM TO THE
 *	NAMING AND TYPING OF THESE PROCEDURES.
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "queue.h"
#include "synch.h"
#include "minithread.h"
#include "minithread_private.h"

/*
 * A minithread should be defined either in this file or in a private
 * header file.  Minithreads have a stack pointer with to make procedure
 * calls, a stackbase which points to the bottom of the procedure
 * call stack, the ability to be enqueueed and dequeued, and any other state
 * that you feel they must have.
 */

/*
 * struct minithread is defined in the private header "minithread_private.h".
 */

/* File scope pointers */
static struct minithread idle_thread_;
static minithread_t const idle_thread = &idle_thread_;
static struct thread_monitor thread_monitor;

/* File scope functions */
static void minithread_schedule();
static minithread_t minithread_pickold();
static minithread_t minithread_picknew();
static int minithread_exit(arg_t arg);

/* minithread functions */

minithread_t
minithread_self() {
	return thread_monitor.instack;
}

int
minithread_id() {
	return thread_monitor.instack->id;
}

/*
 * Allocate memory for and initialize a thread.
 * Return NULL when allocation fails.
 */
minithread_t
minithread_create(proc_t proc, arg_t arg) {
	minithread_t t;
	/* Allocate memory for TCB and stack. */
	if ((t = malloc(sizeof(*t))) == NULL) {
		printf("TCB memory allocation failed.\n");
		return NULL;
	}
	minithread_allocate_stack(&(t->base), &(t->top));
	if (NULL == t->base || NULL == t->top) {
	    printf("Stack allocation failed.\n");
        free(t);
        return NULL;
	}
	/* Initialize TCB and stack. */
	minithread_initialize_stack(&(t->top), proc, arg, minithread_exit, NULL);
	t->prev = NULL;
	t->next = NULL;
	t->id = thread_monitor.tidcount;
	t->status = INITIAL;
	++(thread_monitor.tidcount);
	++(thread_monitor.count);
	return t;
}

/* Add thread t to the tail of the ready queue. */
void
minithread_start(minithread_t t) {
    if (NULL == t)
        return;
	t->status = READY;
	queue_append(thread_monitor.ready, t);
	minithread_schedule();
}

/* Create and start a thread. */
minithread_t
minithread_fork(proc_t proc, arg_t arg) {
	minithread_t t = minithread_create(proc, arg);
	if (NULL == t)
        return NULL;
	minithread_start(t);
	return t;
}

/*
 * Add the running thread to the tail of the ready queue.
 * Let the scheduler decide if the thread needs to be switched out.
 */
void
minithread_yield() {
	thread_monitor.instack->status = READY;
	queue_append(thread_monitor.ready, thread_monitor.instack);
	minithread_schedule();
}

/*
 * The calling thread should be placed on the appropriate wait queue
 * before calling minithread_stop().
 */
void
minithread_stop() {
	thread_monitor.instack->status = BLOCKED;
	minithread_schedule();
}

/*
 * This is the 'final_proc' that helps threads exit properly.
 */
static int
minithread_exit(arg_t arg) {
	thread_monitor.instack->status = EXITED;
	queue_append(thread_monitor.exited, thread_monitor.instack);
	minithread_schedule();
	/*
     * The thread is switched out before this step,
	 * so it is not going to return.
	 * This is just to make the compiler happy.
	 */
	return 0;
}

/*
 * Release stack of exited threads.
 */
void
minithread_cleanup() {
    minithread_t t;
    while (0 == queue_dequeue(thread_monitor.exited, (void**)&t) && NULL != t) {
        if (NULL != t->base)
            minithread_free_stack(t->base);
        free(t);
        --(thread_monitor.count);
    }
}

/*
 * The running thread should be placed on the correct queue
 * before calling the scheduler.
 */
static void
minithread_schedule() {
	minithread_t rp_old = thread_monitor.instack;
	minithread_t rp_new;
	/* No switching when the thread in stack is running and not idle_thread. */
	if (idle_thread != rp_old && RUNNING == rp_old->status)
		return;
    /* No switching when dequeue fails (no ready thread, etc). */
	if (-1 == queue_dequeue(thread_monitor.ready, (void**)&rp_new)) {
	    rp_old->status = RUNNING;
        return;
	}
	/* Switch to another thread instead of idle_thread when possible. */
	if (idle_thread == rp_new) {
		if (0 == queue_length(thread_monitor.ready)) {
            /*
             * Two cases: rp_old exited or blocked (do nothing),
             *            and rp_old is the idle-thread.
             */
		    if (idle_thread == rp_old) {
                rp_old->status = RUNNING;
                return;
            }
		} else {
		    /* There exist other ready threads. */
            queue_append(thread_monitor.ready, rp_new);
			queue_dequeue(thread_monitor.ready, (void**)&rp_new);
		}
	}
	/* Switch out idle_thread when there is another ready thread. */
	if (idle_thread == rp_old && RUNNING == rp_old->status) {
		rp_old->status = READY;
		queue_append(thread_monitor.ready, rp_old);
	}

	/* Switch only when the threads are different. */
	thread_monitor.instack = rp_new;
	rp_new->status = RUNNING;
	if (rp_old != rp_new)
		minithread_switch(&(rp_old->top),&(rp_new->top));
}

/*
 * Initialization.
 *
 * 	minithread_system_initialize:
 *	 This procedure should be called from your C main procedure
 *	 to turn a single threaded UNIX process into a multithreaded
 *	 program.
 *
 *	 Initialize any private data structures.
 * 	 Create the idle thread.
 *       Fork the thread which should call mainproc(mainarg)
 * 	 Start scheduling.
 *
 */
void
minithread_system_initialize(proc_t mainproc, arg_t mainarg) {
	minithread_t main;
    idle_thread->status = RUNNING;
	thread_monitor.count = 1;
	thread_monitor.ready = queue_new();
	thread_monitor.exited = queue_new();
	thread_monitor.instack = idle_thread;

	main = minithread_create(mainproc, mainarg);
	if (NULL == main) {
		printf("Main thread creation failed.\n");
		exit(-1);
	}
	minithread_start(main);

	while (1) {
		minithread_cleanup();
		minithread_yield();
	}

//printf("Main thread finished.\n");
}


