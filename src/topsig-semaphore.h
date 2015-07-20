#ifndef TOPSIG_SEMAPHORE_H
#define TOPSIG_SEMAPHORE_H

typedef struct {
  volatile int val;
} TSemaphore;

void InitSemaphore(TSemaphore *S, int compat, int val);
void WaitSemaphore(TSemaphore *S);
int TryWaitSemaphore(TSemaphore *S);
void PostSemaphore(TSemaphore *S);
void GetSemaphoreValue(TSemaphore *S, int *out);

#endif
