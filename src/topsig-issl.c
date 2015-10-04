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
#include "topsig-resultwriter.h"

// Starting size of the buffer used to hold the list of documents that were touched during the processing of the ISSL tables
#define DEFAULT_HOTLIST_BUFFERSIZE 2048

typedef struct {
  int header_size;
  int max_name_len;
  int sig_width;
  int sig_offset;
  int sig_record_size;
} SignatureHeader;

typedef struct {
  int num_slices;
  int compression;
  int storage_mode;
  int signature_count;
  int avg_slicewidth;
  int sig_width;
} ISSLHeader;

typedef struct {
  unsigned short *score;
  int *score_hotlist;
  int score_hotlist_n;
  int score_hotlist_sz;
} ScoreTable;

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
  //const char **docnames;
  Metadata *metadata;
} ResultList;

typedef struct {
  ResultList *list;
  int i;
} ClarifyEntry;

typedef struct {
  const SignatureHeader *sigCfg;
  const ISSLHeader *isslCfg;

  const unsigned char *sigFile;
  int doc_begin;
  int doc_end;
  int * const *isslCounts;
  int ** const *isslTable;

  struct {
    int *variants;
    int nVariantsExpansion;
    int nVariantsRadius;
  } v;
  
  int top_k;
  ResultList *output;
} WorkerThroughput;

typedef struct {
  int threadid;
  ScoreTable scores;
} WorkerThroughputThread;

static SignatureHeader readSigHeader(FILE *fp)
{
  SignatureHeader cfg;
  char sig_method[64];
  cfg.header_size = fileRead32(fp);
  int version = fileRead32(fp);
  cfg.max_name_len = fileRead32(fp);
  cfg.sig_width = fileRead32(fp); 
  fileRead32(fp); // Signature density
  if (version >= 2) {
    fileRead32(fp); // Signature seed
  }
  fread(sig_method, 1, 64, fp);

  cfg.sig_offset = cfg.max_name_len + 1;
  cfg.sig_offset += 8 * 4; // 8 32-bit ints
  cfg.sig_record_size = cfg.sig_offset + cfg.sig_width / 8;

  return cfg;
}

static inline int getSliceWidth(int sig_width, int slices, int slice_n)
{
  int pos_start = sig_width * slice_n / slices;
  int pos_end = sig_width * (slice_n+1) / slices;
  return pos_end - pos_start;
}

static inline int getSliceAt(const unsigned char *sig, int pos, int sw)
{
  int r = 0;
  for (int bit_n = 0; bit_n < sw; bit_n++) {
    int i = pos + bit_n;
    r = r | ( ((sig[i / 8] & (1 << (i % 8))) > 0) << bit_n );
  }
  return r;
}

static int **countListLengths(const SignatureHeader *cfg, FILE *fp, int num_slices, int *signature_count)
{
  // Allocate memory for the list lengths
  int **isslCounts = malloc(sizeof(int *) * num_slices);

  for (int i = 0; i < num_slices; i++) {
    int width = getSliceWidth(cfg->sig_width, num_slices, i);
    int num_issl_lists = 1 << width;
    isslCounts[i] = malloc(sizeof(int) * num_issl_lists);
    memset(isslCounts[i], 0, sizeof(int) * num_issl_lists);
  }

  // Rewind to just after the header
  fseek(fp, cfg->header_size, SEEK_SET);

  unsigned char *sig_buf = malloc(cfg->sig_record_size);
  int sig_num = 0;

  unsigned char *sig = sig_buf + cfg->sig_offset;
  while (fread(sig_buf, cfg->sig_record_size, 1, fp) > 0) {
    int slice_pos = 0;
    for (int i = 0; i < num_slices; i++) {
      int width = getSliceWidth(cfg->sig_width, num_slices, i);
      int val = getSliceAt(sig, slice_pos, width);

      isslCounts[i][val]++;

      slice_pos += width;
    }
    sig_num++;
  }
  free(sig_buf);

  *signature_count = sig_num;

  return isslCounts;
}

static int ***buildSliceTable(const SignatureHeader *cfg, FILE *fp, int num_slices, int **isslCounts)
{
  int ***isslTable = malloc(sizeof(int *) * num_slices);

  size_t mem_required = 0;
  for (int i = 0; i < num_slices; i++) {
    int width = getSliceWidth(cfg->sig_width, num_slices, i);
    int num_issl_lists = 1 << width;
    isslTable[i] = malloc(sizeof(int *) * num_issl_lists);

    for (int j = 0; j < num_issl_lists; j++) {
      mem_required += sizeof(int) * isslCounts[i][j];
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
    int width = getSliceWidth(cfg->sig_width, num_slices, i);
    int num_issl_lists = 1 << width;
    for (int j = 0; j < num_issl_lists; j++) {
      isslTable[i][j] = linear_buffer + linear_buffer_pos;
      linear_buffer_pos += isslCounts[i][j];
    }
    memset(isslCounts[i], 0, sizeof(int) * num_issl_lists);
  }

  // Rewind to just after the header
  fseek(fp, cfg->header_size, SEEK_SET);

  unsigned char *sig_buf = malloc(cfg->sig_record_size);
  unsigned char *sig = sig_buf + cfg->sig_offset;
  int sig_num = 0;
  while (fread(sig_buf, cfg->sig_record_size, 1, fp) > 0) {
    int slice_pos = 0;
    for (int i = 0; i < num_slices; i++) {
      int width = getSliceWidth(cfg->sig_width, num_slices, i);
      int val = getSliceAt(sig, slice_pos, width);
      isslTable[i][val][isslCounts[i][val]] = sig_num;
      isslCounts[i][val]++;

      slice_pos += width;
    }
    sig_num++;
  }
  free(sig_buf);

  return isslTable;
}

void CreateISSLTable()
{
  int avg_slicewidth = 16;
  if (Config("ISSL-SLICEWIDTH")) {
    avg_slicewidth = atoi(Config("ISSL-SLICEWIDTH"));
    if ((avg_slicewidth <= 0) || (avg_slicewidth >= 31)) {
      fprintf(stderr, "Error: slice widths outside of the range 1-30 are currently not supported.\n");
      exit(1);
    }
  }

  FILE *fp = fopen(GetMandatoryConfig("SIGNATURE-PATH", "The path to a signature file must be provided with the -signature-path (signature file) option."), "rb");
  if (!fp) {
    fprintf(stderr, "Failed to open signature file for reading.\n");
    exit(1);
  }
  SignatureHeader sigCfg = readSigHeader(fp);

  // Number of slices is calculated as ceil(signature width / ideal slice width)
  int num_slices = (sigCfg.sig_width + avg_slicewidth - 1) / avg_slicewidth;

  int signature_count;

  Timer T = StartTimer();
  int **isslCounts = countListLengths(&sigCfg, fp, num_slices, &signature_count);

  fprintf(stderr, "ISSL table generation: first pass - %.2fms\n", TickTimer(&T));
  int ***isslTable = buildSliceTable(&sigCfg, fp, num_slices, isslCounts);
  fprintf(stderr, "ISSL table generation: second pass - %.2fms\n", TickTimer(&T));
  fclose(fp);

  // Write out ISSL table

  FILE *fo = fopen(GetMandatoryConfig("ISSL-PATH", "The path to write the ISSL table to must be provided with the -issl-path (ISSL table) argument"), "wb");
  if (!fo) {
    fprintf(stderr, "Failed to write out ISSL table\n");
    exit(1);
  }
  fprintf(stderr, "Writing %d signatures\n", signature_count);

  fileWrite32(num_slices, fo);
  fileWrite32(0, fo); // compression
  fileWrite32(2, fo); // storage mode
  fileWrite32(signature_count, fo); // signatures
  fileWrite32(avg_slicewidth, fo); // average slice width
  fileWrite32(sigCfg.sig_width, fo); // signature width

  for (int slice = 0; slice < num_slices; slice++) {
    int width = getSliceWidth(sigCfg.sig_width, num_slices, slice);
    int num_issl_lists = 1 << width;

    fwrite(isslCounts[slice], sizeof(int), num_issl_lists, fo);
  }
  for (int slice = 0; slice < num_slices; slice++) {
    int width = getSliceWidth(sigCfg.sig_width, num_slices, slice);
    int num_issl_lists = 1 << width;

    for (int val = 0; val < num_issl_lists; val++) {
      fwrite(isslTable[slice][val], sizeof(int), isslCounts[slice][val], fo);
    }
  }

  fclose(fo);

  fprintf(stderr, "ISSL table generation: writing - %.2fms\n", TickTimer(&T));
  fprintf(stderr, "ISSL table generation: total time - %.2fms\n", GetTotalTime(&T));

  free(isslCounts);
  free(isslTable);
}

static ISSLHeader readSliceTable(const char *path, int ***isslCounts_out, int ****isslTable_out)
{
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    fprintf(stderr, "Unable to open ISSL table for reading.\n");
    exit(1);
  }

  ISSLHeader cfg;
  cfg.num_slices = fileRead32(fp);
  cfg.compression = fileRead32(fp);

  if (cfg.compression != 0) {
    fprintf(stderr, "Error: compression of ISSL table not supported.\n");
    exit(1);
  }
  cfg.storage_mode = fileRead32(fp);
  if (cfg.storage_mode != 2) {
    fprintf(stderr, "Error: storage modes other than 2 not supported.\n");
    exit(1);
  }
  cfg.signature_count = fileRead32(fp);
  cfg.avg_slicewidth = fileRead32(fp);
  cfg.sig_width = fileRead32(fp);

  // Allocate initial structure
  int **isslCounts = malloc(sizeof(int *) * cfg.num_slices);
  int ***isslTable = malloc(sizeof(int *) * cfg.num_slices);

  // Load table
  size_t isslCounts_sz = 0;
  for (int slice = 0; slice < cfg.num_slices; slice++) {
    int width = getSliceWidth(cfg.sig_width, cfg.num_slices, slice);
    int num_issl_lists = 1 << width;
    isslTable[slice] = malloc(sizeof(int *) * num_issl_lists);

    isslCounts_sz += sizeof(int) * num_issl_lists;
  }
  int *isslCounts_buffer = malloc(isslCounts_sz);
  if (!isslCounts_buffer) {
    double mb = (double)isslCounts_sz / 1048576.0;
    fprintf(stderr, "Error: unable to allocate memory for ISSL counts array (%.2f MB)\n", mb);
    exit(1);
  }
  size_t isslCounts_pos = 0;

  size_t isslTable_sz = 0;
  for (int slice = 0; slice < cfg.num_slices; slice++) {
    int width = getSliceWidth(cfg.sig_width, cfg.num_slices, slice);
    int num_issl_lists = 1 << width;

    isslCounts[slice] = isslCounts_buffer + isslCounts_pos;

    fread(isslCounts[slice], sizeof(int), num_issl_lists, fp);

    for (int val = 0; val < num_issl_lists; val++) {
      isslTable_sz += sizeof(int) * isslCounts[slice][val];
    }

    isslCounts_pos += num_issl_lists;
  }

  int *isslTable_buffer = malloc(isslTable_sz);
  if (!isslTable_buffer) {
    double mb = (double)isslTable_sz / 1048576.0;
    fprintf(stderr, "Error: unable to allocate memory for ISSL table (%.2f MB)\n", mb);
    exit(1);
  }
  size_t isslTable_pos = 0;
  for (int slice = 0; slice < cfg.num_slices; slice++) {
    int width = getSliceWidth(cfg.sig_width, cfg.num_slices, slice);
    int num_issl_lists = 1 << width;

    for (int val = 0; val < num_issl_lists; val++) {
      isslTable[slice][val] = isslTable_buffer + isslTable_pos;
      fread(isslTable[slice][val], sizeof(int), isslCounts[slice][val], fp);
      isslTable_pos += isslCounts[slice][val];
    }
  }
  fclose(fp);

  *isslCounts_out = isslCounts;
  *isslTable_out = isslTable;

  return cfg;
}

static SignatureHeader readSigFile(const char *path, unsigned char **buf, int sigs)
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

static ScoreTable createScoreTable(int signatures)
{
  ScoreTable S;
  S.score = malloc(sizeof(unsigned short) * signatures);
  memset(S.score, 0, sizeof(unsigned short) * signatures);
  S.score_hotlist_sz = DEFAULT_HOTLIST_BUFFERSIZE;
  S.score_hotlist = malloc(sizeof(int) * S.score_hotlist_sz);
  S.score_hotlist_n = 0;
  return S;
}
static void destroyScoreTable(ScoreTable *S)
{
  free(S->score);
  S->score = NULL;
  free(S->score_hotlist);
  S->score_hotlist = NULL;
  S->score_hotlist_n = 0;
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

static void writeResults(FILE *fp, int topicId, const char *topicName, const ResultList *list, int top_k)
{
  const char *format = GetOptionalConfig("RESULTS-FORMAT", "%T Q0 %D %r %s Topsig-ISSL %h\\n");
  if (list->results < top_k) top_k = list->results;
  for (int i = 0; i < top_k; i++) {
    //printf("%d Q0 %s %d %d Topsig-ISSL %d\n", topicId, list->metadata[i].docname, i + 1, 1000000 - i, list->distances[i]);
    //WriteResult(stdout, "%d Q0 %s %d %d Topsig-ISSL %d\n");

    WriteResult(fp, format, topicId, topicName, list->docids[i], list->metadata[i].docname, i + 1, 1000000 - i, list->distances[i], list->metadata[i].uniqueTerms, list->metadata[i].documentCharLength, list->metadata[i].totalTerms, list->metadata[i].quality, list->metadata[i].offsetBegin, list->metadata[i].offsetEnd, list->metadata[i].unused7, list->metadata[i].unused8);
  }
}

static ResultList summarise(ScoreTable **scts, int scts_n, int top_k)
{
  int R_lowest_score = 0;
  int R_lowest_score_i = 0;

  if (scts[0]->score_hotlist_n < top_k) {
    top_k = scts[0]->score_hotlist_n;
  }

  ResultList R = createResultList(top_k);
  for (int sct_i = 0; sct_i < scts_n; sct_i++) {
    ScoreTable *sct = scts[sct_i];

    for (int i = 0; i < sct->score_hotlist_n; i++) {
      int docId = sct->score_hotlist[i];
      int score = sct->score[docId];

      if (score > R_lowest_score) {
        R.docids[R_lowest_score_i] = docId;
        R.issl_scores[R_lowest_score_i] = score;

        R_lowest_score = INT_MAX;
        for (int j = 0; j < R.results; j++) {
          if (R.issl_scores[j] < R_lowest_score) {
            R_lowest_score = R.issl_scores[j];
            R_lowest_score_i = j;
          }
        }
      }

      sct->score[docId] = 0;
    }
    sct->score_hotlist_n = 0;
  }

  return R;
}

static int compareResultLists(const void *A, const void *B)
{
  const ClarifyEntry *a = A;
  const ClarifyEntry *b = B;

  const ResultList *list = a->list;

  return list->distances[a->i] - list->distances[b->i];
}


static void applyPseudoFeedback(const SignatureHeader *cfg, ResultList *list, const unsigned char *sigFile, unsigned char *sig_out, int sample)
{
    double dsig[cfg->sig_width];
    memset(&dsig, 0, cfg->sig_width * sizeof(double));
    double sample_2 = ((double)sample) * 8;

    for (int i = 0; i < sample; i++) {
        double di = i;
        const unsigned char *cursig = sigFile + (size_t)cfg->sig_record_size * list->docids[i] + cfg->sig_offset;
        for (int j = 0; j < cfg->sig_width; j++) {
            dsig[j] += exp(-di*di/sample_2) * ((cursig[j/8] & (1 << (7 - (j%8))))>0?1.0:-1.0);
        }
    }

    Signature *sig = NewSignature("pseudo-query");
    SignatureFillDoubles(sig, dsig);

    unsigned char bmask[cfg->sig_width / 8];

    FlattenSignature(sig, sig_out, bmask);

    SignatureDestroy(sig);
}

static void clarifyResults(const SignatureHeader *cfg, ResultList *list, const unsigned char *sigFile, const unsigned char *sig, int top_k)
{
  if (top_k == -1) top_k = list->results;
  if (top_k > list->results) top_k = list->results;
  static unsigned char *mask = NULL;
  if (!mask) {
    mask = malloc(cfg->sig_width / 8);
    memset(mask, 0xFF, cfg->sig_width / 8);
  }

  ClarifyEntry *clarify = malloc(sizeof(ClarifyEntry) * top_k);

  for (int i = 0; i < top_k; i++) {
    const unsigned char *cursig = sigFile + ((size_t)cfg->sig_record_size * list->docids[i] + cfg->sig_offset);
    list->distances[i] = DocumentDistance(cfg->sig_width, sig, mask, cursig);
    
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
  qsort(clarify, top_k, sizeof(ClarifyEntry), compareResultLists);

  ResultList newlist = createResultList(list->results);

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
    newlist.metadata[i] = list->metadata[j];
  }

  destroyResultList(list);
  *list = newlist;

  free(clarify);
}

static inline int countBits(unsigned int v) {
  v = v - ((v >> 1) & 0x55555555);
  v = (v & 0x33333333) + ((v >> 2) & 0x33333333);
  return (((v + (v >> 4)) & 0xF0F0F0F) * 0x1010101) >> 24;
}

static void traverseSliceTable(const int *count, int * const *issl, ScoreTable *scores, const int *variants, int nVariantsRadius, int nVariants, int val, int getSliceWidth)
{
  int limit = 1 << getSliceWidth;
  for (int variant = 0; variant < nVariants; variant++) {
    if (variant >= nVariantsRadius) break;
    int score = getSliceWidth - countBits(variants[variant]);
    int value = val ^ variants[variant];
    if (value >= limit) continue;
    for (int i = 0; i < count[value]; i++) {
      int docId = issl[value][i];
      if (scores->score[docId] == 0) {
        if (scores->score_hotlist_n == scores->score_hotlist_sz) {
          scores->score_hotlist_sz *= 2;
          scores->score_hotlist = realloc(scores->score_hotlist, sizeof(scores->score_hotlist[0]) * scores->score_hotlist_sz);
        }
        scores->score_hotlist[scores->score_hotlist_n++] = docId;
      }
      scores->score[docId] += score;
    }
  }
}

static int compareHammingDistance(const void *A, const void *B)
{
  const int *a = A;
  const int *b = B;

  return countBits(*a) - countBits(*b);
}

static inline int getVariantsThreshold(const int *variants, int nVariants, int threshold)
{
  for (int i = 0; i < nVariants; i++) {
    if (countBits(variants[i]) > threshold) {
      return i;
    }
  }
  return nVariants;
}

static void *runThreadedSearch(void *input, void *thread_data)
{
  WorkerThroughput *T = input;
  WorkerThroughputThread *TP = thread_data;

  int doc_count = T->doc_end - T->doc_begin;

  const SignatureHeader *sigCfg = T->sigCfg;
  const ISSLHeader *isslCfg = T->isslCfg;
  const unsigned char *sigFile = T->sigFile;
  int * const *isslCounts = T->isslCounts;
  int ** const *isslTable = T->isslTable;
  int *variants = T->v.variants;
  int nVariantsExpansion = T->v.nVariantsExpansion;
  int nVariantsRadius = T->v.nVariantsRadius;
  T->output = malloc(sizeof(ResultList) * doc_count);
  int top_k = T->top_k;
  unsigned char *pseudo_sig = malloc(sigCfg->sig_record_size);

  ScoreTable *scores = &TP->scores;

  int doc_i = 0;
  for (int doc_cmp = T->doc_begin; doc_cmp < T->doc_end; doc_cmp++) {
    const unsigned char *sig = sigFile + (size_t)sigCfg->sig_record_size * doc_cmp + sigCfg->sig_offset;
    int slice_pos = 0;
    for (int slice = 0; slice < isslCfg->num_slices; slice++) {
      int width = getSliceWidth(isslCfg->sig_width, isslCfg->num_slices, slice);
      int val = getSliceAt(sig, slice_pos, width);

      traverseSliceTable(isslCounts[slice], isslTable[slice], scores, variants, nVariantsRadius, nVariantsExpansion, val, width);
      slice_pos += width;

    }

    ScoreTable *sct[1] = {scores};
    T->output[doc_i] = summarise(sct, 1, top_k);
    clarifyResults(sigCfg, &T->output[doc_i], sigFile, sig, -1);
    if (0) {
      applyPseudoFeedback(sigCfg, &T->output[doc_i], sigFile, pseudo_sig, 3);
      clarifyResults(sigCfg, &T->output[doc_i], sigFile, pseudo_sig, 10);
    }
    doc_i++;
  }

  return NULL;
}

void SearchISSLTable()
{
  int **isslCounts;
  int ***isslTable;
  
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


  ISSLHeader isslCfg = readSliceTable(GetMandatoryConfig("ISSL-PATH", "The path to the ISSL table must be provided through the -issl-path (ISSL table) argument"), &isslCounts, &isslTable);
  unsigned char *sigFile;
  SignatureHeader sigCfg = readSigFile(GetMandatoryConfig("SIGNATURE-PATH", "The path to the signature file must be provided through the -signature-path (signature file) argument"), &sigFile, isslCfg.signature_count);

  int nVariants = 1 << isslCfg.avg_slicewidth;
  int *variants = malloc(sizeof(int) * nVariants);
  for (int i = 0; i < nVariants; i++) {
    variants[i] = i;
  }
  qsort(variants, nVariants, sizeof(int), compareHammingDistance);

  int maxExpansionDistance = GetIntegerConfig("ISSL-EXPANSION", 3);
  int maxConsiderationRadius = GetIntegerConfig("ISSL-CONSIDERATION-RADIUS", 1);

  int nVariantsExpansion = getVariantsThreshold(variants, nVariants, maxExpansionDistance);
  int nVariantsRadius = getVariantsThreshold(variants, nVariants, maxConsiderationRadius);

  int threadCount = GetIntegerConfig("THREADS", 1);
  
  if (threadCount < 0) {
    fprintf(stderr, "ERROR: THREADS value (%d) out of range.\n", threadCount);
    exit(1);
  }

  int jobCount = GetIntegerConfig("JOBS", 0);
  
  // Default of 16 jobs per thread if there are multiple threads.
  if (jobCount <= 0) {
    if (threadCount != 1) {
      jobCount = threadCount * 16;
    } else {
      jobCount = 1;
    }
  }

  int search_doc_first = GetIntegerConfig("SEARCH-DOC-FIRST", 0);
  int search_doc_last = GetIntegerConfig("SEARCH-DOC-LAST", isslCfg.signature_count - 1);

  if (search_doc_first < 0) {
    fprintf(stderr, "ERROR: SEARCH_DOC_FIRST value (%d) out of range.\n", search_doc_first);
    exit(1);
  }
  if (search_doc_last >= isslCfg.signature_count) {
    fprintf(stderr, "ERROR: SEARCH_DOC_LAST value (%d) out of range (%d).\n", search_doc_last, isslCfg.signature_count);
    exit(1);
  }
  if (search_doc_last < search_doc_first) {
    fprintf(stderr, "ERROR: SEARCH_DOC_LAST (%d) lower than SEARCH_DOC_FIRST (%d).\n", search_doc_last, search_doc_first);
    exit(1);
  }

  int top_k_present = GetIntegerConfig("K", 10);
  int top_k_rerank = GetIntegerConfig("K-OUTPUT", top_k_present);

  if (top_k_rerank < top_k_present) top_k_rerank = top_k_present;

  int total_docs = search_doc_last - search_doc_first + 1;

  void **jobdata = malloc(sizeof(void *) * jobCount);
  void **threaddata = malloc(sizeof(void *) * threadCount);
  for (int i = 0; i < jobCount; i++) {
    WorkerThroughput *perJobData = malloc(sizeof(WorkerThroughput));
    perJobData->sigCfg = &sigCfg;
    perJobData->isslCfg = &isslCfg;
    perJobData->sigFile = sigFile;
    perJobData->doc_begin = total_docs * i / jobCount + search_doc_first;
    perJobData->doc_end = total_docs * (i+1) / jobCount + search_doc_first;
    perJobData->isslCounts = isslCounts;
    perJobData->isslTable = isslTable;
    perJobData->v.variants = variants;
    perJobData->v.nVariantsExpansion = nVariantsExpansion;
    perJobData->v.nVariantsRadius = nVariantsRadius;
    perJobData->top_k = top_k_rerank;
    jobdata[i] = perJobData;
  }
  for (int i = 0; i < threadCount; i++) {
    WorkerThroughputThread *per_thread_data = malloc(sizeof(WorkerThroughput));
    per_thread_data->scores = createScoreTable(isslCfg.signature_count);
    per_thread_data->threadid = i;
    threaddata[i] = per_thread_data;
  }

  Timer T = StartTimer();

  DivideWorkTP(jobdata, threaddata, runThreadedSearch, jobCount, threadCount);

  for (int i = 0; i < jobCount; i++) {
    WorkerThroughput *thread_data = jobdata[i];
    int doc_count = thread_data->doc_end - thread_data->doc_begin;
    for (int j = 0; j < doc_count; j++) {
      int topicid = thread_data->doc_begin + j;
      const char *docname = (const char *)(sigFile + sigCfg.sig_record_size * topicid);
      writeResults(fo, topicid, docname, &thread_data->output[j], top_k_present);
    }
  }

  fprintf(stderr, "Search time: %.2fms\n", TickTimer(&T));

  for (int i = 0; i < jobCount; i++) {
    free(jobdata[i]);
  }
  for (int i = 0; i < threadCount; i++) {
    destroyScoreTable(&((WorkerThroughputThread *)threaddata[i])->scores);
    free(threaddata[i]);
  }
  free(jobdata);
  free(threaddata);
}
