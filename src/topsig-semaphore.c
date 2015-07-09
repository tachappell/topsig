#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <assert.h>
#include "topsig-semaphore.h"
#include "topsig-atomic.h"
#include "topsig-thread.h"

void tsem_init(TSemaphore *S, int compat, int val)
{
  assert(compat==0);
  S->val = val;
}

void tsem_wait(TSemaphore *S)
{
  for (;;) {
    if (S->val > 0) {
      int r = atomic_sub(&S->val, 1);
      if (r > 0) break;
      atomic_add(&S->val, 1);
    }
    ThreadYield();
  }
}

int tsem_trywait(TSemaphore *S)
{
  if (S->val > 0) {
    int r = atomic_sub(&S->val, 1);
    if (r > 0) return 0;
    atomic_add(&S->val, 1);
  }
  return -1;
}

void tsem_post(TSemaphore *S)
{
  atomic_add(&S->val, 1);
}

void tsem_getvalue(TSemaphore *S, int *out)
{
  if (S->val > 0) *out = S->val;
  else *out = 0;
  return;
}
