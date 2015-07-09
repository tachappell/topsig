#ifndef TOPSIG_SEMAPHORE_H
#define TOPSIG_SEMAPHORE_H

typedef struct {
  volatile int val;
} TSemaphore;

void tsem_init(TSemaphore *S, int compat, int val);
void tsem_wait(TSemaphore *S);
int tsem_trywait(TSemaphore *S);
void tsem_post(TSemaphore *S);
void tsem_getvalue(TSemaphore *S, int *out);

#endif
