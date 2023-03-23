#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "synch.h" // for semaphore struct
#include "fixed_point.h"

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                                  /* Thread identifier. */
    enum thread_status status;                  /* Thread state. */
    char name[16];                              /* Name (for debugging purposes). */
    uint8_t *stack;                             /* Saved stack pointer. */
    int priority;                               /* Priority. */
    struct list_elem allelem;                   /* List element for all threads list. */
    fp_real m_recent_cpu;                       // amount of CPU time a thread has received "recently"
    int m_nice_value;                           // "nice" value of the thread

    // owned by timer.c
    int64_t m_wakeup_tick;                      // store minimum tick for when a *sleeping* thread should be woken up
    struct list_elem m_sleep_timer;             // store sleep timer for a thread, used with m_wakeup_tick
    // ================
    
    // shared by timer.c and thread.c
    struct semaphore m_sleep_timer_semaphore;   // sempahore to block sleeping threads
    // ==============================

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;                      /* List element. */
    struct list m_donatees;                     // list of threads which are donating priority
    struct list_elem m_donor_elem;              // list element of donor list
    bool m_donated;                             // flag for whether the thread has donated so we don't donate multiple times
    struct lock* m_waiting_for_lock;            // pointer to a lock that is blocking thread
    // =======================================

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

// helper function to get a *modified* priority of a thread (max value of actual priority and donated priorities)
int get_modified_priority_of_thread(const struct thread* t);
// helper function to add a thread back into the ready queue
// ensures that the ready queue is always sorted by priority
void add_to_ready_queue(struct thread* t);
// used to requeue a thread into the ready list when it was waiting for a lock which has since been released.
void thread_requeue_after_lock_release(struct thread* t);

// helper function to get the number of ready or running threads
int get_ready_thread_count(void);

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

// helper function to tag the ready list as "dirty", meaning we have adjusted priorities through donation
// and the list may not be in order any more
void set_ready_list_dirty(bool dirty);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);
void thread_donate_priority(struct thread* receiver, struct thread* donor);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

NO_INLINE bool compare_threads_by_priority(const struct list_elem* thread_elem1,  const struct list_elem* thread_elem2, void* aux);

#endif /* threads/thread.h */
