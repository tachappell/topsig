#ifndef TOPSIG_THREAD_H
#define TOPSIG_THREAD_H

#include "topsig-search.h"
#include "topsig-document.h"

void ThreadYield();

// Threaded indexing
void ProcessFile_Threaded(Document *);
void Flush_Threaded();

// Threaded searching
Results *FindHighestScoring_Threaded(Search *, const int, const int, const int, unsigned char *, unsigned char *, int);
Results *FindHighestScoring_Threaded_X(Search *, const int, const int, const int, unsigned char *, unsigned char *, int);

void DivideWork(void **job_inputs, void *(*start_routine)(void*), int jobs);

// Thread broadcast pool

struct TBPHandle;
typedef struct TBPHandle TBPHandle;

TBPHandle *TBPInit(int threads, void **threaddata);
void **TBPDivideWork(TBPHandle *H, void *job_input, void *(*start_routine)(void*, void*));
void TBPClose(TBPHandle *H);

// Traditional thread pool
void DivideWorkTP(void **job_inputs, void **thread_inputs, void *(*start_routine)(void*, void*), int jobs, int threads);

#endif
