#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <assert.h>
#include "topsig-semaphore.h"
#include "topsig-atomic.h"
#include "topsig-thread.h"

void InitSemaphore(TSemaphore *S, int compat, int val)
{
  assert(compat==0);
  S->val = val;
}

void WaitSemaphore(TSemaphore *S)
{
  for (;;) {
    if (S->val > 0) {
      int r = atomicFetchAndSub(&S->val, 1);
      if (r > 0) break;
      atomicFetchAndAdd(&S->val, 1);
    }
    ThreadYield();
  }
}

int TryWaitSemaphore(TSemaphore *S)
{
  if (S->val > 0) {
    int r = atomicFetchAndSub(&S->val, 1);
    if (r > 0) return 0;
    atomicFetchAndAdd(&S->val, 1);
  }
  return -1;
}

void PostSemaphore(TSemaphore *S)
{
  atomicFetchAndAdd(&S->val, 1);
}

void GetSemaphoreValue(TSemaphore *S, int *out)
{
  if (S->val > 0) *out = S->val;
  else *out = 0;
  return;
}
