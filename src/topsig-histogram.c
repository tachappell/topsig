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

typedef struct {
  int results;
  int *issl_scores;
  int *distances;
  int *docIds;
  const char **docnames;
} ResultList;

typedef struct {
  ResultList *list;
  int i;
} ClarifyEntry;


typedef struct {
  const SignatureHeader *sig_cfg;
  const unsigned char *sig_file;
  int doc_begin;
  int doc_end;
  ResultList *output;
} WorkerThroughput;

typedef struct {
  char fname[128];
  int hd;
  int topic;

  UT_hash_handle hh;
} ResultHash;

typedef struct {
  char docId[128];
  UT_hash_handle hh;
} CategorisedDoc;

typedef struct {
  char catname[256];
  CategorisedDoc *dochash;
  UT_hash_handle hh;
} Category;

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
    fprintf(stderr, "The path to a signature file must be provided with the -signature-path (signature file) option.\n");
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

void HistogramAdd(SignatureHeader *sig_cfg, const unsigned char *sig_file, int *histogram, const unsigned char *sig, const unsigned char *mask, ResultHash *H, int topic_only, int start_from, int must_be_in_table, CategorisedDoc *dochash, const char *sig_fname)
{
  CategorisedDoc *D1, *D2;
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

void RunHistogram()
{
  ResultHash *H = NULL;
  unsigned char *sig_file;
  SignatureHeader sig_cfg = readSigFile(Config("SIGNATURE-PATH"), &sig_file);
  size_t histogram_size = sizeof(int) * sig_cfg.sig_width;
  int *histogram = malloc(histogram_size);
  memset(histogram, 0x00, histogram_size);

  Category *cathash = NULL;
  CategorisedDoc *dochash = NULL;
  if (Config("HISTOGRAM-CATFILE")) {
    FILE *fp = fopen(Config("HISTOGRAM-CATFILE"), "r");
    char docId[128];
    char catname[256];

    while (fscanf(fp, "%s %[^\n]\n", docId, catname) == 2) {
      Category *C;
      HASH_FIND_STR(cathash, catname, C);
      if (!C) {
        C = malloc(sizeof(Category));
        strcpy(C->catname, catname);
        C->dochash = NULL;
        HASH_ADD_STR(cathash, catname, C);
      }
      CategorisedDoc *D;
      HASH_FIND_STR(C->dochash, docId, D);
      if (!D) {
        D = malloc(sizeof(CategorisedDoc));
        strcpy(D->docId, docId);
        HASH_ADD_STR(C->dochash, docId, D);
      }
    }
    fclose(fp);
    Category *C;
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
  if (strcmp_lc(Config("HISTOGRAM-TYPE"), "doc") == 0) {
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
        const char *fname = (const char *)(sig_file + (size_t)sig_cfg.sig_record_size * i);
        if (H) {
          ResultHash *f = NULL;

          HASH_FIND_STR(H, fname, f);
          if (!f || f->topic != cur_topic) {
            continue;
          }
          fprintf(stderr, "  - %d %s\n", i, fname);
        } else {
        }
        const unsigned char *sig = sig_file + (size_t)sig_cfg.sig_record_size * i + sig_cfg.sig_offset;
        HistogramAdd(&sig_cfg, sig_file, histogram, sig, mask, H, cur_topic, i + 1, 1, dochash, fname);
      }
    }
  } else if (strcmp_lc(Config("HISTOGRAM-TYPE"), "query") == 0) {
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
  for (int i = 0; i <= sig_cfg.sig_width; i++) {
    printf("%.32f\n", (double)histogram[i] / (double)xcount);
  }

  if (strcmp_lc(Config("HISTOGRAM-TYPE"), "query") == 0) {
    ResultHash *curr, *tmp;
    HASH_ITER(hh, H, curr, tmp) {
      printf("%s %d %.32f\n", curr->fname, curr->hd, (double)histogram[curr->hd] / (double)xcount);
    }
  }
}
