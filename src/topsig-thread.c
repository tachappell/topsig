#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "topsig-thread.h"

// NO_THREADING is an optional mode to disable the pthread dependency.
// Naturally attempting to do a multithreaded run will fail if this is the case.

#ifdef NO_THREADING

void ProcessFile_Threaded(char *arg_filename, char *arg_filedat)
{
  fprintf(stderr, "Error: Threading disabled. Change INDEX-THREADING to single or compile in threading support.\n");
  exit(1);
}
void Flush_Threaded() { SignatureFlush(); }
void ThreadYield(){}

Results *FindHighestScoring_Threaded(Search *S, const int start, const int count, const int topk, unsigned char *bsig, unsigned char *bmask)
{
  fprintf(stderr, "Error: Threading disabled. Change SEARCH-THREADING to single or compile in threading support.\n");
  exit(1);
  return NULL;
}

void DivideWork(void **job_inputs, void *(*start_routine)(void*), int jobs)
{
  fprintf(stderr, "Error: Threading disabled. Change SEARCH-THREADING to single or compile in threading support.\n");
  exit(1);
  return NULL;
}

struct {
  void (*func)(void);
} CallOnce_list[CALLONCE_BUFFER]; // will be init with {NULL}

void CallOnce(void (*func)())
{
  for (int i = 0; i < CALLONCE_BUFFER; i++) {
    if (CallOnce_list[i].func == func) {
      return;
    }
    if (CallOnce_list[i].func == NULL) {
      CallOnce_list[i].func = func;
      func();
      return;
    }
  }
}

#else
#endif /* NO_THREADING */

#include <string.h>
#include <pthread.h>
#include <sched.h>
#include "topsig-config.h"
#include "topsig-process.h"
#include "topsig-atomic.h"
#include "topsig-global.h"
#include "topsig-search.h"
#include "topsig-signature.h"
#include "topsig-semaphore.h"
#include "topsig-document.h"

#define JOB_POOL 512

TSemaphore sem_jobs_ready;
TSemaphore sem_job_avail[JOB_POOL];

struct thread_job {
  enum {EMPTY, READY, TAKEN} state;
  int owner;
  Document *doc;
};

volatile int jobs_start = -1;
volatile int jobs_end;
volatile int current_jobs;
volatile int jobs_complete;
volatile int threads_running;
volatile int finishup;
volatile struct thread_job jobs[JOB_POOL];

int threadpool_size = 0;
pthread_t *threadpool;
SignatureCache **threadcache;

void ThreadYield()
{
  sched_yield();
}

void *start_work_writer(void *sigcache_ptr)
{
  assert(sigcache_ptr);
  for (;;) {
    if (finishup) break;
    SignatureFlush();
    ThreadYield();
  }
  return NULL;
}

void *start_work(void *sigcache_ptr)
{
  SignatureCache *C = sigcache_ptr;
  int threadID = atomic_add(&threads_running, 1);
  
  for (;;) {
    tsem_wait(&sem_jobs_ready);
    if (finishup) break;
    
    int currjob = atomic_add(&jobs_start, 1) % JOB_POOL;

    jobs[currjob].state = TAKEN;
    jobs[currjob].owner = threadID;
    ProcessFile(C, jobs[currjob].doc);
    
    jobs[currjob].state = EMPTY;
  
    atomic_add(&jobs_complete, 1);
    atomic_sub(&current_jobs, 1);
    tsem_post(&sem_job_avail[currjob]);
  }
  
  //printf("%d> __DONE__\n", my_tid);
  
  atomic_sub(&threads_running, 1);
  //pthread_exit(NULL);
  return NULL;
}

void ProcessFile_Threaded(Document *doc)
{
  if (jobs_start == -1) {
    threads_running = 0;
    memset((void *)jobs, 0, sizeof(jobs));
    
    tsem_init(&sem_jobs_ready, 0, 0);
    for (int i = 0; i < JOB_POOL; i++) {
      tsem_init(&sem_job_avail[i], 0, 1);
    }

    threadpool_size = atoi(Config("INDEX-THREADS"))+1;
    threadpool = malloc(sizeof(pthread_t) * threadpool_size);
    threadcache = malloc(sizeof(SignatureCache *) * threadpool_size);

    jobs_start = 0;
    jobs_end = 0;
    current_jobs = 0;
    jobs_complete = 0;
    finishup = 0;

    threadcache[0] = NewSignatureCache(2, 0);
    pthread_create(threadpool+0, NULL, start_work_writer, threadcache[0]);
    for (int i = 1; i < threadpool_size; i++) {
      threadcache[i] = NewSignatureCache(0, 1);
      pthread_create(threadpool+i, NULL, start_work, threadcache[i]);
    }
  }
  int currjob = atomic_add(&jobs_end, 1) % JOB_POOL;

  tsem_wait(&sem_job_avail[currjob]);

  jobs[currjob].doc = doc;
  jobs[currjob].state = READY;

  atomic_add(&current_jobs, 1);
  tsem_post(&sem_jobs_ready);
  //printf("Job %d ready.\n", currjob);
  
}

void Flush_Threaded()
{
  if (jobs_start == -1) { // single-threaded
    SignatureFlush();
    return;
  } else {
    int jobs_ready = 1;
    while (jobs_ready > 0) {
      tsem_getvalue(&sem_jobs_ready, &jobs_ready);
      ThreadYield();
    }
    if (jobs_start != -1) {
      finishup = 1;
      
      for (int i = 0; i < threadpool_size; i++) {
        tsem_post(&sem_jobs_ready);
      }
      for (int i = 0; i < threadpool_size; i++) {
        pthread_join(threadpool[i], NULL);
      }
      SignatureFlush();
    }
    return;
  }
}

struct searchthread_params {
  Search *S;
  int start;
  int count;
  int topk;
  unsigned char *bsig;
  unsigned char *bmask;
};

void *FindHighestScoring_work(void *param)
{
  struct searchthread_params *P = param;
  
  return FindHighestScoring(P->S, P->start, P->count, P->topk, P->bsig, P->bmask);
}


Results *FindHighestScoring_Threaded(Search *S, const int start, const int count, const int topk, unsigned char *bsig, unsigned char *bmask, int threadcount)
{
  pthread_t searchthreads[threadcount];
  struct searchthread_params params[threadcount];
  for (int i = 0; i < threadcount; i++) {
    params[i].S = S;
    params[i].topk = topk;
    params[i].bsig = bsig;
    params[i].bmask = bmask;
    
    params[i].start = (count / threadcount * i) + start;
    params[i].count = count / threadcount;
    if (i == (threadcount - 1)) {
      // Add remaining to the last thread
      params[i].count += count - (count / threadcount * threadcount);
    }
    pthread_create(searchthreads+i, NULL, FindHighestScoring_work, params+i);
  }
  
  Results *R = NULL;
  Results *newR = NULL;
  for (int i = 0; i < threadcount; i++) {
    void *newR_void = newR;
    pthread_join(searchthreads[i], &newR_void);
    newR = newR_void;
    if (R) {
      MergeResults(R, newR);
    } else {
      R = newR;
    }
  }
  return R;
}

// Thread-pooling version of FindHighestScoring_Threaded

struct searchthread_params_x_shared {
  int start;
  int count;
  
  int i;
  int n;
  int tn;
};

struct searchthread_params_x {
  Search *S;
  struct searchthread_params_x_shared *shared;
  int topk;
  unsigned char *bsig;
  unsigned char *bmask;
};

void *FindHighestScoring_work_x(void *param)
{
  struct searchthread_params_x *P = param;
  Results *R = InitialiseResults(P->S, P->topk);
  int tn = atomic_add(&P->shared->tn, 1);
  for (;;) {
    int my_job = atomic_add(&P->shared->i, 1);
    if (my_job >= P->shared->n) break;
    
    int start = (long long)my_job * P->shared->count / P->shared->n;
    int count = (long long)(my_job + 1) * P->shared->count / P->shared->n - start;
    
    //fprintf(stderr, "Thread %d, job %d, %d->%d\n", tn, my_job, start, start+count-1);
    FindHighestScoring_ReuseResults(P->S, R, start, count, P->topk, P->bsig, P->bmask);
  }
  
  return R;
}

Results *FindHighestScoring_Threaded_X(Search *S, const int start, const int count, const int topk, unsigned char *bsig, unsigned char *bmask, int threadcount)
{
  pthread_t searchthreads[threadcount];
  struct searchthread_params_x params[threadcount];
  struct searchthread_params_x_shared *shared = malloc(sizeof(struct searchthread_params_x_shared));
  
  shared->start = start;
  shared->count = count;
  shared->i = 0;
  shared->n = atoi(Config("SEARCH-JOBS"));
  shared->tn = 0;
  for (int i = 0; i < threadcount; i++) {
    params[i].S = S;
    params[i].topk = topk;
    params[i].bsig = bsig;
    params[i].bmask = bmask;
    
    params[i].shared = shared;
    
    pthread_create(searchthreads+i, NULL, FindHighestScoring_work_x, params+i);
  }
  
  Results *R = NULL;
  Results *newR = NULL;
  for (int i = 0; i < threadcount; i++) {
    void *newR_void = newR;
    pthread_join(searchthreads[i], &newR_void);
    newR = newR_void;
    if (R) {
      MergeResults(R, newR);
    } else {
      R = newR;
    }
  }
  return R;
}

// Generic job-splitting routine
void DivideWork(void **job_inputs, void *(*start_routine)(void*), int jobs)
{
  pthread_t workthreads[jobs];

  for (int i = 0; i < jobs; i++) {
    pthread_create(workthreads+i, NULL, start_routine, job_inputs[i]);
  }
  
  for (int i = 0; i < jobs; i++) {
    pthread_join(workthreads[i], NULL);
  }
}

// Thread broadcast pool. Gives each job to every thread in the pool when used.
// This assumes threads will divide up the work themselves.

typedef struct {
  void *threaddata;
  TBPHandle *H;
  void *result;
} TBPDaemon;

struct TBPHandle {
  int threads;
  pthread_t *workthreads;
  
  TBPDaemon **daemons;
  
  int current_task;
  void *current_task_data;
  void *(*current_task_start_routine)(void*, void*);
  int jobs_complete;
  
  void **results;
};



void *tbp_thread_daemon(void *threaddata_vp) {
  TBPDaemon *D = threaddata_vp;
  
  int mytask = 0;
  
  for (;;) {
    // Wait for a new task
    int newtask;
    while (atomic_cas(&D->H->current_task, mytask+1, mytask+1) == 0) {
      ThreadYield();
    }
    newtask = D->H->current_task;
    if (newtask == -1) { // Terminate
      break;
    }
    // new task
    mytask = newtask;
    D->result = D->H->current_task_start_routine(D->threaddata, D->H->current_task_data);
    atomic_add(&D->H->jobs_complete, 1);
    
    // and wait for a new task.
  }
  return NULL;
}

TBPHandle *TBPInit(int threads, void **threaddata)
{
  TBPHandle *H = malloc(sizeof(TBPHandle));
  H->threads = threads;
  H->workthreads = malloc(sizeof(pthread_t) * threads);
  H->current_task = 0;
  H->daemons = malloc(sizeof(TBPDaemon *) * threads);
  H->results = malloc(sizeof(void *) * threads);
  for (int i = 0; i < threads; i++) {
    TBPDaemon *D = malloc(sizeof(TBPDaemon));
    D->threaddata = threaddata[i];
    D->H = H;
    pthread_create(H->workthreads+i, NULL, tbp_thread_daemon, D);
    H->daemons[i] = D;
  }
  return H;
}

void **TBPDivideWork(TBPHandle *H, void *job_input, void *(*start_routine)(void*, void*))
{
  H->jobs_complete = 0;
  H->current_task_data = job_input;
  H->current_task_start_routine = start_routine;
  atomic_add(&H->current_task, 1);
  
  while (!atomic_cas(&H->jobs_complete, H->threads, 0)) {
    //fprintf(stderr, "[%d %d]\n", H->jobs_complete, cmp);
    ThreadYield();
  }
  
  for (int i = 0; i < H->threads; i++) {
    H->results[i] = H->daemons[i]->result;
  }
  
  return H->results;
  
}

void TBPClose(TBPHandle *H)
{
  atomic_cas(&H->current_task, H->current_task, -1);
  
  for (int i = 0; i < H->threads; i++) {
    pthread_join(H->workthreads[i], NULL);
    
    free(H->daemons[i]);
  }
  free(H->daemons);
  free(H->results);
  free(H->workthreads);
  free(H);
}

// New thread pool implementation, because why not have like 5 of these?

typedef struct {
  pthread_mutex_t *lock;
  int jobs;
  void **job_inputs;
  void *thread_input;
  int *job_statuses;
  void *(*start_routine)(void*,void*);
} DWTP;

void *DWTP_Worker(void *in) {
  DWTP *dwtp = in;
  
  for (;;) {
    pthread_mutex_lock(dwtp->lock);
    int my_job = -1;
    for (int i = 0; i < dwtp->jobs; i++) {
      if (dwtp->job_statuses[i] == 0) {
        my_job = i;
        dwtp->job_statuses[i] = 1;
        break;
      }
    }
    pthread_mutex_unlock(dwtp->lock);
    if (my_job == -1) break;
    dwtp->start_routine(dwtp->job_inputs[my_job], dwtp->thread_input);
  }
  
  return NULL;
}

// Generic job-splitting routine
void DivideWorkTP(void **job_inputs, void **thread_inputs, void *(*start_routine)(void*,void*), int jobs, int threads)
{
  pthread_t workthreads[threads];
  DWTP dwtp;
  dwtp.lock = malloc(sizeof(pthread_mutex_t));
  pthread_mutex_init(dwtp.lock, NULL);
  dwtp.jobs = jobs;
  dwtp.job_inputs = job_inputs;
  dwtp.thread_input = NULL;
  dwtp.job_statuses = malloc(sizeof(int) * jobs);
  dwtp.start_routine = start_routine;
  
  for (int i = 0; i < jobs; i++) {
    dwtp.job_statuses[i] = 0;
  }
  DWTP per_dwtp[threads];
  for (int i = 0; i < threads; i++) {
    per_dwtp[i] = dwtp;
    per_dwtp[i].thread_input = thread_inputs[i];
    pthread_create(workthreads+i, NULL, DWTP_Worker, &per_dwtp[i]);
  }
  
  for (int i = 0; i < threads; i++) {
    pthread_join(workthreads[i], NULL);
  }
  free(dwtp.job_statuses);
  free(dwtp.lock);
}
