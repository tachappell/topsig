#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include "topsig-global.h"
#include "topsig-issl.h"
#include "topsig-timer.h"
#include "topsig-config.h"
#include "topsig-search.h"
#include "topsig-thread.h"

// Important configuration options:
// ISL-PATH
// SIGNATURE-PATH
// ISL-MAX-DIST
// ISL-MAX-DIST-NONEW
// SEARCH-DOC-THREADS
// SEARCH-DOC-FIRST
// SEARCH-DOC-LAST
// SEARCH-DOC-TOPK
// SEARCH-DOC-RERANK

#define DEFAULT_HOTLIST_BUFFERSIZE 2048

typedef struct {
  int header_size;
  int max_name_len;
  int sig_width;
  int sig_offset;
  int sig_record_size;
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
  
  return cfg;
}

static inline int slice_width(int sig_width, int slices, int slice_n)
{
  int pos_start = sig_width * slice_n / slices;
  int pos_end = sig_width * (slice_n+1) / slices;
  return pos_end - pos_start;
}

static inline int get_slice_at(const unsigned char *sig, int pos, int sw)
{
  int r = 0;
  for (int bit_n = 0; bit_n < sw; bit_n++) {
    int i = pos + bit_n;
    r = r | ( ((sig[i / 8] & (1 << (i % 8))) > 0) << bit_n );
  }
  return r;
}

static int **Count_ISSL_List_Lengths(const SignatureHeader *cfg, FILE *fp, int num_slices, int *signature_count)
{
  // Allocate memory for the list lengths
  int **issl_counts = malloc(sizeof(int *) * num_slices);
  
  for (int i = 0; i < num_slices; i++) {
    int width = slice_width(cfg->sig_width, num_slices, i);
    int num_issl_lists = 1 << width;
    issl_counts[i] = malloc(sizeof(int) * num_issl_lists);
    memset(issl_counts[i], 0, sizeof(int) * num_issl_lists);
  }
  
  // Rewind to just after the header
  fseek(fp, cfg->header_size, SEEK_SET);
  
  unsigned char *sig_buf = malloc(cfg->sig_record_size);
  int sig_num = 0;
  
  unsigned char *sig = sig_buf + cfg->sig_offset;
  while (fread(sig_buf, cfg->sig_record_size, 1, fp) > 0) {
    int slice_pos = 0;
    for (int i = 0; i < num_slices; i++) {
      int width = slice_width(cfg->sig_width, num_slices, i);
      int val = get_slice_at(sig, slice_pos, width);
            
      issl_counts[i][val]++;
      
      slice_pos += width;
    }
    sig_num++;
  }
  free(sig_buf);
    
  *signature_count = sig_num;
  
  return issl_counts;
}

static int ***Build_ISSL_Table(const SignatureHeader *cfg, FILE *fp, int num_slices, int **issl_counts)
{
  int ***issl_table = malloc(sizeof(int *) * num_slices);
  
  size_t mem_required = 0;
  for (int i = 0; i < num_slices; i++) {
    int width = slice_width(cfg->sig_width, num_slices, i);
    int num_issl_lists = 1 << width;
    issl_table[i] = malloc(sizeof(int *) * num_issl_lists);
    
    for (int j = 0; j < num_issl_lists; j++) {
      mem_required += sizeof(int) * issl_counts[i][j];
    }
  }
  
  int *linear_buffer = malloc(mem_required);

  if (!linear_buffer) {
    fprintf(stderr, "Ran out of memory generating ISSL table.\n");
    double mb = (double)mem_required / 1048576.0;
    fprintf(stderr, "Memory required for table: %.2f MB\n", mb);
    exit(1);
  }
  size_t linear_buffer_pos = 0;
  
  for (int i = 0; i < num_slices; i++) {
    int width = slice_width(cfg->sig_width, num_slices, i);
    int num_issl_lists = 1 << width;
    for (int j = 0; j < num_issl_lists; j++) {
      issl_table[i][j] = linear_buffer + linear_buffer_pos;
      linear_buffer_pos += issl_counts[i][j];
    }
    memset(issl_counts[i], 0, sizeof(int) * num_issl_lists);
  }
  
  // Rewind to just after the header
  fseek(fp, cfg->header_size, SEEK_SET);
  
  unsigned char *sig_buf = malloc(cfg->sig_record_size);
  unsigned char *sig = sig_buf + cfg->sig_offset;
  int sig_num = 0;
  while (fread(sig_buf, cfg->sig_record_size, 1, fp) > 0) {
    int slice_pos = 0;
    for (int i = 0; i < num_slices; i++) {
      int width = slice_width(cfg->sig_width, num_slices, i);
      int val = get_slice_at(sig, slice_pos, width);
      issl_table[i][val][issl_counts[i][val]] = sig_num;
      issl_counts[i][val]++;
      
      slice_pos += width;
    }
    sig_num++;
  }
  free(sig_buf);
  
  return issl_table;
}

void RunCreateISL()
{
  int avg_slice_width = 16;
  if (Config("ISL_SLICEWIDTH")) {
    avg_slice_width = atoi(Config("ISL_SLICEWIDTH"));
    if ((avg_slice_width <= 0) || (avg_slice_width >= 31)) {
      fprintf(stderr, "Error: slice widths outside of the range 1-30 are currently not supported.\n");
      exit(1);
    }
  }
  
  FILE *fp = fopen(Config("SIGNATURE-PATH"), "rb");
  if (!fp) {
    fprintf(stderr, "Failed to open signature file.\n");
    exit(1);
  }
  SignatureHeader sig_cfg = readSigHeader(fp);
  
  // Number of slices is calculated as ceil(signature width / ideal slice width)
  int num_slices = (sig_cfg.sig_width + avg_slice_width - 1) / avg_slice_width;
  
  int signature_count;
    
  timer T = timer_start();
  int **issl_counts = Count_ISSL_List_Lengths(&sig_cfg, fp, num_slices, &signature_count);
    
  fprintf(stderr, "ISSL table generation: first pass - %.2fms\n", timer_tick(&T));
  int ***issl_table = Build_ISSL_Table(&sig_cfg, fp, num_slices, issl_counts);
  fprintf(stderr, "ISSL table generation: second pass - %.2fms\n", timer_tick(&T));
  fclose(fp);
  
  // Write out ISSL table

  FILE *fo = fopen(Config("ISL-PATH"), "wb");
  if (!fo) {
    fprintf(stderr, "Failed to write out ISSL table\n");
    exit(1);
  }
  fprintf(stderr, "Writing %d signatures\n", signature_count);
  
  file_write32(num_slices, fo);
  file_write32(0, fo); // compression
  file_write32(2, fo); // storage mode
  file_write32(signature_count, fo); // signatures
  file_write32(avg_slice_width, fo); // average slice width
  file_write32(sig_cfg.sig_width, fo); // signature width
  
  for (int slice = 0; slice < num_slices; slice++) {
    int width = slice_width(sig_cfg.sig_width, num_slices, slice);
    int num_issl_lists = 1 << width;
    
    fwrite(issl_counts[slice], sizeof(int), num_issl_lists, fo);
  }
  for (int slice = 0; slice < num_slices; slice++) {
    int width = slice_width(sig_cfg.sig_width, num_slices, slice);
    int num_issl_lists = 1 << width;
    
    for (int val = 0; val < num_issl_lists; val++) {
      fwrite(issl_table[slice][val], sizeof(int), issl_counts[slice][val], fo);
    }
  }

  fclose(fo);
  
  fprintf(stderr, "ISSL table generation: writing - %.2fms\n", timer_tick(&T));
  fprintf(stderr, "ISSL table generation: total time - %.2fms\n", get_total_time(&T));
  
  free(issl_counts);
  free(issl_table);
}

typedef struct {
  int num_slices;
  int compression;
  int storage_mode;
  int signature_count;
  int avg_slice_width;
  int sig_width;
} ISSLHeader;

static ISSLHeader Read_ISSL_Table(const char *path, int ***issl_counts_out, int ****issl_table_out)
{
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    fprintf(stderr, "Unable to open ISSL table for reading.\n");
    exit(1);
  }
  
  ISSLHeader cfg;
  cfg.num_slices = file_read32(fp);
  cfg.compression = file_read32(fp);
  
  if (cfg.compression != 0) {
    fprintf(stderr, "Error: compression of ISSL table not supported.\n");
    exit(1);
  }
  cfg.storage_mode = file_read32(fp);
  if (cfg.storage_mode != 2) {
    fprintf(stderr, "Error: storage modes other than 2 not supported.\n");
    exit(1);
  }
  cfg.signature_count = file_read32(fp);
  cfg.avg_slice_width = file_read32(fp);
  cfg.sig_width = file_read32(fp);
  
  // Allocate initial structure
  int **issl_counts = malloc(sizeof(int *) * cfg.num_slices);
  int ***issl_table = malloc(sizeof(int *) * cfg.num_slices);
    
  // Load table
  size_t issl_counts_sz = 0;
  for (int slice = 0; slice < cfg.num_slices; slice++) {
    int width = slice_width(cfg.sig_width, cfg.num_slices, slice);
    int num_issl_lists = 1 << width;
    issl_table[slice] = malloc(sizeof(int *) * num_issl_lists);
    
    issl_counts_sz += sizeof(int) * num_issl_lists;
  }
  int *issl_counts_buffer = malloc(issl_counts_sz);
  if (!issl_counts_buffer) {
    double mb = (double)issl_counts_sz / 1048576.0;
    fprintf(stderr, "Error: unable to allocate memory for ISSL counts array (%.2f MB)\n", mb);
    exit(1);
  }
  size_t issl_counts_pos = 0;
    
  size_t issl_table_sz = 0;
  for (int slice = 0; slice < cfg.num_slices; slice++) {
    int width = slice_width(cfg.sig_width, cfg.num_slices, slice);
    int num_issl_lists = 1 << width;
    
    issl_counts[slice] = issl_counts_buffer + issl_counts_pos;

    fread(issl_counts[slice], sizeof(int), num_issl_lists, fp);
    
    for (int val = 0; val < num_issl_lists; val++) {
      //fprintf(stderr, "--- %d %d\n", (int)sizeof(int), issl_counts[slice][val]);
      issl_table_sz += sizeof(int) * issl_counts[slice][val];
    }
    
    issl_counts_pos += num_issl_lists;
  }
    
  int *issl_table_buffer = malloc(issl_table_sz);
  if (!issl_table_buffer) {
    double mb = (double)issl_table_sz / 1048576.0;
    fprintf(stderr, "Error: unable to allocate memory for ISSL table (%.2f MB)\n", mb);
    exit(1);
  }
  size_t issl_table_pos = 0;
  for (int slice = 0; slice < cfg.num_slices; slice++) {
    int width = slice_width(cfg.sig_width, cfg.num_slices, slice);
    int num_issl_lists = 1 << width;

    for (int val = 0; val < num_issl_lists; val++) {
      issl_table[slice][val] = issl_table_buffer + issl_table_pos;
      fread(issl_table[slice][val], sizeof(int), issl_counts[slice][val], fp);
      issl_table_pos += issl_counts[slice][val];
    }
  }  
  fclose(fp);
  
  *issl_counts_out = issl_counts;
  *issl_table_out = issl_table;
  
  return cfg;
}

static SignatureHeader Read_Signature_File(const char *path, unsigned char **buf, int sigs)
{
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    fprintf(stderr, "Unable to open signature file for reading.\n");
    exit(1);
  }
  
  SignatureHeader cfg = readSigHeader(fp);
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
  unsigned short *score;
  int *score_hotlist;
  int score_hotlist_n;
  int score_hotlist_sz;
} ScoreTable;

static ScoreTable Create_Score_Table(int signatures)
{
  ScoreTable S;
  S.score = malloc(sizeof(unsigned short) * signatures);
  memset(S.score, 0, sizeof(unsigned short) * signatures);
  S.score_hotlist_sz = DEFAULT_HOTLIST_BUFFERSIZE;
  S.score_hotlist = malloc(sizeof(int) * S.score_hotlist_sz);
  S.score_hotlist_n = 0;
  return S;
}
static void Destroy_Score_Table(ScoreTable *S)
{
  free(S->score);
  S->score = NULL;
  free(S->score_hotlist);
  S->score_hotlist = NULL;
  S->score_hotlist_n = 0;
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

static void Output_Results(int topic_id, const ResultList *list, int top_k)
{
  if (list->results < top_k) top_k = list->results;
  for (int i = 0; i < top_k; i++) {
    //printf("%d. %d (%d) - %s\n", i, list->docids[i], list->distances[i], list->docnames[i]);
    printf("%d Q0 %s %d %d Topsig-ISSL %d\n", topic_id, list->docnames[i], i + 1, 1000000 - i, list->distances[i]);
  }
}

ResultList Summarise(ScoreTable **scts, int scts_n, int top_k)
{
  int R_lowest_score = 0;
  int R_lowest_score_i = 0;
  
  if (scts[0]->score_hotlist_n < top_k) {
    //fprintf(stderr, "%d Error: not enough entries in hotlist (%d) to make up topk (%d). Reduce topk and try again.\n", threadid, scts[0]->score_hotlist_n, top_k);
    //exit(1);
    top_k = scts[0]->score_hotlist_n;
  }
  
  ResultList R = Create_Result_List(top_k);
  for (int sct_i = 0; sct_i < scts_n; sct_i++) {
    ScoreTable *sct = scts[sct_i];
  
    for (int i = 0; i < sct->score_hotlist_n; i++) {
      int docid = sct->score_hotlist[i];
      int score = sct->score[docid];
      
      if (score > R_lowest_score) {
        R.docids[R_lowest_score_i] = docid;
        R.issl_scores[R_lowest_score_i] = score;
        
        R_lowest_score = INT_MAX;
        for (int j = 0; j < R.results; j++) {
          if (R.issl_scores[j] < R_lowest_score) {
            R_lowest_score = R.issl_scores[j];
            R_lowest_score_i = j;
          }
        }
      }
      
      sct->score[docid] = 0;
    }
    sct->score_hotlist_n = 0;
  }
  
  return R;
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


static void ISSLPseudo(const SignatureHeader *cfg, ResultList *list, const unsigned char *sig_file, unsigned char *sig_out, int sample)
{
    double dsig[cfg->sig_width];
    memset(&dsig, 0, cfg->sig_width * sizeof(double));
    double sample_2 = ((double)sample) * 8;
    
    for (int i = 0; i < sample; i++) {
        double di = i;
        const unsigned char *cursig = sig_file + (size_t)cfg->sig_record_size * list->docids[i] + cfg->sig_offset;
        for (int j = 0; j < cfg->sig_width; j++) {
            dsig[j] += exp(-di*di/sample_2) * ((cursig[j/8] & (1 << (7 - (j%8))))>0?1.0:-1.0); 
        }
    }
    
    Signature *sig = NewSignature("pseudo-query");
    SignatureFillDoubles(sig, dsig);
    
    //unsigned char bsig[cfg->sig_width / 8];
    unsigned char bmask[cfg->sig_width / 8];
    
    FlattenSignature(sig, sig_out, bmask);

    SignatureDestroy(sig);    
}

static void Clarify_Results(const SignatureHeader *cfg, ResultList *list, const unsigned char *sig_file, const unsigned char *sig, int top_k)
{
  if (top_k == -1) top_k = list->results;
  if (top_k > list->results) top_k = list->results;
  static unsigned char *mask = NULL;
  if (!mask) {
    mask = malloc(cfg->sig_width / 8);
    memset(mask, 0xFF, cfg->sig_width / 8);
  }
  
  //list->docnames = malloc(sizeof(char *) * list->results);
  
  ClarifyEntry *clarify = malloc(sizeof(ClarifyEntry) * top_k);
  
  for (int i = 0; i < top_k; i++) {
    const unsigned char *cursig = sig_file + ((size_t)cfg->sig_record_size * list->docids[i] + cfg->sig_offset);
    list->distances[i] = DocumentDistance(cfg->sig_width, sig, mask, cursig);
    list->docnames[i] = (const char *)(sig_file + ((size_t)cfg->sig_record_size * list->docids[i]));
    
    clarify[i].list = list;
    clarify[i].i = i;
  }
  qsort(clarify, top_k, sizeof(ClarifyEntry), clarify_compar);
  
  ResultList newlist = Create_Result_List(list->results);  
  //newlist.docnames = malloc(sizeof(char *) * list->results);
  for (int i = 0; i < list->results; i++) {
    int j;
    if (i < top_k) {
      j = clarify[i].i;
    } else {
      j = i;
    }
    newlist.issl_scores[i] = list->issl_scores[j];
    newlist.distances[i] = list->distances[j];
    newlist.docids[i] = list->docids[j];
    newlist.docnames[i] = list->docnames[j];
  }
  
  Destroy_Result_List(list);
  *list = newlist;
  
  free(clarify);
}

static inline int count_bits(unsigned int v) {
  v = v - ((v >> 1) & 0x55555555);
  v = (v & 0x33333333) + ((v >> 2) & 0x33333333);
  return (((v + (v >> 4)) & 0xF0F0F0F) * 0x1010101) >> 24;
}

static void Traverse_ISSL(const int *count, int * const *issl, ScoreTable *scores, const int *variants, int n_variants_ceasenew, int n_variants, int val, int slice_width)
{
  int limit = 1 << slice_width;
  for (int variant = 0; variant < n_variants; variant++) {
    if (variant >= n_variants_ceasenew) break;
    int score = slice_width - count_bits(variants[variant]);
    int value = val ^ variants[variant];
    if (value >= limit) continue;
    for (int i = 0; i < count[value]; i++) {
      int docid = issl[value][i];
      if (scores->score[docid] == 0) {
        if (scores->score_hotlist_n == scores->score_hotlist_sz) {
          scores->score_hotlist_sz *= 2;
          scores->score_hotlist = realloc(scores->score_hotlist, sizeof(scores->score_hotlist[0]) * scores->score_hotlist_sz);
        }
        scores->score_hotlist[scores->score_hotlist_n++] = docid;
      }
      scores->score[docid] += score;
    }
  }
}

static int bitcount_compar(const void *A, const void *B)
{
  const int *a = A;
  const int *b = B;
  
  return count_bits(*a) - count_bits(*b);
}

static inline int variants_threshold(const int *variants, int n_variants, int threshold)
{
  for (int i = 0; i < n_variants; i++) {
    if (count_bits(variants[i]) > threshold) {
      return i;
    }
  }
  return n_variants;
}

typedef struct {
  const SignatureHeader *sig_cfg;
  const ISSLHeader *issl_cfg;

  const unsigned char *sig_file;
  int doc_begin;
  int doc_end;
  int * const *issl_counts;
  int ** const *issl_table;
  //ScoreTable scores;
  struct {
    int *variants;
    int n_variants_stopearly;
    int n_variants_ceasenew;
  } v;
  int top_k;
  ResultList *output;
} Worker_Throughput;

typedef struct {
  int threadid;
  ScoreTable scores;
} Worker_Throughput_perthread;

static void *Throughput_Job(void *input, void *thread_data)
{
  Worker_Throughput *T = input;
  Worker_Throughput_perthread *TP = thread_data;
  
  int doc_count = T->doc_end - T->doc_begin;
  
  const SignatureHeader *sig_cfg = T->sig_cfg;
  const ISSLHeader *issl_cfg = T->issl_cfg;
  const unsigned char *sig_file = T->sig_file;
  int * const *issl_counts = T->issl_counts;
  int ** const *issl_table = T->issl_table;
  int *variants = T->v.variants;
  int n_variants_stopearly = T->v.n_variants_stopearly;
  int n_variants_ceasenew = T->v.n_variants_ceasenew;
  T->output = malloc(sizeof(ResultList) * doc_count);
  int top_k = T->top_k;
  unsigned char *pseudo_sig = malloc(sig_cfg->sig_record_size);
  
  ScoreTable *scores = &TP->scores;
  
  int doc_i = 0;
  for (int doc_cmp = T->doc_begin; doc_cmp < T->doc_end; doc_cmp++) {
    const unsigned char *sig = sig_file + (size_t)sig_cfg->sig_record_size * doc_cmp + sig_cfg->sig_offset;
    int slice_pos = 0;
    for (int slice = 0; slice < issl_cfg->num_slices; slice++) {
      int width = slice_width(issl_cfg->sig_width, issl_cfg->num_slices, slice);
      int val = get_slice_at(sig, slice_pos, width);
      //fprintf(stderr, "Slice %d val %d (@%d,%d)\n", slice, val, slice_pos, width);
      Traverse_ISSL(issl_counts[slice], issl_table[slice], scores, variants, n_variants_ceasenew, n_variants_stopearly, val, width);
      slice_pos += width;
      //fprintf(stderr, "Score of cmp: %d\n", scores.score[doc_cmp]);

    }
    
    ScoreTable *sct[1] = {scores};
    T->output[doc_i] = Summarise(sct, 1, top_k);
    Clarify_Results(sig_cfg, &T->output[doc_i], sig_file, sig, -1);
    if (0) {
      ISSLPseudo(sig_cfg, &T->output[doc_i], sig_file, pseudo_sig, 3);
      Clarify_Results(sig_cfg, &T->output[doc_i], sig_file, pseudo_sig, 10);
    }
    doc_i++;
  }
  
  return NULL;
}

void RunSearchISLTurbo()
{
  int **issl_counts;
  int ***issl_table;
  
  ISSLHeader issl_cfg = Read_ISSL_Table(Config("ISL-PATH"), &issl_counts, &issl_table);
  unsigned char *sig_file;
  SignatureHeader sig_cfg = Read_Signature_File(Config("SIGNATURE-PATH"), &sig_file, issl_cfg.signature_count);
  
  int n_variants = 1 << issl_cfg.avg_slice_width;
  int *variants = malloc(sizeof(int) * n_variants);
  for (int i = 0; i < n_variants; i++) {
    variants[i] = i;
  }
  qsort(variants, n_variants, sizeof(int), bitcount_compar);
  
  int stop_early = 3; //3
  int cease_new = 1;  //1
  
  if (Config("ISL-MAX-DIST"))
    stop_early = atoi(Config("ISL-MAX-DIST"));
  if (Config("ISL-MAX-DIST-NONEW"))
    cease_new = atoi(Config("ISL-MAX-DIST-NONEW"));

  int n_variants_stopearly = variants_threshold(variants, n_variants, stop_early);
  int n_variants_ceasenew = variants_threshold(variants, n_variants, cease_new);
  
  //fprintf(stderr, "n_variants_stopearly %d\n", n_variants_stopearly);
  //fprintf(stderr, "n_variants_ceasenew %d\n", n_variants_ceasenew);
  
  int thread_count = 1;
  
  if (Config("SEARCH-DOC-THREADS")) {
    thread_count = atoi(Config("SEARCH-DOC-THREADS"));
  }
  
  int job_count;
  if (thread_count > 1)
    job_count = thread_count * 10;
  else
    job_count = 1;
    
  if (Config("SEARCH-DOC-JOBS")) {
    job_count = atoi(Config("SEARCH-DOC-JOBS"));
  }
  int search_doc_first = 0;
  int search_doc_last = issl_cfg.signature_count - 1;
  
  if (Config("SEARCH-DOC-FIRST"))
    search_doc_first = atoi(Config("SEARCH-DOC-FIRST"));
  if (Config("SEARCH-DOC-LAST"))
    search_doc_last = atoi(Config("SEARCH-DOC-LAST"));
    
  fprintf(stderr, "search_doc_last %d\n", search_doc_last);
  
  if (search_doc_first < 0) {
    fprintf(stderr, "ERROR: SEARCH_DOC_FIRST value (%d) out of range.\n", search_doc_first);
    exit(1);
  }
  if (search_doc_last >= issl_cfg.signature_count) {
    fprintf(stderr, "ERROR: SEARCH_DOC_LAST value (%d) out of range (%d).\n", search_doc_last, issl_cfg.signature_count);
    exit(1);
  }
  if (search_doc_last < search_doc_first) {
    fprintf(stderr, "ERROR: SEARCH_DOC_LAST (%d) lower than SEARCH_DOC_FIRST (%d).\n", search_doc_last, search_doc_first);
    exit(1);
  }
  
  int top_k_rerank = 10;
  int top_k_present = 10;
  
  if (Config("SEARCH-DOC-TOPK"))
    top_k_present = atoi(Config("SEARCH-DOC-TOPK"));
  if (Config("SEARCH-DOC-RERANK"))
    top_k_rerank = atoi(Config("SEARCH-DOC-RERANK"));
  
  if (top_k_rerank < top_k_present) top_k_rerank = top_k_present;
  
  int total_docs = search_doc_last - search_doc_first + 1;
  
  void **jobdata = malloc(sizeof(void *) * job_count);
  void **threaddata = malloc(sizeof(void *) * thread_count);
  for (int i = 0; i < job_count; i++) {
    Worker_Throughput *per_job_data = malloc(sizeof(Worker_Throughput));
    per_job_data->sig_cfg = &sig_cfg;
    per_job_data->issl_cfg = &issl_cfg;
    per_job_data->sig_file = sig_file;
    per_job_data->doc_begin = total_docs * i / job_count + search_doc_first;
    per_job_data->doc_end = total_docs * (i+1) / job_count + search_doc_first;
    per_job_data->issl_counts = issl_counts;
    per_job_data->issl_table = issl_table;
    per_job_data->v.variants = variants;
    per_job_data->v.n_variants_stopearly = n_variants_stopearly;
    per_job_data->v.n_variants_ceasenew = n_variants_ceasenew;
    //per_job_data->scores = Create_Score_Table(issl_cfg.signature_count);
    per_job_data->top_k = top_k_rerank;
    jobdata[i] = per_job_data;
  }
  for (int i = 0; i < thread_count; i++) {
    Worker_Throughput_perthread *per_thread_data = malloc(sizeof(Worker_Throughput));
    per_thread_data->scores = Create_Score_Table(issl_cfg.signature_count);
    per_thread_data->threadid = i;
    threaddata[i] = per_thread_data;
  }
  
  timer T = timer_start();
  
  //DivideWork(threads, Throughput_Job, thread_count);
  DivideWorkTP(jobdata, threaddata, Throughput_Job, job_count, thread_count);
  
  for (int i = 0; i < job_count; i++) {
    Worker_Throughput *thread_data = jobdata[i];
    int doc_count = thread_data->doc_end - thread_data->doc_begin;
    for (int j = 0; j < doc_count; j++) {
      Output_Results(thread_data->doc_begin + j, &thread_data->output[j], top_k_present);
    }
  }
  
  fprintf(stderr, "search time %.2fms\n", timer_tick(&T));
  
  for (int i = 0; i < job_count; i++) {
    free(jobdata[i]);
  }
  for (int i = 0; i < thread_count; i++) {
    Destroy_Score_Table(&((Worker_Throughput_perthread *)threaddata[i])->scores);
    free(threaddata[i]);
  }
  free(jobdata);
  free(threaddata);
}

void ExperimentalRerankTopFile()
{
  fprintf(stderr, "Unimplemented.\n");
}
