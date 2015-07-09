#include "topsig-issl.h"
#include "topsig-global.h"
#include "topsig-search.h"
#include "topsig-signature.h"
#include "topsig-config.h"
#include "topsig-atomic.h"
#include "topsig-thread.h"
#include "uthash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/time.h>

int score_list_n = 0;
int score_list[1048576];

typedef struct {
  struct timeval start_time;
  struct timeval last_time;
} timer;
timer timer_start() {
  timer t;
  gettimeofday(&t.start_time, NULL);
  t.last_time = t.start_time;
  return t;
}
double timer_tick(timer *t) {
  struct timeval curr_time;
  gettimeofday(&curr_time, NULL);

  double last_elapsed_ms = (curr_time.tv_sec - t->last_time.tv_sec) * 1000.0 + (curr_time.tv_usec - t->last_time.tv_usec) / 1000.0;
  t->last_time = curr_time;

  return last_elapsed_ms;
}
double get_total_time(timer *t) {
  double total_elapsed_ms = (t->last_time.tv_sec - t->start_time.tv_sec) * 1000.0 + (t->last_time.tv_usec - t->start_time.tv_usec) / 1000.0;

  return total_elapsed_ms;
}

typedef struct {
  int count;
  int *list;
} islEntry_reading;
typedef struct {
  int *list;
} islEntry;
typedef struct {
  islEntry_reading *lookup;
} islSlice_reading;
typedef struct {
  islEntry *lookup;
} islSlice;

static struct {
  int headersize;
  int maxnamelen;
  int sig_width;
  size_t sig_record_size;
  size_t sig_offset;
  int sig_slices;
  int slicewidth;
  
  int threads;
  
  int search_distance;
} cfg;

static void islCount(FILE *, islSlice_reading *);
static void islAdd(FILE *, islSlice_reading *);

void readSigHeader(FILE *fp)
{
  char sig_method[64];
  cfg.headersize = file_read32(fp); // header-size
  int version = file_read32(fp); // version
  cfg.maxnamelen = file_read32(fp); // maxnamelen
  cfg.sig_width = file_read32(fp); // sig_width
  file_read32(fp); // sig_density
  if (version >= 2) {
    file_read32(fp); // sig_seed
  }
  fread(sig_method, 1, 64, fp); // sig_method
  
  cfg.sig_slices = (cfg.sig_width + cfg.slicewidth - 1) / cfg.slicewidth;
  
  cfg.sig_offset = cfg.maxnamelen + 1;
  cfg.sig_offset += 8 * 4; // 8 32-bit ints
  cfg.sig_record_size = cfg.sig_offset + cfg.sig_width / 8;
}

static inline int slice_width(int sig_width, int slice_width, int slice_n)
{
  int slices = (sig_width + slice_width - 1) / slice_width;
  int pos_start = sig_width * slice_n / slices;
  int pos_end = sig_width * (slice_n+1) / slices;
  return pos_end - pos_start;
}

void RunCreateISL()
{
  cfg.slicewidth = 16;
  if (Config("ISL_SLICEWIDTH")) {
    cfg.slicewidth = atoi(Config("ISL_SLICEWIDTH"));
    if ((cfg.slicewidth <= 0) || (cfg.slicewidth >= 31)) {
      fprintf(stderr, "Error: slice widths outside of the range 1-30 are currently not supported.\n");
      exit(1);
    }
  }
  
  FILE *fp = fopen(Config("SIGNATURE-PATH"), "rb");
  readSigHeader(fp);
  
  islSlice_reading *slices = malloc(sizeof(islSlice_reading) * cfg.sig_slices);
  memset(slices, 0, sizeof(islSlice_reading) * cfg.sig_slices);
  
  for (int i = 0; i < cfg.sig_slices; i++) {
    int slicewidth = slice_width(cfg.sig_width, cfg.slicewidth, i);
    slices[i].lookup = malloc(sizeof(islEntry_reading) * (1 << slicewidth));
    memset(slices[i].lookup, 0, sizeof(islEntry_reading) * (1 << slicewidth));
  }
  
  islCount(fp, slices); // Pass 1
  islAdd(fp, slices); // Pass 2
  
}

int get_nth_slice(const unsigned char *sig, int n)
{
  int r = 0;
  switch (cfg.slicewidth) {
    case 8:
      return sig[n];
    case 16:
      return mem_read16(sig + 2 * n);
      break;
    case 24:
      return mem_read16(sig + 3 * n) + (sig[n*3+2]<<1);
      break;
    default:
      // This is not very efficient.
      for (int bit_n = 0; bit_n < cfg.slicewidth; bit_n++) {
        int i = n * cfg.slicewidth + bit_n;
        r = r | ( ((sig[i / 8] & (1 << (i % 8))) > 0) << bit_n );
      }
      return r;
      break;
  }
  // Should not happen
  exit(1);
  return -1;
}

int get_slice_at(const unsigned char *sig, int pos, int sw)
{
  int r = 0;
  for (int bit_n = 0; bit_n < sw; bit_n++) {
    int i = pos + bit_n;
    r = r | ( ((sig[i / 8] & (1 << (i % 8))) > 0) << bit_n );
  }
  return r;
}

static void islCount(FILE *fp, islSlice_reading *slices)
{
  unsigned char sigcache[cfg.sig_record_size + 32];
  memset(sigcache, 0, sizeof(sigcache)); // Extra buffer space so get_nth_slice will work
  fseek(fp, cfg.headersize, SEEK_SET);
  
  //printf("cfg.sig_offset = %d\n", cfg.sig_offset);
  //printf("cfg.sig_record_size = %d\n", cfg.sig_record_size);
  //int grandsum = 0;
  while (fread(sigcache, cfg.sig_record_size, 1, fp) > 0) {
    int slicepos = 0;
    for (int slice = 0; slice < cfg.sig_slices; slice++) {
      int slicewidth = slice_width(cfg.sig_width, cfg.slicewidth, slice);
      //int val = get_nth_slice(sigcache + cfg.sig_offset, slice);
      int val = get_slice_at(sigcache + cfg.sig_offset, slicepos, slicewidth);
      //int val = mem_read16(sigcache + cfg.sig_offset + 2 * slice);
      slices[slice].lookup[val].count++;
      //grandsum++;
      slicepos += slicewidth;
    }
  }
  //printf("grand sum: %d\n", grandsum);
  
  for (int slice = 0; slice < cfg.sig_slices; slice++) {
    int slicewidth = slice_width(cfg.sig_width, cfg.slicewidth, slice);
    for (int val = 0; val < (1 << slicewidth); val++) {
      if (slices[slice].lookup[val].count > 0) {
        slices[slice].lookup[val].list = malloc(sizeof(int) * slices[slice].lookup[val].count);
      }
      slices[slice].lookup[val].count = 0;
    }
  }
}

static int thread_range(int thread_num, int threads, int records)
{
  return thread_num * records / threads;
}

static void islAdd(FILE *fp, islSlice_reading *slices)
{
  unsigned char sigcache[cfg.sig_record_size + 32];
  memset(sigcache, 0, sizeof(sigcache)); // Extra buffer space so get_nth_slice will work
  fseek(fp, cfg.headersize, SEEK_SET);
  int sig_index = 0;
  
  while (fread(sigcache, cfg.sig_record_size, 1, fp) > 0) {
    for (int slice = 0; slice < cfg.sig_slices; slice++) {
      int val = get_nth_slice(sigcache + cfg.sig_offset, slice);
      //int val = mem_read16(sigcache + cfg.sig_offset + 2 * slice);
      int count = slices[slice].lookup[val].count;
      slices[slice].lookup[val].list[count] = sig_index;
      slices[slice].lookup[val].count++;
    }
    sig_index++;
  }

  
  FILE *fo = fopen(Config("ISL-PATH"), "wb");
  file_write32(cfg.sig_slices, fo); // slices
  file_write32(0, fo); // compression
  file_write32(1, fo); // storage mode
  file_write32(sig_index, fo); // signatures
  file_write32(cfg.slicewidth, fo); // slice width
  
  for (int slice = 0; slice < cfg.sig_slices; slice++) {
    fprintf(stderr, "Slice %d/%d\n", slice, cfg.sig_slices);
    int slicewidth = slice_width(cfg.sig_width, cfg.slicewidth, slice);
    for (int val = 0; val < (1 << slicewidth); val++) {
      if ((val % 1000)==0) fprintf(stderr, "val %d/%d\n", val, (1 << slicewidth));
      file_write32(slices[slice].lookup[val].count, fo);
      for (int n = 0; n < slices[slice].lookup[val].count; n++) {
        file_write32(slices[slice].lookup[val].list[n], fo);
      }
    }
  }
  fclose(fo);
}

islSlice *readISL(const char *islPath, int *records)
{
  FILE *fp = fopen(islPath, "rb");
  
  unsigned char islheader[16];
  
  fread(islheader, 1, 16, fp);
  
  int threads = 1;
  if (Config("SEARCH-DOC-THREADS")) {
    threads = atoi(Config("SEARCH-DOC-THREADS"));
    if (threads > 1) {
      fprintf(stderr, "Multithreading mode enabled, using %d worker threads\n", threads);
    } else {
      threads = 1;
    }
  }
  cfg.threads = threads;
  
  int islfield_slices = mem_read32(islheader);
  int islfield_compression = mem_read32(islheader+4);
  int islfield_storagemode = mem_read32(islheader+8);
  *records = mem_read32(islheader+12);
  cfg.sig_slices = islfield_slices;
  
  int *test_fields = (int *)islheader;
  int fastread = 0;

  if (islfield_slices == test_fields[0]) {
    if (islfield_compression == test_fields[1]) {
      if (islfield_storagemode == test_fields[2]) {
        if (*records == test_fields[3]) {
          fastread = 1;
        }
      }
    }
  }
  
  int slicewidth = 16;
  if (islfield_storagemode == 1) {
    // Extra 32-bit field containing slice width.
    slicewidth = file_read32(fp);
  }
  cfg.slicewidth = slicewidth;
  
  islSlice *slices = malloc(sizeof(islSlice) * cfg.sig_slices);
  for (int i = 0; i < cfg.sig_slices; i++) {
    slices[i].lookup = malloc(sizeof(islEntry) * (1 << cfg.slicewidth));
    memset(slices[i].lookup, 0, sizeof(islEntry) * (1 << cfg.slicewidth));
  }
  
  fprintf(stderr, "Reading ISSL:\r");
  
  int slices_read_total = 0;
  int slices_read_null = 0;
  
  for (int slice = 0; slice < cfg.sig_slices; slice++) {
    int slicewidth = slice_width(cfg.sig_width, cfg.slicewidth, slice);
    for (int val = 0; val < (1 << slicewidth); val++) {
      slices_read_total++;
      int count = file_read32(fp);
      if (count > 0) {
        int *ar = malloc(sizeof(int) * (count + 1 + threads));
        slices[slice].lookup[val].list = ar+1+threads;
        slices[slice].lookup[val].list[-1] = count;

        if (fastread) {
          fread(slices[slice].lookup[val].list, 4, count, fp);
        } else {
          for (int n = 0; n < count; n++) {
            slices[slice].lookup[val].list[n] = file_read32(fp);
          }
        }
      } else {
        slices[slice].lookup[val].list = NULL;
        slices_read_null++;
      }
    }
    //if ((slice % (cfg.sig_slices / cfg.slicewidth)) == (cfg.sig_slices / cfg.slicewidth - 1)) {
    fprintf(stderr, "Reading ISSL: %d/%d\r", slice+1, cfg.sig_slices);
    //}
  }
  fprintf(stderr, "\n%d/%d (%.2f%% null lists)\n",slices_read_null, slices_read_total, 100.0*slices_read_null/slices_read_total);
  fclose(fp);

  /* Pointless slice partitioning, wrong way to go about things
  if (threads > 1) {
    for (int slice = 0; slice < cfg.sig_slices; slice++) {
      for (int val = 0; val < (1 << cfg.slicewidth); val++) {
        int *isl = slices[slice].lookup[val].list;
        if (isl) {
          int count = isl[-1];
          int cthread = -1;
          for (int i = 0; i < count; i++) {
            while (isl[i] >= thread_range(cthread+1, threads, *records)) {
              cthread++;
              isl[-1 - threads + cthread] = i;
            }
          }
          while (cthread < threads-1) {
            cthread++;
            isl[-1 - threads + cthread] = count;
          }
        }
      }
      fprintf(stderr, "Slice partitioning: %d/%d\r", slice+1, cfg.sig_slices);
    }
    fprintf(stderr, "\n");
  }
  */
    //fprintf(stderr, "T %d\n", thread_range(i, threads, *records));
  
  return slices;
}

static inline int count_bits(unsigned int v) {
  v = v - ((v >> 1) & 0x55555555);
  v = (v & 0x33333333) + ((v >> 2) & 0x33333333);
  return (((v + (v >> 4)) & 0xF0F0F0F) * 0x1010101) >> 24;
}

static int bitcount_compar(const void *A, const void *B)
{
  const int *a = A;
  const int *b = B;
  
  return count_bits(*a) - count_bits(*b);
}

typedef struct {
  int score;
  int dist;
  int docid;
} result;

// For sorting results, highest to lowest
static int result_compar(const void *A, const void *B)
{
  const result *a = A;
  const result *b = B;
  
  if (a->dist != b->dist)
    return a->dist - b->dist;
  
  return a->docid - b->docid;
}

void rescoreResults(FILE *fp, result *results, int topk, unsigned char *sig, unsigned char *mask)
{
  int sig_bytes = cfg.sig_width / 8;
  unsigned char cursig[sig_bytes];

  for (int i = 0; i < topk; i++) {
    fseek(fp, cfg.headersize + cfg.sig_record_size * results[i].docid + cfg.sig_offset, SEEK_SET);
    fread(cursig, sig_bytes, 1, fp);
    
    results[i].dist = DocumentDistance(cfg.sig_width, sig, mask, cursig);
  }
}


result *dummy_extract_topk(result *results, int total_results, int topk)
{
  result *r = malloc(sizeof(result) * topk);
  
  for (int i = 0; i < topk; i++) {
    if (total_results > -1)
      r[i] = results[i];
  }
  return r;
}


result *extract_topk(result *results, int total_results, int topk)
{
  result *r = malloc(sizeof(result) * topk);
  int filled_n = 0;
  int worst_result = -1;
  
  for (int i = 0; i < total_results; i++) {
    if (filled_n < topk) {
      r[filled_n++] = results[i];
    } else {
      if (worst_result == -1) {
        worst_result = 0;
        for (int j = 1; j < topk; j++) {
          if (result_compar(&r[j], &r[worst_result]) > 0) {
            worst_result = j;
          }
        }
      }
      if (result_compar(&results[i], &r[worst_result]) < 0) {
        r[worst_result] = results[i];
        worst_result = -1;
      }
    }
  }
  //qsort(results, total_results, sizeof(result), result_compar);
  //memcpy(r, results, sizeof(result) * topk);
  return r;
}

static result *extract_topk2_t(int *scores, int begin, int end, int topk)
{
  result *r = malloc(sizeof(result) * topk);
  int worst_result = -1;
  int worst_score = -1;
  
  for (int i = begin; i < begin + topk; i++) {
    r[i-begin].score = scores[i-begin];
    r[i-begin].docid = i;
  }
  
  for (int i = begin+topk; i < end; i++) {
    if (worst_result == -1) {
      worst_result = 0;
      for (int j = 1; j < topk; j++) {
        if (r[j].score < r[worst_result].score) {
          worst_result = j;
          worst_score = r[worst_result].score;
        }
      }
    }
    if (scores[i-begin] > worst_score) {
      r[worst_result].docid = i;
      r[worst_result].score = scores[i-begin];
      worst_result = -1;
    }
  }
  //qsort(results, total_results, sizeof(result), result_compar);
  //memcpy(r, results, sizeof(result) * topk);
  return r;
}

static inline result *extract_topk2(int *scores, int total_results, int topk) {
  return extract_topk2_t(scores, 0, total_results, topk);
}

static inline result *extract_topk3(int *scores, int total_results, int topk)
{
  result *r = malloc(sizeof(result) * topk);
  int worst_result = -1;
  int worst_score = -1;
  
  for (int i = 0; i < topk; i++) {
    r[i].score = scores[score_list[i]];
    r[i].docid = i;
  }
  
  for (int i = topk; i < score_list_n; i++) {
    if (worst_result == -1) {
      worst_result = 0;
      for (int j = 1; j < topk; j++) {
        if (r[j].score < r[worst_result].score) {
          worst_result = j;
          worst_score = r[worst_result].score;
        }
      }
    }
    if (scores[score_list[i]] > worst_score) {
      r[worst_result].docid = score_list[i];
      r[worst_result].score = scores[score_list[i]];
      worst_result = -1;
    }
  }
  //qsort(results, total_results, sizeof(result), result_compar);
  //memcpy(r, results, sizeof(result) * topk);
  return r;
}


void rescoreResults_buffer(unsigned char *fp_sig_buffer, result *results, int topk, unsigned char *sig, unsigned char *mask)
{
  for (int i = 0; i < topk; i++) {
    unsigned char *cursig = fp_sig_buffer + (cfg.sig_record_size * results[i].docid + cfg.sig_offset);
    
    results[i].dist = DocumentDistance(cfg.sig_width, sig, mask, cursig);
  }
}

typedef struct {
  char docname[32];
  int docindex;
  UT_hash_handle hh;
} docPosition;

void ExperimentalRerankTopFile()
{
  Search *S = InitSearch();

  FILE *fp_sig = fopen(Config("SIGNATURE-PATH"), "rb");
  
  readSigHeader(fp_sig);
  
  docPosition *docPosLut = NULL;
  
  unsigned char sigcache[cfg.sig_record_size];
  int doc_index = 0;
  while (fread(sigcache, cfg.sig_record_size, 1, fp_sig) > 0) {
    docPosition *D = malloc(sizeof(docPosition));
    strcpy(D->docname, (char *)sigcache);
    D->docindex = doc_index;
    
    //printf("Added doc %s\n", D->docname);
    HASH_ADD_STR(docPosLut, docname, D);
    doc_index++;
  }
  
  
  int sig_bytes = cfg.sig_width / 8;
  unsigned char cursig[sig_bytes];
  unsigned char curmask[sig_bytes];
  unsigned char docsig[sig_bytes];
  
  FILE *fp_input = fopen(Config("RERANK-INPUT"), "r");
  FILE *fp_topic = fopen(Config("RERANK-TOPIC"), "r");
  FILE *fp_output = fopen(Config("RERANK-OUTPUT"), "w");
  
  int cur_topic_id = -1;
  char cur_topic_name[65536];
  
  Signature *sig = NULL;
  
  for (;;) {
    int in_topic_id;
    char in_q0[3];
    char in_doc_id[4096];
    int in_rank;
    int in_score;
    char in_runname[4096];
    
    if (fscanf(fp_input, "%d %s %s %d %d %s\n", &in_topic_id, in_q0, in_doc_id, &in_rank, &in_score, in_runname) < 6) break;
    
    while (cur_topic_id < in_topic_id) {
      fscanf(fp_topic, "%d %[^\n]\n", &cur_topic_id, cur_topic_name);
      printf("Switched to topic %d [%s]\n", cur_topic_id, cur_topic_name);
      
      if (sig) {
        SignatureDestroy(sig);
        sig = NULL;
      }
      sig = CreateQuerySignature(S, cur_topic_name);
      FlattenSignature(sig, cursig, curmask);
    }
    if (cur_topic_id != in_topic_id) {
      fprintf(stderr, "ERROR: TOPIC %d NOT FOUND IN TOPIC FILE\n", in_topic_id);
      exit(1);
    }
    
    docPosition *D;
    HASH_FIND_STR(docPosLut, in_doc_id, D);
    if (D) {
      fseek(fp_sig, cfg.headersize + cfg.sig_record_size * D->docindex + cfg.sig_offset, SEEK_SET);
      fread(docsig, sig_bytes, 1, fp_sig);
      int dist = DocumentDistance(cfg.sig_width, docsig, curmask, cursig);
      
      fprintf(fp_output, "%d %s %s %d %d %s\n", in_topic_id, in_q0, in_doc_id, dist, 100000-dist, in_runname);
    } else {
      fprintf(stderr, "ERROR: DOCUMENT %s NOT FOUND IN SIGNATURE\n", in_doc_id);
      exit(1);
    }
  }
  SignatureDestroy(sig);
  
  fclose(fp_input);
  fclose(fp_topic);
  fclose(fp_output);
  fclose(fp_sig);
}


static inline void isslsumadd(int *scores, const islEntry *e, int dist)
{
  if (dist < 2) {
    for (int n = 0; n < e->list[-1]; n++) {
      scores[e->list[n]] += cfg.slicewidth-dist;
    }
  } else {
    for (int n = 0; n < e->list[-1]; n++) {
      if (scores[e->list[n]]>0) {
        scores[e->list[n]] += cfg.slicewidth-dist;
      }
    }
  }
}

/*
static inline void isslsumadd(int *scores, const islEntry *e, int dist)
{
  for (int n = 0; n < e->count; n++) {
    //atomic_add(scores+e->list[n], cfg.slicewidth-dist);
    scores[e->list[n]] += cfg.slicewidth-dist;
  }
}
*/

static inline void isslsum(int *scores, const unsigned char *sigcache, const int *bitmask, int first_issl, int last_issl, const islSlice *slices)
{
  int slicevals[cfg.sig_slices];
  for (int slice = 0; slice < cfg.sig_slices; slice++) {
    //slicevals[slice] = mem_read16(sigcache + cfg.sig_offset + 2 * slice);
    slicevals[slice] = get_nth_slice(sigcache + cfg.sig_offset, slice);
  }

  for (int m = first_issl; m <= last_issl; m++) {
    int dist = count_bits(bitmask[m]);
    
    for (int slice = 0; slice < cfg.sig_slices; slice++) {
      isslsumadd(scores, &slices[slice].lookup[slicevals[slice] ^ bitmask[m]], dist);
    }
  }
}

static inline void isslsum2_t(int *scores, const unsigned char *sigcache, const int *bitmask, int first_issl, int last_issl, const islSlice *slices, int thread_n, int offset)
{
  int threads = cfg.threads;
  int slicevals[cfg.sig_slices];
  for (int slice = 0; slice < cfg.sig_slices; slice++) {
    slicevals[slice] = get_nth_slice(sigcache + cfg.sig_offset, slice);
  }
  
  for (int m = first_issl; m <= last_issl; m++) {
    int dist = count_bits(bitmask[m]);
    int scoreadd = cfg.slicewidth-dist;
    
    if (dist < 2) {
      for (int slice = 0; slice < cfg.sig_slices; slice++) {
        const islEntry *e = &slices[slice].lookup[slicevals[slice] ^ bitmask[m]];
        if (e->list) {
          const int e_begin = e->list[-1 - threads + thread_n];
          const int e_end = e->list[0 - threads + thread_n];
          const int *e_list = e->list;
          
          for (int n = e_begin; n < e_end; n++) {
            scores[e_list[n]-offset] += scoreadd;
          }
        }
      }
    } else {
      for (int slice = 0; slice < cfg.sig_slices; slice++) {
        const islEntry *e = &slices[slice].lookup[slicevals[slice] ^ bitmask[m]];
        if (e->list) {
          const int e_begin = e->list[-1 - threads + thread_n];
          const int e_end = e->list[0 - threads + thread_n];
          const int *e_list = e->list;
          
          for (int n = e_begin; n < e_end; n++) {
            if (scores[e_list[n]-offset]>0) {
              scores[e_list[n]-offset] += scoreadd;
            }
          }
        }
      }
    }
  }
}

static long long list_count = 0;
static long long list_len = 0;

static inline void isslsum2(int *scores, const unsigned char *sigcache, const int *bitmask, int first_issl, int last_issl, const islSlice *slices)
{
  int slicevals[cfg.sig_slices];
  int slicepos = 0;
  for (int slice = 0; slice < cfg.sig_slices; slice++) {
    int slicewidth = slice_width(cfg.sig_width, cfg.slicewidth, slice);
    slicevals[slice] = get_slice_at(sigcache + cfg.sig_offset, slicepos, slicewidth);
    slicepos += slicewidth;
  }
  
  for (int m = first_issl; m <= last_issl; m++) {
    int dist = count_bits(bitmask[m]);
    
    //if (dist <= cfg.search_distance / 2) {
    if (dist < 2) {
      for (int slice = 0; slice < cfg.sig_slices; slice++) {
        int slicewidth = slice_width(cfg.sig_width, cfg.slicewidth, slice);
        if (dist >= slicewidth) continue;
        int scoreadd = slicewidth-dist;
        const islEntry *e = &slices[slice].lookup[slicevals[slice] ^ bitmask[m]];
        if (e->list) {
          const int e_count = e->list[-1];
          const int *e_list = e->list;
          
          list_count++;
          list_len += e_count;
          
          for (int n = 0; n < e_count; n++) {
            if (scores[e_list[n]] == 0) {
              score_list[score_list_n++] = e_list[n];
            }
            scores[e_list[n]] += scoreadd;
          }
        }
      }
    } else {
      for (int slice = 0; slice < cfg.sig_slices; slice++) {
        int slicewidth = slice_width(cfg.sig_width, cfg.slicewidth, slice);
        if (dist >= slicewidth) continue;
        int scoreadd = slicewidth-dist;
        const islEntry *e = &slices[slice].lookup[slicevals[slice] ^ bitmask[m]];
        if (e->list) {
          const int e_count = e->list[-1];
          const int *e_list = e->list;
          
          list_count++;
          list_len += e_count;
          
          for (int n = 0; n < e_count; n++) {
            if (scores[e_list[n]]>0) {
              scores[e_list[n]] += scoreadd;
            }
          }
        }
      }
    }
  }
}

typedef struct {
  int **scores;
  unsigned char const* const* sigcaches;
  int sigcaches_n;
  const int *bitmask;
  int first_issl;
  int last_issl;
  const islSlice *slices;
} ISSLSumInput;

static void *isslsum_void(void *input)
{
  ISSLSumInput *i = (ISSLSumInput *)input;
  for (int n = 0; n < i->sigcaches_n; n++) {
    isslsum(i->scores[n], i->sigcaches[n], i->bitmask, i->first_issl, i->last_issl, i->slices);
  }
  return NULL;
}

static inline result *allocscores(const int *scores, int records)
{
  result *results = malloc(sizeof(result) * records);
  for (int i = 0; i < records; i++) {
    results[i].docid = i;
    results[i].score = scores[i];
    results[i].dist = cfg.sig_width - scores[i];
  }
  return results;
}

void old_RunSearchISLTurbo()
{
  int records;
  timer T = timer_start();
  islSlice *slices = readISL(Config("ISL-PATH"), &records);
  double isl_read_time = timer_tick(&T);
  fprintf(stderr, "Time to read ISL: %.2fms\nTotal records: %d\n", isl_read_time, records);
  
  FILE *fp_sig = fopen(Config("SIGNATURE-PATH"), "rb");
  FILE *fp_src = fp_sig;
  
  if (Config("SOURCE-SIGNATURE-PATH")) {
    fp_src = fopen(Config("SOURCE-SIGNATURE-PATH"), "rb");
  } else {
    fp_src = fopen(Config("SIGNATURE-PATH"), "rb");
  }
  
  int topk = atoi(Config("SEARCH-DOC-TOPK")) + 1;
  
  readSigHeader(fp_sig);
  int fpsig_sig_width = cfg.sig_width;
  readSigHeader(fp_src);
  if (fpsig_sig_width != cfg.sig_width) {
    fprintf(stderr, "Error: source and signature sigfiles differ in width. %d v %d\n", fpsig_sig_width, cfg.sig_width);
    exit(1);
  }
  
  fseek(fp_sig, 0, SEEK_END);
  int fp_sig_count = (ftell(fp_sig) - cfg.headersize) / cfg.sig_record_size;
  
  if (fp_sig_count != records) {
    fprintf(stderr, "Error: the search signature file and the index contain a different number of signatures\n");
    exit(1);
  }
  fseek(fp_sig, cfg.headersize, SEEK_SET);
  unsigned char *fp_sig_buffer = malloc((long)cfg.sig_record_size * (long)records);
  if (!fp_sig_buffer) {
    fprintf(stderr, "Error: unable to allocate the %ld bytes needed to hold the signature file\n", (long)cfg.sig_record_size * (long)records);
    exit(1);
  }
  fread(fp_sig_buffer, cfg.sig_record_size, records, fp_sig);
  fclose(fp_sig);
  
  fseek(fp_src, 0, SEEK_END);
  int fp_src_count = (ftell(fp_src) - cfg.headersize) / cfg.sig_record_size;
  fseek(fp_src, 0, SEEK_SET);
  
  int compare_doc = -1;
  int compare_doc_first = 0;
  int compare_doc_last = fp_src_count - 1;
  if (Config("SEARCH-DOC")) {
    compare_doc_first = atoi(Config("SEARCH-DOC"));
    compare_doc_last = atoi(Config("SEARCH-DOC"));
  }
  if (Config("SEARCH-DOC-FIRST")) {
    compare_doc_first = atoi(Config("SEARCH-DOC-FIRST"));
  }
  if (Config("SEARCH-DOC-LAST")) {
    compare_doc_last = atoi(Config("SEARCH-DOC-LAST"));
  }

  int slice_lim = 1 << cfg.slicewidth;
  int *bitmask = malloc(sizeof(int) * slice_lim);
  
  int max_dist = atoi(Config("ISL-MAX-DIST"));
  
  for (int i = 0; i < slice_lim; i++) {
    bitmask[i] = i;
  }
  qsort(bitmask, slice_lim, sizeof(int), bitcount_compar);
  
  int mlength = slice_lim;

  for (int i = 0; i < slice_lim; i++) {
    int dist = count_bits(bitmask[i]);
    if (dist > max_dist) {
      mlength = i;
      break;
    }
  }
  
  int threads = 1;
  if (Config("SEARCH-DOC-THREADS")) {
    threads = atoi(Config("SEARCH-DOC-THREADS"));
  }
  
  int lookahead = 1;
  if (Config("SEARCH-DOC-LOOKAHEAD")) {
    lookahead = atoi(Config("SEARCH-DOC-LOOKAHEAD"));
  }
  
  int **scores = malloc(sizeof(int *) * lookahead);
  unsigned char **sigcaches = malloc(sizeof(unsigned char *) * lookahead);

  for (int i = 0; i < lookahead; i++) {
    scores[i] = malloc(sizeof(int) * records);
    sigcaches[i] = malloc(sizeof(unsigned char) * cfg.sig_record_size);
  }
  
  int sigcaches_filled = 0;
  fseek(fp_src, cfg.headersize + cfg.sig_record_size * compare_doc_first, SEEK_SET);
  
  double misc_time = timer_tick(&T);
  fprintf(stderr, "Time for misc ops: %.2fms\n", misc_time);
    
  for (compare_doc = compare_doc_first; compare_doc <= compare_doc_last; compare_doc++) {
    memset(scores[sigcaches_filled], 0, sizeof(scores[0][0]) * records);

    fread(sigcaches[sigcaches_filled++], cfg.sig_record_size, 1, fp_src);

    if ((sigcaches_filled == lookahead) || (compare_doc == compare_doc_last)) {
    
      if (threads == 1) {
        //for (int i = 0; i < sigcaches_filled; i++) {
          isslsum(scores[0], sigcaches[0], bitmask, 0, mlength-1, slices);
        //}
      } else {
        void *inputs[threads];
        for (int cthread = 0; cthread < threads; cthread++) {
          int mstart = cthread * mlength / threads;
          int mend = (1+cthread) * mlength / threads - 1;
          
          ISSLSumInput *input = malloc(sizeof(ISSLSumInput));
          
          input->scores = scores;
          input->sigcaches = (unsigned char const * const * )sigcaches;
          input->sigcaches_n = sigcaches_filled;
          input->bitmask = bitmask;
          input->first_issl = mstart;
          input->last_issl = mend;
          input->slices = slices;
          
          inputs[cthread] = input;
          //isslsum(scores, sigcache, bitmask, mstart, mend, slices);
        }
        DivideWork(inputs, isslsum_void, threads);
        for (int cthread = 0; cthread < threads; cthread++) {
          free(inputs[cthread]);
        }
      }
      
      for (int sc = 0; sc < sigcaches_filled; sc++) {
        result *results = allocscores(scores[sc], records);
        
        //result *topresults = dummy_extract_topk(results, records, topk);
        result *topresults = extract_topk(results, records, topk);

        int sig_bytes = cfg.sig_width / 8;
        unsigned char curmask[sig_bytes];
        memset(curmask, 0xFF, sig_bytes);
        
        rescoreResults_buffer(fp_sig_buffer, topresults, topk, sigcaches[sc] + cfg.sig_offset, curmask);
        
        qsort(topresults, topk, sizeof(result), result_compar);
        
        
        for (int i = 0; i < topk-1; i++) {
          char *docname = (char *)fp_sig_buffer + (cfg.sig_record_size * topresults[i].docid);

          //printf("%02d. (%05d) %s  Dist: %d (first seen at %d)\n", i+1, results[i].docid, docname, results[i].dist, scoresd[results[i].docid] - 1);
          //printf("%s Q0 %s 1 1 topsig\n", sigcache, docname);
          //printf("%s DIST %03d XIST Q0 %s 1 1 topsig\n", sigcache, results[i].dist, docname);
          
          // Norm format:
          printf("%s %s %d\n", sigcaches[sc], docname, topresults[i].dist);
          
          // TREC format:
          //printf("%s Q0 %s %d %d TopISSL\n", sigcaches[sc], docname, i+1, 1000000 - i);
        }
        free(results);
        free(topresults);
        
        sigcaches_filled = 0;
      }
    }
  }
  
  double search_time = timer_tick(&T);
  fprintf(stderr, "Time to search: %.2fms\n", search_time);
  
  fclose(fp_src);
}

typedef struct {
  const int *bitmask;
  const islSlice *slices;
  int records;
  int mlength;
  int *scores;
  
  TBPHandle *thread_handle;
} ISSLSearchHandle;

typedef struct {
  int thread_n;
  ISSLSearchHandle *H;
  int *scores;
  
  int doc_begin;
  int doc_end;
} ISSLSearchThread;

ISSLSearchHandle *ISSLSearchInit(const int *bitmask, const islSlice *slices, int records, int mlength)
{
  ISSLSearchHandle *H = malloc(sizeof(ISSLSearchHandle));
  H->bitmask = bitmask;
  H->slices = slices;
  H->records = records;
  H->mlength = mlength;
  H->scores = NULL;
  H->thread_handle = NULL;
  
  if (cfg.threads > 1) {
    void **thread_data_vps = malloc(sizeof(void *) * cfg.threads);
    for (int i = 0; i < cfg.threads; i++) {
      ISSLSearchThread *T = malloc(sizeof(ISSLSearchThread));
      T = malloc(sizeof(ISSLSearchThread));
      T->thread_n = i;
      T->H = H;
      T->doc_begin = thread_range(i, cfg.threads, records);
      T->doc_end = thread_range(i+1, cfg.threads, records);
      T->scores = malloc(sizeof(int) * T->doc_end - T->doc_begin);
      thread_data_vps[i] = T;
    }
    H->thread_handle = TBPInit(cfg.threads, thread_data_vps);
  } else {
    H->scores = malloc(sizeof(int) * records);
  }
  return H;
}

void ISSLSearchDestroy(ISSLSearchHandle *H)
{
  if (H->scores) free(H->scores);
  free(H);
}

typedef struct {
  const unsigned char *signature;
  int topk;
} ISSLTask;

static void *ISSLSearchThreaded(void *thread_data_vp, void *task_data_vp)
{
  ISSLSearchThread *T = thread_data_vp;
  ISSLTask *D = task_data_vp;
  
  memset(T->scores, 0, sizeof(int) * T->doc_end - T->doc_begin);
  isslsum2_t(T->scores, D->signature, T->H->bitmask, 0, T->H->mlength - 1, T->H->slices, T->thread_n, T->doc_begin);
  result *topresults = extract_topk2_t(T->scores, T->doc_begin, T->doc_end, D->topk);
  return topresults;
}

static inline result *ISSLSearch(ISSLSearchHandle *H, const unsigned char *signature, int topk)
{
  if (!H->thread_handle) {
    memset(H->scores, 0, sizeof(int) * H->records);
    score_list_n = 0;
    isslsum2(H->scores, signature, H->bitmask, 0, H->mlength - 1, H->slices);
    result *topresults = extract_topk3(H->scores, H->records, topk);
    return topresults;
  } else {
    ISSLTask T = {signature, topk};
    
    void **res = TBPDivideWork(H->thread_handle, &T, ISSLSearchThreaded);
    
    result *topresults = res[0];

    return topresults;
  }
}

void RunSearchISLTurbo()
{
  int records;
  timer T = timer_start();
  islSlice *slices = readISL(Config("ISL-PATH"), &records);
  double isl_read_time = timer_tick(&T);
  fprintf(stderr, "Time to read ISSL: %.2fms\nTotal records: %d\n", isl_read_time, records);
  
  FILE *fp_sig = fopen(Config("SIGNATURE-PATH"), "rb");
  FILE *fp_src = fp_sig;
  
  if (Config("SOURCE-SIGNATURE-PATH")) {
    fp_src = fopen(Config("SOURCE-SIGNATURE-PATH"), "rb");
  } else {
    fp_src = fopen(Config("SIGNATURE-PATH"), "rb");
  }
  
  int topk = atoi(Config("SEARCH-DOC-TOPK")) + 1;
  
  readSigHeader(fp_sig);
  int fpsig_sig_width = cfg.sig_width;
  readSigHeader(fp_src);
  if (fpsig_sig_width != cfg.sig_width) {
    fprintf(stderr, "Error: source and signature sigfiles differ in width. %d v %d\n", fpsig_sig_width, cfg.sig_width);
    exit(1);
  }
  
  fseek(fp_sig, 0, SEEK_END);
  int fp_sig_count = (ftell(fp_sig) - cfg.headersize) / cfg.sig_record_size;
  
  if (fp_sig_count != records) {
    fprintf(stderr, "Error: the search signature file and the index contain a different number of signatures\n");
    exit(1);
  }
  fseek(fp_sig, cfg.headersize, SEEK_SET);
  unsigned char *fp_sig_buffer = malloc((long)cfg.sig_record_size * (long)records);
  if (!fp_sig_buffer) {
    fprintf(stderr, "Error: unable to allocate the %ld bytes needed to hold the signature file\n", (long)cfg.sig_record_size * (long)records);
    exit(1);
  }
  fread(fp_sig_buffer, cfg.sig_record_size, records, fp_sig);
  fclose(fp_sig);
  
  fseek(fp_src, 0, SEEK_END);
  int fp_src_count = (ftell(fp_src) - cfg.headersize) / cfg.sig_record_size;
  fseek(fp_src, 0, SEEK_SET);
  
  int compare_doc = -1;
  int compare_doc_first = 0;
  int compare_doc_last = fp_src_count - 1;
  if (Config("SEARCH-DOC")) {
    compare_doc_first = atoi(Config("SEARCH-DOC"));
    compare_doc_last = atoi(Config("SEARCH-DOC"));
  }
  if (Config("SEARCH-DOC-FIRST")) {
    compare_doc_first = atoi(Config("SEARCH-DOC-FIRST"));
  }
  if (Config("SEARCH-DOC-LAST")) {
    compare_doc_last = atoi(Config("SEARCH-DOC-LAST"));
  }

  int slice_lim = 1 << cfg.slicewidth;
  int *bitmask = malloc(sizeof(int) * slice_lim);
  
  int max_dist = atoi(Config("ISL-MAX-DIST"));
  cfg.search_distance = max_dist;
  
  for (int i = 0; i < slice_lim; i++) {
    bitmask[i] = i;
  }
  qsort(bitmask, slice_lim, sizeof(int), bitcount_compar);
  
  int mlength = slice_lim;

  for (int i = 0; i < slice_lim; i++) {
    int dist = count_bits(bitmask[i]);
    if (dist > max_dist) {
      mlength = i;
      break;
    }
  }
  
  //int *scores = malloc(sizeof(int) * records);
  unsigned char **sigcaches = malloc(sizeof(unsigned char *) * 1);

  for (int i = 0; i < 1; i++) {
    sigcaches[i] = malloc(sizeof(unsigned char) * cfg.sig_record_size);
  }
  
  fseek(fp_src, cfg.headersize + cfg.sig_record_size * compare_doc_first, SEEK_SET);
  
  ISSLSearchHandle *search_handle = ISSLSearchInit(bitmask, slices, records, mlength);
  
  double misc_time = timer_tick(&T); // The time between these two operations is generally inconsequential
    
  for (compare_doc = compare_doc_first; compare_doc <= compare_doc_last; compare_doc++) {
    //memset(scores, 0, sizeof(int) * records);
    fread(sigcaches[0], cfg.sig_record_size, 1, fp_src);
    
    //result *topresults = ISSLSearch(sigcaches[0], bitmask, slices, records, topk, mlength, NULL);
    result *topresults = ISSLSearch(search_handle, sigcaches[0], topk);
    
    //isslsum2(scores, sigcaches[0], bitmask, 0, mlength-1, slices);
    
    //result *topresults = extract_topk2(scores, records, topk);

    int sig_bytes = cfg.sig_width / 8;
    unsigned char curmask[sig_bytes];
    memset(curmask, 0xFF, sig_bytes);
    
    rescoreResults_buffer(fp_sig_buffer, topresults, topk, sigcaches[0] + cfg.sig_offset, curmask);
    qsort(topresults, topk, sizeof(result), result_compar);

    for (int i = 0; i < topk-1; i++) {
      char *docname = (char *)fp_sig_buffer + (cfg.sig_record_size * topresults[i].docid);

      //printf("%02d. (%05d) %s  Dist: %d (first seen at %d)\n", i+1, results[i].docid, docname, results[i].dist, scoresd[results[i].docid] - 1);
      //printf("%s Q0 %s 1 1 topsig\n", sigcache, docname);
      //printf("%s DIST %03d XIST Q0 %s 1 1 topsig\n", sigcache, results[i].dist, docname);
      
      // Norm format:
      printf("%s %s %d\n", sigcaches[0], docname, topresults[i].dist);
      
      // TREC format:
      //printf("%s Q0 %s %d %d TopISSL\n", sigcaches[sc], docname, i+1, 1000000 - i);
    }

    free(topresults);
  }
  
  double search_time = timer_tick(&T);
  fprintf(stderr, "Time to search: %.2fms\n", search_time);
  fprintf(stderr, "list lens: %I64d %I64d\n", list_count, list_len);
  
  ISSLSearchDestroy(search_handle);
  
  fclose(fp_src);
}
