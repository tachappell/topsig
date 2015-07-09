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
#include "uthash.h"

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
  const char **docnames;
} ResultList;

static ResultList Create_Result_List(int k)
{
  ResultList R;
  R.results = k;
  size_t buffer_size = sizeof(int) * k;
  R.issl_scores = malloc(buffer_size);
  R.distances = malloc(buffer_size);
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
    printf("%d Q0 %s %d %d Topsig-Exhaustive %d\n", topic_id, list->docnames[i], i + 1, 1000000 - i, list->distances[i]);
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

typedef struct {
  char fname[128];
  int hd;
  int topic;
  
  UT_hash_handle hh;
} ResultHash;

typedef struct {
  char docid[128];  
  UT_hash_handle hh;
} categorised_doc;

typedef struct {
  char catname[256];
  categorised_doc *dochash;
  UT_hash_handle hh;
} category;

void HistogramAdd(SignatureHeader *sig_cfg, const unsigned char *sig_file, int *histogram, const unsigned char *sig, const unsigned char *mask, ResultHash *H, int topic_only, int start_from, int must_be_in_table, categorised_doc *dochash, const char *sig_fname)
{
  categorised_doc *D1, *D2;
  HASH_FIND_STR(dochash, sig_fname, D1);
  if (dochash && D1==NULL) return;
  for (int i = start_from; i < sig_cfg->num_signatures; i++) {
    const unsigned char *cursig = sig_file + (size_t)sig_cfg->sig_record_size * i + sig_cfg->sig_offset;
    int hd = DocumentDistance(sig_cfg->sig_width, sig, mask, cursig);
    const char *fname = (const char *)(sig_file + (size_t)sig_cfg->sig_record_size * i);
    if (dochash) {
      HASH_FIND_STR(dochash, fname, D2);
      if (D2 == NULL)
        continue;
      //fprintf(stderr, "both %s and %s are part of cat\n", sig_fname, fname);
    }
    if (H) {
      ResultHash *f = NULL;
      
      HASH_FIND_STR(H, fname, f);
      if (f && (f->topic == topic_only)) {
        if (must_be_in_table)
          histogram[hd]++;
        if (f->hd > hd) {
          f->hd = hd;
        }
      }
      if (!must_be_in_table)
        histogram[hd]++;
    } else { // H == NULL
      histogram[hd]++;
    }
  }
}

//
// HISTOGRAM-TYPE  "doc" / "query"
// HISTOGRAM-SOURCE (number of signatures from doc) or (query)



void RunHistogram()
{
  ResultHash *H = NULL;
  unsigned char *sig_file;
  SignatureHeader sig_cfg = Read_Signature_File(Config("SIGNATURE-PATH"), &sig_file);
  size_t histogram_size = sizeof(int) * sig_cfg.sig_width;
  int *histogram = malloc(histogram_size);
  memset(histogram, 0x00, histogram_size);
  
  category *cathash = NULL;
  categorised_doc *dochash = NULL;
  if (Config("HISTOGRAM-CATFILE")) {
    FILE *fp = fopen(Config("HISTOGRAM-CATFILE"), "r");
    char docid[128];
    char catname[256];
    
    while (fscanf(fp, "%s %[^\n]\n", docid, catname) == 2) {
      category *C;
      HASH_FIND_STR(cathash, catname, C);
      if (!C) {
        C = malloc(sizeof(category));
        strcpy(C->catname, catname);
        C->dochash = NULL;
        HASH_ADD_STR(cathash, catname, C);
      }
      categorised_doc *D;
      HASH_FIND_STR(C->dochash, docid, D);
      if (!D) {
        D = malloc(sizeof(categorised_doc));
        strcpy(D->docid, docid);
        HASH_ADD_STR(C->dochash, docid, D);
      }
    }
    fclose(fp);
    category *C;
    HASH_FIND_STR(cathash, Config("HISTOGRAM-CATEGORY"), C);
    dochash = C->dochash;
    
    fprintf(stderr, "Category %s has %d documents\n", Config("HISTOGRAM-CATEGORY"), HASH_COUNT(dochash));
  }
  
  if (Config("HISTOGRAM-QRELS")) {
    FILE *fp = fopen(Config("HISTOGRAM-QRELS"), "r");
    int qr_topic, qr_rel;
    char qr_q[8], fname[128];
    
    int qr_topic_restriction = -1;
    if (Config("HISTOGRAM-QRELS-TOPIC"))
      qr_topic_restriction = atoi(Config("HISTOGRAM-QRELS-TOPIC"));
    while (fscanf(fp, "%d %s %s %d\n", &qr_topic, qr_q, fname, &qr_rel) == 4) {
      //fprintf(stderr, "%d %s %s %d\n", qr_topic, qr_q, fname, qr_rel);
      if (qr_rel) {
        if ((qr_topic_restriction == -1) || (qr_topic == qr_topic_restriction)) {
          ResultHash *f = NULL;
          
          HASH_FIND_STR(H, fname, f);
          if (f) {
            fprintf(stderr, "!! %s DUPLICATE!!\n", fname);
          } else {
            f = malloc(sizeof(ResultHash));
            strcpy(f->fname, fname);
            f->topic = qr_topic;
            f->hd = sig_cfg.sig_width+1;
            HASH_ADD_STR(H, fname, f);
          }
          
        }
      }
    }
    fclose(fp);
    fprintf(stderr, "Finished reading qrels\n");
  }
  if (lc_strcmp(Config("HISTOGRAM-TYPE"), "doc") == 0) {
    int sigcount = atoi(Config("HISTOGRAM-SOURCE"));
    unsigned char *mask = malloc(sig_cfg.sig_width / 8);
    memset(mask, 0xFF, sig_cfg.sig_width / 8);
    
    int topic_start = -1;
    int topic_end = -1;
    if (Config("HISTOGRAM-TOPIC-START")) {
      topic_start = atoi(Config("HISTOGRAM-TOPIC-START"));
      topic_end = atoi(Config("HISTOGRAM-TOPIC-END"));
    }

    for (int cur_topic = topic_start; cur_topic <= topic_end; cur_topic++) {
      fprintf(stderr, "TOPIC: %d\n", cur_topic);
      for (int i = 0; i < sigcount; i++) {
        //fprintf(stderr, "..%d\n", i);
        const char *fname = (const char *)(sig_file + (size_t)sig_cfg.sig_record_size * i);
        if (H) {
          ResultHash *f = NULL;
          
          HASH_FIND_STR(H, fname, f);
          if (!f || f->topic != cur_topic) {
            //fprintf(stderr, "continue\n");
            continue;
          }
          fprintf(stderr, "  - %d %s\n", i, fname);
        } else {
          //fprintf(stderr, "NO HASH TABLE\n");
        }
        const unsigned char *sig = sig_file + (size_t)sig_cfg.sig_record_size * i + sig_cfg.sig_offset;
        HistogramAdd(&sig_cfg, sig_file, histogram, sig, mask, H, cur_topic, i + 1, 1, dochash, fname);
      }
    }
  } else if (lc_strcmp(Config("HISTOGRAM-TYPE"), "query") == 0) {
    Search *S = InitSearch();
    unsigned char *sig = malloc(sig_cfg.sig_width / 8);
    unsigned char *mask = malloc(sig_cfg.sig_width / 8);
    FlattenSignature(CreateQuerySignature(S, Config("HISTOGRAM-SOURCE")), sig, mask);
    
    HistogramAdd(&sig_cfg, sig_file, histogram, sig, mask, H, atoi(Config("HISTOGRAM-QRELS-TOPIC")), 0, 0, NULL, NULL);
  }
  
  long long xcount = 0;
  for (int i = 0; i <= sig_cfg.sig_width; i++) {
   xcount += histogram[i];
  }
  //printf("%f\n", (double)xcount);
  for (int i = 0; i <= sig_cfg.sig_width; i++) {
    printf("%.32f\n", (double)histogram[i] / (double)xcount);
  }

  if (lc_strcmp(Config("HISTOGRAM-TYPE"), "query") == 0) {
    ResultHash *curr, *tmp;
    HASH_ITER(hh, H, curr, tmp) {
      printf("%s %d %.32f\n", curr->fname, curr->hd, (double)histogram[curr->hd] / (double)xcount);
    }
  }
}

void __RunExhaustiveDocsimSearch()
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
