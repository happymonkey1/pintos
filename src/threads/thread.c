#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "lib/kernel/list.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif
#include "devices/timer.h"

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

// static variable that stores the current load average of all the threads
static fp_real s_load_average;

// bool to control whether the ready list is dirty
static bool s_ready_list_dirty;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

inline static void thread_calculate_priority(struct thread* t)
{
  // check for programmer error
  if (!thread_mlfqs)
    return;
  
  fp_real fp_priority_val = fp_int_to_real(PRI_MAX) - (t->m_recent_cpu / 4) - fp_int_to_real(t->m_nice_value * 2);
  t->priority = fp_real_to_int_nearest(fp_priority_val);
  if (t->priority > PRI_MAX)
    t->priority = PRI_MAX;
  else if (t->priority < PRI_MIN)
    t->priority = PRI_MIN;
}

// helper function to recalculate recent cpu for all threads
// recent = (2 * load average)/(2 * load_average + 1) * recent + nice
void thread_recalculate_recent_cpu(void)
{
  // only do work if mlfqs is enabled
  if (!thread_mlfqs)
    return;
  
  // iterate all threads and recalculate recent cpu value
  for (struct list_elem* it = list_begin(&all_list); it != list_end(&all_list); it = list_next(it))
  {
    struct thread* t = list_entry(it, struct thread, allelem);
    // let a = (2 * load_avg)
    fp_real a = s_load_average * 2;
    // let b = (2 * load_avg + 1)
    fp_real b = fp_add(s_load_average * 2, 1);
    // let c = a/b
    fp_real c = fp_div(a, b);
    // let recent = c * recent + nice
    t->m_recent_cpu = fp_add(fp_mult(c, t->m_recent_cpu), t->m_nice_value);
  }
}

// helper function to re-caclulate load average over all threads (occurs once per second due to assumptions made)
// load_avg = (59/60)*load_avg + (1/60)*ready_threads
void thread_recalculate_load_avg(void)
{
  // only do work if mlfqs is enabled
  if (!thread_mlfqs)
    return;
  
  
  int ready_threads = list_size(&ready_list);
  if (thread_current() != idle_thread)
    ++ready_threads;
  // add currently running threads
  /*int ready_threads = 0;
  for (struct list_elem* it = list_begin(&all_list); it != list_end(&all_list); it = list_next(it))
  {
    struct thread* t = list_entry(it, struct thread, allelem);
    if (t == idle_thread)
      continue;
    bool running_or_ready = t->status == THREAD_RUNNING || t->status == THREAD_READY;
    if (running_or_ready)
      ++ready_threads;
  }*/

  // let a = (59/60)*load_avg
  //fp_real a = fp_mult(fp_div(fp_int_to_real(59), fp_int_to_real(60)), s_load_average);
  fp_real a = fp_mult(fp_int_to_real(59), s_load_average) / 60;
  // let b = (1/60)*ready_threads
  //fp_real b = fp_mult(fp_div(fp_int_to_real(1), fp_int_to_real(60)), fp_int_to_real(ready_threads));
  fp_real b = fp_int_to_real(ready_threads) / 60;
  // load_avg = a + b
  s_load_average = a + b;
}

// helper function to compute the number of ready and running threads
int get_ready_thread_count(void)
{
  int ready_threads = 0;
  for (struct list_elem* it = list_begin(&all_list); it != list_end(&all_list); it = list_next(it))
  {
    struct thread* t = list_entry(it, struct thread, allelem);
    if (t == idle_thread)
      continue;
    bool running_or_ready = t->status == THREAD_RUNNING || t->status == THREAD_READY;
    if (running_or_ready)
      ++ready_threads;
  }

  return ready_threads;
}


// helper function that adds element to ready queue in sorted order
void add_to_ready_queue(struct thread* t)
{
  // check for programmer error
  if (t == idle_thread)
    return;
  
  list_insert_ordered(&ready_list, &t->elem, &compare_threads_by_priority, NULL);
  t->status = THREAD_READY;
}

// helper function to get the *modified* priority of a thread (higher value between donations and base)
int get_modified_priority_of_thread(const struct thread* t)
{
  // do not do unnecessary work if we are using mlfqs (which has separate priority calculations)
  if (thread_mlfqs)
    return t->priority;

  int highest_prio = t->priority;
  if (list_empty(&t->m_donatees))
    return highest_prio;

  // iterate donor list and get the highest priority
  for (const struct list_elem* it = list_cbegin(&t->m_donatees); it != list_cend(&t->m_donatees); it = list_cnext(it))
  {
    const struct thread* donor = list_entry(it, struct thread, m_donor_elem);
    int prio = get_modified_priority_of_thread(donor);
    if (prio > highest_prio)
      highest_prio = prio;
  }

  return highest_prio;
}

// function to compare the priority of two threads (which are elements of the ready list)
bool compare_threads_by_priority(
  const struct list_elem* thread_elem1, 
  const struct list_elem* thread_elem2,
  __attribute__((unused)) void* aux)
{
  // get thread pointers using list_entry macro
  const struct thread* thread1 = list_entry(thread_elem1, struct thread, elem);
  const struct thread* thread2 = list_entry(thread_elem2, struct thread, elem);

  // compare priorities and donations
  return get_modified_priority_of_thread(thread1) <= get_modified_priority_of_thread(thread2);
}

// helper function that flags the ready list as "dirty", which indicates that it is *probably* not
// in sorted order anymore due to donated priorities
void set_ready_list_dirty(bool dirty)
{
  s_ready_list_dirty = dirty;
}

// requeue thread which was waiting for a lock, but is no longer waiting for the lock
// used in synch.c:donate_priority_recursive()
void thread_requeue_after_lock_release(struct thread* t)
{
  list_remove(&t->elem);
  list_insert_ordered(&ready_list, &t->elem, compare_threads_by_priority, NULL);
}


/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);

  s_load_average = fp_int_to_real(0);
  s_ready_list_dirty = false;

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();
  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  // update recent cpu time
  if (thread_mlfqs)
  {
    if (t != idle_thread)
        t->m_recent_cpu = fp_add(t->m_recent_cpu, 1);

    // update load and recent cpu once per second
    if (timer_ticks() % TIMER_FREQ == 0)
    {
      // do load average calculation
      thread_recalculate_load_avg();
      // do recent cpu
      thread_recalculate_recent_cpu();
    }

    // recalculate priorities every four ticks for mlfqs priority
    // offset priority calculation to *hopefully* help with load average
    if (thread_ticks % TIME_SLICE == 0)
      thread_calculate_priority(t);
  }
  
  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  if (thread_mlfqs)
    thread_calculate_priority(t);

  /* Add to run queue. */
  thread_unblock (t);
  // see if the newly created thread is higher priority, and try schedule
  thread_yield();

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  //list_push_back (&ready_list, &t->elem);
  // insert thread back into the ready list in order
  add_to_ready_queue(t);
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
  {
    //list_push_back (&ready_list, &cur->elem);
    // insert thread back into the ready list in order
    add_to_ready_queue(cur);
  }
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  struct thread* current_thread = thread_current();
  current_thread->priority = new_priority;
  thread_yield();
}

void thread_donate_priority(struct thread* receiver, struct thread* donor)
{
  if (donor->m_donated)
    return;

  // check for programmer error
  ASSERT(receiver != donor);
  // add to donator list
  //enum intr_level old_level = intr_disable();
  list_push_back(&receiver->m_donatees, &donor->m_donor_elem);
  donor->m_donated = true;
  //intr_set_level(old_level);
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  const struct thread* t = thread_current();

  // mlfqs has separate priority calc
  if (thread_mlfqs)
    return t->priority;

  // return the greater value between the base priority and donated priorities
  return get_modified_priority_of_thread(t);
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice) 
{
  // set the current thread to have a new nice value
  struct thread* cur = thread_current();
  cur->m_nice_value = nice;

  // recalculate thead's priority
  thread_calculate_priority(cur);

  //if (!list_empty(&ready_list))
  //  list_sort(&ready_list, compare_threads_by_priority, NULL);

  // yield so if there is a new, higher priority thread, the current thread preempts
  thread_yield();
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  struct thread* cur = thread_current();
  return cur->m_nice_value;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  return fp_real_to_int_nearest(s_load_average * 100) ;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  struct thread* cur = thread_current();
  return fp_real_to_int_nearest(cur->m_recent_cpu * 100);
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->m_recent_cpu = fp_int_to_real(0);         // initialize recent cpu
  t->m_nice_value = 0;                         // initialize nice value
  t->m_waiting_for_lock = NULL;                // pointer to lock which is blocking thread
  t->m_donated = false;
  sema_init(&(t->m_sleep_timer_semaphore), 0); // initialize wait semaphore for sleep timer
  list_init(&t->m_donatees);                   // initialize donatees list
  t->magic = THREAD_MAGIC;

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
  {
    /*if (s_ready_list_dirty)
    {
      list_sort(&ready_list, &compare_threads_by_priority, NULL);
      s_ready_list_dirty = false;
    }*/

    return list_entry (list_pop_back (&ready_list), struct thread, elem);
  }
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
