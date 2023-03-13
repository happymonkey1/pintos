#include "tests/threads/tests.h"
#include <debug.h>
#include <string.h>
#include <stdio.h>

typedef void test_func (void);

struct test 
{
  const char *name;
  test_func *function;
};

static struct test tests[27] = 
{
  {.name = "alarm-single",  .function = test_alarm_single},
  {.name = "alarm-multiple", .function = test_alarm_multiple},
  {.name = "alarm-simultaneous", .function =  test_alarm_simultaneous},
  {.name = "alarm-priority", .function = test_alarm_priority},
  {.name = "alarm-zero", .function = test_alarm_zero},
  {.name = "alarm-negative", .function = test_alarm_negative},
  {.name = "priority-change", .function = test_priority_change},
  {.name = "priority-donate-one", .function = test_priority_donate_one},
  {.name = "priority-donate-multiple", .function = test_priority_donate_multiple},
  {.name = "priority-donate-multiple2", .function = test_priority_donate_multiple2},
  {.name = "priority-donate-nest", .function = test_priority_donate_nest},
  {.name = "priority-donate-sema", .function = test_priority_donate_sema},
  {.name = "priority-donate-lower", .function = test_priority_donate_lower},
  {.name = "priority-donate-chain", .function = test_priority_donate_chain},
  {.name = "priority-fifo", .function = test_priority_fifo},
  {.name = "priority-preempt", .function = test_priority_preempt},
  {.name = "priority-sema", .function = test_priority_sema},
  {.name = "priority-condvar", .function = test_priority_condvar},
  {.name = "mlfqs-load-1", .function = test_mlfqs_load_1},
  {.name = "mlfqs-load-60", .function = test_mlfqs_load_60},
  {.name = "mlfqs-load-avg", .function = test_mlfqs_load_avg},
  {.name = "mlfqs-recent-1", .function = test_mlfqs_recent_1},
  {.name = "mlfqs-fair-2", .function = test_mlfqs_fair_2},
  {.name = "mlfqs-fair-20", .function = test_mlfqs_fair_20},
  {.name = "mlfqs-nice-2", .function = test_mlfqs_nice_2},
  {.name = "mlfqs-nice-10", .function = test_mlfqs_nice_10},
  {.name = "mlfqs-block", .function = test_mlfqs_block},
};

static const char *test_name;

/* Runs the test named NAME. */
void
run_test (const char *name) 
{
  // for *some* reason the WHOLE TESTS STRUCT WAS NULL, lets see if initializing AGAIN fixes this issue...
  // ANOTHER POSSIBLE SOLUTION WOULD TO NOT MAKE THE TESTS ARRAY STATIC, AND INITIALIZE LOCAL ARRAY
  // I HAVE NO IDEA WHY TF THIS IS AN ISSUE, BUT IT WAS (AT LEAST ON GCC 9.4)
  int counter = 0;
  tests[counter].name = "alarm-single"; tests[counter++].function = test_alarm_single;
  tests[counter].name = "alarm-multiple"; tests[counter++].function = test_alarm_multiple;
  tests[counter].name = "alarm-simultaneous"; tests[counter++].function =  test_alarm_simultaneous;
  tests[counter].name = "alarm-priority"; tests[counter++].function = test_alarm_priority;
  tests[counter].name = "alarm-zero"; tests[counter++].function = test_alarm_zero;
  tests[counter].name = "alarm-negative"; tests[counter++].function = test_alarm_negative;
  tests[counter].name = "priority-change"; tests[counter++].function = test_priority_change;
  tests[counter].name = "priority-donate-one"; tests[counter++].function = test_priority_donate_one;
  tests[counter].name = "priority-donate-multiple"; tests[counter++].function = test_priority_donate_multiple;
  tests[counter].name = "priority-donate-multiple2"; tests[counter++].function = test_priority_donate_multiple2;
  tests[counter].name = "priority-donate-nest"; tests[counter++].function = test_priority_donate_nest;
  tests[counter].name = "priority-donate-sema"; tests[counter++].function = test_priority_donate_sema;
  tests[counter].name = "priority-donate-lower"; tests[counter++] .function = test_priority_donate_lower;
  tests[counter].name = "priority-donate-chain"; tests[counter++] .function = test_priority_donate_chain;
  tests[counter].name = "priority-fifo"; tests[counter++] .function = test_priority_fifo;
  tests[counter].name = "priority-preempt"; tests[counter++] .function = test_priority_preempt;
  tests[counter].name = "priority-sema"; tests[counter++] .function = test_priority_sema;
  tests[counter].name = "priority-condvar"; tests[counter++] .function = test_priority_condvar;
  tests[counter].name = "mlfqs-load-1"; tests[counter++] .function = test_mlfqs_load_1;
  tests[counter].name = "mlfqs-load-60"; tests[counter++] .function = test_mlfqs_load_60;
  tests[counter].name = "mlfqs-load-avg"; tests[counter++] .function = test_mlfqs_load_avg;
  tests[counter].name = "mlfqs-recent-1"; tests[counter++] .function = test_mlfqs_recent_1;
  tests[counter].name = "mlfqs-fair-2"; tests[counter++] .function = test_mlfqs_fair_2;
  tests[counter].name = "mlfqs-fair-20"; tests[counter++] .function = test_mlfqs_fair_20;
  tests[counter].name = "mlfqs-nice-2"; tests[counter++] .function = test_mlfqs_nice_2;
  tests[counter].name = "mlfqs-nice-10"; tests[counter++] .function = test_mlfqs_nice_10;
  tests[counter].name = "mlfqs-block"; tests[counter++] .function = test_mlfqs_block;
  

  const struct test *t;

  int tests_count = (int)(sizeof(tests) / sizeof(struct test));
  for (int test_index = 0; test_index < tests_count; ++test_index)
  {
    t = &tests[test_index];
    if (!strcmp (name, t->name))
      {
        test_name = name;
        msg ("begin");
        t->function ();
        msg ("end");
        return;
      }
  }
  PANIC ("no test named \"%s\"", name);
}

/* Prints FORMAT as if with printf(),
   prefixing the output by the name of the test
   and following it with a new-line character. */
void
msg (const char *format, ...) 
{
  va_list args;
  
  printf ("(%s) ", test_name);
  va_start (args, format);
  vprintf (format, args);
  va_end (args);
  putchar ('\n');
}

/* Prints failure message FORMAT as if with printf(),
   prefixing the output by the name of the test and FAIL:
   and following it with a new-line character,
   and then panics the kernel. */
void
fail (const char *format, ...) 
{
  va_list args;
  
  printf ("(%s) FAIL: ", test_name);
  va_start (args, format);
  vprintf (format, args);
  va_end (args);
  putchar ('\n');

  PANIC ("test failed");
}

/* Prints a message indicating the current test passed. */
void
pass (void) 
{
  printf ("(%s) PASS\n", test_name);
}

