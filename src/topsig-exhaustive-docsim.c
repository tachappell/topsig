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

typedef struct {
  int header_size;
  int max_name_len;
  int sig_width;
  int sig_offset;
  int sig_record_size;
  int num_signatures;
} SignatureHeader;

static SignatureHeader readSigHeader(FILE *fp)
{
  SignatureHeader cfg;
  char sig_method[64];
  cfg.header_size = file_read32(fp); // header_size
  int version = file_read32(fp); // version
  cfg.max_name_len = file_read32(fp); // max_name_len
  cfg.sig_width = file_read32(fp); // sig_width
  file_read32(fp); // sig_density
  if (version >= 2) {
    file_read32(fp); // sig_seed
  }
  fread(sig_method, 1, 64, fp); // sig_method
    
  cfg.sig_offset = cfg.max_name_len + 1;
  cfg.sig_offset += 8 * 4; // 8 32-bit ints
  cfg.sig_record_size = cfg.sig_offset + cfg.sig_width / 8;
  
  cfg.num_signatures = -1; // unknown thus far
  
  return cfg;
}

static SignatureHeader Read_Signature_File(const char *path, unsigned char **buf)
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

typedef struct {
  int results;
  int *issl_scores;
  int *distances;
  int *docids;
  int *offset_begins;
  int *offset_ends;
  const char **docnames;
} ResultList;

static ResultList Create_Result_List(int k)
{
  ResultList R;
  R.results = k;
  size_t buffer_size = sizeof(int) * k;
  R.issl_scores = malloc(buffer_size);
  R.distances = malloc(buffer_size);
  R.offset_begins = malloc(buffer_size);
  R.offset_ends = malloc(buffer_size);
  R.docids = malloc(buffer_size);
  R.docnames = malloc(sizeof(char *) * k);
  
  memset(R.issl_scores, 0, buffer_size);
  memset(R.distances, 0, buffer_size);
  memset(R.docids, 0, buffer_size);
  
  return R;
}

static void Destroy_Result_List(ResultList *R)
{
  free(R->issl_scores);
  free(R->distances);
  free(R->docids);
  free(R->docnames);
}


typedef struct {
  ResultList *list;
  int i;
} ClarifyEntry;

static int clarify_compar(const void *A, const void *B)
{
  const ClarifyEntry *a = A;
  const ClarifyEntry *b = B;
  
  const ResultList *list = a->list;
  
  return list->distances[a->i] - list->distances[b->i];
}

static void Clarify_Results(const SignatureHeader *cfg, ResultList *list, const unsigned char *sig_file)
{
  static unsigned char *mask = NULL;
  if (!mask) {
    mask = malloc(cfg->sig_width / 8);
    memset(mask, 0xFF, cfg->sig_width / 8);
  }
  
  //list->docnames = malloc(sizeof(char *) * list->results);
  
  ClarifyEntry *clarify = malloc(sizeof(ClarifyEntry) * list->results);
  
  for (int i = 0; i < list->results; i++) {
//    list->distances[i] = DocumentDistance(cfg->sig_width, sig, mask, cursig);
    list->distances[i] = cfg->sig_width - list->issl_scores[i];
    list->docnames[i] = (const char *)(sig_file + ((size_t)cfg->sig_record_size * list->docids[i]));
    list->offset_begins[i] = *(int *)(sig_file + ((size_t)cfg->sig_record_size * list->docids[i]) + cfg->max_name_len + 1 + 4*4);
    list->offset_ends[i] = *(int *)(sig_file + ((size_t)cfg->sig_record_size * list->docids[i]) + cfg->max_name_len + 1 + 5*4);
    
    clarify[i].list = list;
    clarify[i].i = i;
  }
  qsort(clarify, list->results, sizeof(ClarifyEntry), clarify_compar);
  
  ResultList newlist = Create_Result_List(list->results);  
  //newlist.docnames = malloc(sizeof(char *) * list->results);
  for (int i = 0; i < list->results; i++) {
    int j = clarify[i].i;
    newlist.issl_scores[i] = list->issl_scores[j];
    newlist.distances[i] = list->distances[j];
    newlist.offset_begins[i] = list->offset_begins[j];
    newlist.offset_ends[i] = list->offset_ends[j];
    newlist.docids[i] = list->docids[j];
    newlist.docnames[i] = list->docnames[j];
  }
  
  Destroy_Result_List(list);
  *list = newlist;
  
  free(clarify);
}


static void Output_Results(int topic_id, const ResultList *list)
{
  for (int i = 0; i < list->results; i++) {
    //printf("%d. %d (%d) - %s\n", i, list->docids[i], list->distances[i], list->docnames[i]);
    //printf("%d Q0 %s %d %d Topsig-Exhaustive\n", topic_id, list->docnames[i], i + 1, 1000000 - i);
    
    //printf("%d Q0 %s %d %d Topsig-Exhaustive %d\n", topic_id, list->docnames[i], i + 1, 1000000 - i, list->distances[i]);
    printf("%d Q0 %s %d %d Topsig-Exhaustive %d %d %d\n", topic_id, list->docnames[i], i + 1, 1000000 - i, list->distances[i], list->offset_begins[i], list->offset_ends[i]);
  }
}

typedef struct {
  const SignatureHeader *sig_cfg;
  const unsigned char *sig_file;
  int doc_begin;
  int doc_end;
  ResultList *output;
} Worker_Throughput;

static void *Throughput_Job(void *input)
{
  int topk = atoi(Config("SEARCH-DOC-TOPK"));
  Worker_Throughput *T = input;
  const SignatureHeader *sig_cfg = T->sig_cfg;
  
  unsigned char *mask = NULL;
  mask = malloc(sig_cfg->sig_width / 8);
  memset(mask, 0xFF, sig_cfg->sig_width / 8);
  
  int doc_count = T->doc_end - T->doc_begin;
  
  const unsigned char *sig_file = T->sig_file;
  T->output = malloc(sizeof(ResultList) * doc_count);
  
  int doc_i = 0;
  for (int doc_cmp = T->doc_begin; doc_cmp < T->doc_end; doc_cmp++) {
    const unsigned char *sig = sig_file + (size_t)sig_cfg->sig_record_size * doc_cmp + sig_cfg->sig_offset;
    
    
    T->output[doc_i] = Create_Result_List(topk);
    ResultList *R = T->output+doc_i;
    int R_lowest_score = 0;
    int R_lowest_score_i = 0;
  
    for (int cmp_to = 0; cmp_to < sig_cfg->num_signatures; cmp_to++) {
      //Traverse_ISSL(issl_counts[slice], issl_table[slice], &scores, variants, n_variants_ceasenew, n_variants_stopearly, val, width);
      int docid = cmp_to;
      const unsigned char *cursig = sig_file + (size_t)sig_cfg->sig_record_size * cmp_to + sig_cfg->sig_offset;
      int score = sig_cfg->sig_width - DocumentDistance(sig_cfg->sig_width, sig, mask, cursig);
      
      if (score > R_lowest_score) {
        R->docids[R_lowest_score_i] = docid;
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
    
    Clarify_Results(sig_cfg, R, sig_file);
    
    doc_i++;
  }
  
  free(mask);
  
  return NULL;
}

void RunExhaustiveDocsimSearch()
{
  unsigned char *sig_file;
  SignatureHeader sig_cfg = Read_Signature_File(Config("SIGNATURE-PATH"), &sig_file);
  
  int thread_count = 1;
  int search_doc_first = 0;
  int search_doc_last = 9999;
  if (Config("SEARCH-DOC-THREADS")) {
    thread_count = atoi(Config("SEARCH-DOC-THREADS"));
  }
  if (Config("SEARCH-DOC-FIRST"))
    search_doc_first = atoi(Config("SEARCH-DOC-FIRST"));
  if (Config("SEARCH-DOC-LAST"))
    search_doc_last = atoi(Config("SEARCH-DOC-LAST"));
  int total_docs = search_doc_last - search_doc_first + 1;
  void **threads = malloc(sizeof(void *) * thread_count);
  for (int i = 0; i < thread_count; i++) {
    Worker_Throughput *thread_data = malloc(sizeof(Worker_Throughput));
    thread_data->sig_cfg = &sig_cfg;
    thread_data->sig_file = sig_file;
    thread_data->doc_begin = total_docs * i / thread_count + search_doc_first;
    thread_data->doc_end = total_docs * (i+1) / thread_count + search_doc_first;
    threads[i] = thread_data;
  }
  
  timer T = timer_start();
  
  DivideWork(threads, Throughput_Job, thread_count);
  
  for (int i = 0; i < thread_count; i++) {
    Worker_Throughput *thread_data = threads[i];
    int doc_count = thread_data->doc_end - thread_data->doc_begin;
    for (int j = 0; j < doc_count; j++) {
      Output_Results(thread_data->doc_begin + j, &thread_data->output[j]);
    }
  }
  
  fprintf(stderr, "search time %.2fms\n", timer_tick(&T));
  
  for (int i = 0; i < thread_count; i++) {
    free(threads[i]);
  }
  free(threads);
}

