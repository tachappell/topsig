#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "topsig-global.h"
#include "topsig-issl.h"
#include "topsig-timer.h"
#include "topsig-config.h"
#include "topsig-search.h"
#include "topsig-thread.h"
#include "topsig-resultwriter.h"

typedef struct {
  int header_size;
  int max_name_len;
  int sig_width;
  int sig_offset;
  int sig_record_size;
  int num_signatures;
} SignatureHeader;

typedef struct {
  const char *docname;
  
  int uniqueTerms;
  int documentCharLength;
  int totalTerms;
  int quality;
  int offsetBegin;
  int offsetEnd;
  int unused7;
  int unused8;
} Metadata;

typedef struct {
  int results;
  int *issl_scores;
  int *distances;
  int *docids;
  Metadata *metadata;
} ResultList;

typedef struct {
  ResultList *list;
  int i;
} ClarifyEntry;

typedef struct {
  const SignatureHeader *sig_cfg;
  const unsigned char *sigFile;
  int doc_begin;
  int doc_end;
  ResultList *output;
} WorkerThroughput;

static SignatureHeader readSigHeader(FILE *fp)
{
  SignatureHeader cfg;
  char sig_method[64];
  cfg.header_size = fileRead32(fp); // header_size
  int version = fileRead32(fp); // version
  cfg.max_name_len = fileRead32(fp); // max_name_len
  cfg.sig_width = fileRead32(fp); // sig_width
  fileRead32(fp); // sig_density
  if (version >= 2) {
    fileRead32(fp); // sig_seed
  }
  fread(sig_method, 1, 64, fp); // sig_method

  cfg.sig_offset = cfg.max_name_len + 1;
  cfg.sig_offset += 8 * 4; // 8 32-bit ints
  cfg.sig_record_size = cfg.sig_offset + cfg.sig_width / 8;

  cfg.num_signatures = -1; // unknown thus far

  return cfg;
}

static SignatureHeader readSigFile(const char *path, unsigned char **buf)
{
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    fprintf(stderr, "Unable to open signature file for reading.\n");
    exit(1);
  }

  SignatureHeader cfg = readSigHeader(fp);

  unsigned char *dummy_buffer = malloc((size_t)cfg.sig_record_size);
  int sigs = 0;
  for (;;) {
    if (fread(dummy_buffer, cfg.sig_record_size, 1, fp) < 1) break;
    sigs++;
  }
  free(dummy_buffer);
  fseek(fp, cfg.header_size, SEEK_SET);

  cfg.num_signatures = sigs;
  unsigned char *buffer = malloc((size_t)cfg.sig_record_size * sigs);
  if (!buffer) {
    double mb = (double)((size_t)cfg.sig_record_size * sigs) / 1048576.0;
    fprintf(stderr, "Error: unable to allocate memory to store signature file (%.2f MB)\n", mb);
    exit(1);
  }

  fread(buffer, cfg.sig_record_size, sigs, fp);
  fclose(fp);
  *buf = buffer;
  return cfg;
}

static ResultList createResultList(int k)
{
  ResultList R;
  R.results = k;
  size_t buffer_size = sizeof(int) * k;
  R.issl_scores = malloc(buffer_size);
  R.distances = malloc(buffer_size);
  R.docids = malloc(buffer_size);
  R.metadata = malloc(sizeof(Metadata) * k);

  memset(R.issl_scores, 0, buffer_size);
  memset(R.distances, 0, buffer_size);
  memset(R.docids, 0, buffer_size);

  return R;
}

static void destroyResultList(ResultList *R)
{
  free(R->issl_scores);
  free(R->distances);
  free(R->docids);
  free(R->metadata);
}

static int compareResultLists(const void *A, const void *B)
{
  const ClarifyEntry *a = A;
  const ClarifyEntry *b = B;

  const ResultList *list = a->list;

  return list->distances[a->i] - list->distances[b->i];
}

static void clarifyResults(const SignatureHeader *cfg, ResultList *list, const unsigned char *sigFile)
{
  static unsigned char *mask = NULL;
  if (!mask) {
    mask = malloc(cfg->sig_width / 8);
    memset(mask, 0xFF, cfg->sig_width / 8);
  }

  ClarifyEntry *clarify = malloc(sizeof(ClarifyEntry) * list->results);

  for (int i = 0; i < list->results; i++) {
    list->distances[i] = cfg->sig_width - list->issl_scores[i];
    
    const char *sigDocname = (const char *)(sigFile + ((size_t)cfg->sig_record_size * list->docids[i]));
    
    list->metadata[i].docname = sigDocname;
    
    unsigned const char *sigMetadata = (unsigned const char *)(sigDocname + cfg->max_name_len + 1);
    
    list->metadata[i].uniqueTerms = memRead32(sigMetadata + 0*4);
    list->metadata[i].documentCharLength = memRead32(sigMetadata + 1*4);
    list->metadata[i].totalTerms = memRead32(sigMetadata + 2*4);
    list->metadata[i].quality = memRead32(sigMetadata + 3*4);
    list->metadata[i].offsetBegin = memRead32(sigMetadata + 4*4);
    list->metadata[i].offsetEnd = memRead32(sigMetadata + 5*4);
    list->metadata[i].unused7 = memRead32(sigMetadata + 6*4);
    list->metadata[i].unused8 = memRead32(sigMetadata + 7*4);

    clarify[i].list = list;
    clarify[i].i = i;
  }
  qsort(clarify, list->results, sizeof(ClarifyEntry), compareResultLists);

  ResultList newlist = createResultList(list->results);

  for (int i = 0; i < list->results; i++) {
    int j = clarify[i].i;
    newlist.issl_scores[i] = list->issl_scores[j];
    newlist.distances[i] = list->distances[j];
    newlist.docids[i] = list->docids[j];
    newlist.metadata[i] = list->metadata[j];
  }

  destroyResultList(list);
  *list = newlist;

  free(clarify);
}


static void writeResults(FILE *fp, int topicId, const char *topicName, const ResultList *list)
{
  int top_k = list->results;
  const char *format = GetOptionalConfig("RESULTS-FORMAT", "%T Q0 %D %r %s Topsig-Exhaustive %h\\n");
  for (int i = 0; i < top_k; i++) {
    WriteResult(fp, format, topicId, topicName, list->docids[i], list->metadata[i].docname, i + 1, 1000000 - i, list->distances[i], list->metadata[i].uniqueTerms, list->metadata[i].documentCharLength, list->metadata[i].totalTerms, list->metadata[i].quality, list->metadata[i].offsetBegin, list->metadata[i].offsetEnd, list->metadata[i].unused7, list->metadata[i].unused8);
  }
}


static void *Throughput_Job(void *input)
{
  int topk = atoi(Config("K"));
  WorkerThroughput *T = input;
  const SignatureHeader *sig_cfg = T->sig_cfg;

  unsigned char *mask = NULL;
  mask = malloc(sig_cfg->sig_width / 8);
  memset(mask, 0xFF, sig_cfg->sig_width / 8);

  int doc_count = T->doc_end - T->doc_begin;

  const unsigned char *sigFile = T->sigFile;
  T->output = malloc(sizeof(ResultList) * doc_count);

  int doc_i = 0;
  for (int doc_cmp = T->doc_begin; doc_cmp < T->doc_end; doc_cmp++) {
    const unsigned char *sig = sigFile + (size_t)sig_cfg->sig_record_size * doc_cmp + sig_cfg->sig_offset;


    T->output[doc_i] = createResultList(topk);
    ResultList *R = T->output+doc_i;
    int R_lowest_score = 0;
    int R_lowest_score_i = 0;

    for (int cmp_to = 0; cmp_to < sig_cfg->num_signatures; cmp_to++) {
      int docId = cmp_to;
      const unsigned char *cursig = sigFile + (size_t)sig_cfg->sig_record_size * cmp_to + sig_cfg->sig_offset;
      int score = sig_cfg->sig_width - DocumentDistance(sig_cfg->sig_width, sig, mask, cursig);

      if (score > R_lowest_score) {
        R->docids[R_lowest_score_i] = docId;
        R->issl_scores[R_lowest_score_i] = score;

        R_lowest_score = INT_MAX;
        for (int j = 0; j < R->results; j++) {
          if (R->issl_scores[j] < R_lowest_score) {
            R_lowest_score = R->issl_scores[j];
            R_lowest_score_i = j;
          }
        }
      }
    }

    clarifyResults(sig_cfg, R, sigFile);

    doc_i++;
  }

  free(mask);

  return NULL;
}

void RunExhaustiveDocsimSearch()
{
  const char *topicoutput = Config("RESULTS-PATH");
  FILE *fo;
  if (topicoutput) {
    fo = fopen(topicoutput, "wb");
    if (!fo) {
      fprintf(stderr, "The results file \"%s\" could not be opened for writing.\n", topicoutput);
      exit(1);
    }
  } else {
    fo = stdout;
  }
  
  unsigned char *sigFile;
  SignatureHeader sigCfg = readSigFile(Config("SIGNATURE-PATH"), &sigFile);

  int threadCount = 1;
  int searchDocFirst = 0;
  int searchDocLast = (sigCfg.num_signatures - 1);
  if (Config("THREADS")) {
    threadCount = atoi(Config("THREADS"));
  }
  if (Config("SEARCH-DOC-FIRST"))
    searchDocFirst = atoi(Config("SEARCH-DOC-FIRST"));
  if (Config("SEARCH-DOC-LAST"))
    searchDocLast = atoi(Config("SEARCH-DOC-LAST"));
  int totalDocs = searchDocLast - searchDocFirst + 1;
  void **threads = malloc(sizeof(void *) * threadCount);
  for (int i = 0; i < threadCount; i++) {
    WorkerThroughput *threadData = malloc(sizeof(WorkerThroughput));
    threadData->sig_cfg = &sigCfg;
    threadData->sigFile = sigFile;
    threadData->doc_begin = totalDocs * i / threadCount + searchDocFirst;
    threadData->doc_end = totalDocs * (i+1) / threadCount + searchDocFirst;
    threads[i] = threadData;
  }

  Timer T = StartTimer();

  DivideWork(threads, Throughput_Job, threadCount);

  for (int i = 0; i < threadCount; i++) {
    WorkerThroughput *thread_data = threads[i];
    int doc_count = thread_data->doc_end - thread_data->doc_begin;
    for (int j = 0; j < doc_count; j++) {
      int topicId = thread_data->doc_begin + j;
      const char *docName = (const char *)(sigFile + (size_t)sigCfg.sig_record_size * topicId);
      writeResults(fo, topicId, docName, &thread_data->output[j]);
    }
  }

  fprintf(stderr, "search time %.2fms\n", TickTimer(&T));

  for (int i = 0; i < threadCount; i++) {
    free(threads[i]);
  }
  free(threads);
}

