#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include "topsig-config.h"
#include "topsig-search.h"
#include "topsig-global.h"
#include "topsig-signature.h"
#include "topsig-stem.h"
#include "topsig-stop.h"
#include "topsig-thread.h"
#include "superfasthash.h"

#include "uthash.h"

//#define HEAP_RESULTLIST

struct DocumentClass {
  char docId[32];
  char cls[32];
  UT_hash_handle hh;
};

struct Search {
  FILE *sig;
  int cache_size;
  unsigned char *cache;
  int entire_file_cached;
  int sigs_cached;

  SignatureCache *sigcache;

  struct {
    char method[64];
    int length;
    int density;
    int docnamelen;
    int headersize;
    int seed;

    int charmask[256];

    int multithreading;
    int threads;

    int pseudofeedback;
    int termWeightSuffixes;

    enum {
      SCALING_FACTOR_DEFAULT,
      SCALING_FACTOR_1,
      SCALING_FACTOR_1_I,
      SCALING_FACTOR_1_SQRT,
      SCALING_FACTOR_1_HAMMING,
      SCALING_FACTOR_1_SQRT_HAMMING,
      SCALING_FACTOR_E_NI
    } prf_scalingfactor;
    enum {
      PSEUDO_RERANK_DEFAULT,
      PSEUDO_RERANK_FREEZING,
      PSEUDO_RERANK_RESIDUAL
    } prf_reranktype;
    int prf_rerankiters;
    int prf_negfeedback;
    int prf_ignorefirst;

    struct DocumentClass *fb_classTable;

    int OnlyConsiderRelevant;
    int MaxRelevantSamples;
    int MaxIrrelevantSamples;
  } cfg;
};

struct Result;
#ifdef HEAP_RESULTLIST
struct ResultHash {
  struct Result *ptr;
  unsigned int docId_hash;
  UT_hash_handle hh;
};
#endif /* HEAP_RESULTLIST */

struct Result {
  char *docId;
  unsigned int docId_hash;
  unsigned char *signature;
  int dist;
  int qual;
  int offset_begin;
  int offset_end;

  #ifdef HEAP_RESULTLIST
  struct ResultHash *h;
  #endif /* HEAP_RESULTLIST */
};

struct Results {
  #ifdef HEAP_RESULTLIST
  struct ResultHash *htable;
  #endif /* HEAP_RESULTLIST */

  int k;
  struct Result res[1];
};

void RemoveTopKResults(Results *R, int k, int ignorefirst);

// Initialise a search handle to be used for searching a collection multiple times with minimal delay
Search *InitSearch()
{
  Search *S = malloc(sizeof(Search));

  S->sigcache = NewSignatureCache(0, 0);

  S->sig = fopen(Config("SIGNATURE-PATH"), "rb");
  if (!S->sig) {
    fprintf(stderr, "Signature file could not be loaded.\n");
    exit(1);
  }
  
  /*
  char *C = Config("SIGNATURE-CACHE-SIZE");
  if (C == NULL) {
    fprintf(stderr, "SIGNATURE-CACHE-SIZE unspecified\n");
    exit(1);
  }
  S->cache_size = atoi(C);
  */
  // Default 128mb signature cache
  S->cache_size = GetIntegerConfig("SIGNATURE-CACHE-SIZE", 128);

  S->cache = malloc((size_t)S->cache_size * 1024 * 1024);
  if (!S->cache) {
    fprintf(stderr, "Unable to allocate signature cache\n");
    exit(1);
  }

  if (strcmp_lc(Config("SEARCH-THREADING"), "multi") == 0) {
    S->cfg.multithreading = 1;

    S->cfg.threads = atoi(Config("SEARCH-THREADS"));
    if (S->cfg.threads <= 0) {
      fprintf(stderr, "Invalid SEARCH-THREADS value\n");
      exit(1);
    }
  } else {
    S->cfg.multithreading = 0;
  }

  const char *C = Config("PSEUDO-FEEDBACK-SAMPLE");
  S->cfg.pseudofeedback = 0;
  if (C) {
    S->cfg.pseudofeedback = atoi(C);
  }

  S->cfg.termWeightSuffixes = GetBooleanConfig("TERMWEIGHT-SUFFIXES", 0);

  // Read header info from signature file

  S->cfg.headersize = fileRead32(S->sig); // header-size
  int version = fileRead32(S->sig); // version
  S->cfg.docnamelen = fileRead32(S->sig); // maxnamelen
  S->cfg.length = fileRead32(S->sig); // sig_width
  S->cfg.density = fileRead32(S->sig); // sig_density
  S->cfg.seed = 0;
  if (version >= 2) {
    S->cfg.seed = fileRead32(S->sig); // sig_seed
  }
  fread(S->cfg.method, 1, 64, S->sig); // sig_method

  // Override the config file settings with the new values

  char buf[256];
  sprintf(buf, "%d", S->cfg.length);
  OverrideConfigParam("SIGNATURE-WIDTH", buf);

  sprintf(buf, "%d", S->cfg.density);
  OverrideConfigParam("SIGNATURE-DENSITY", buf);

  sprintf(buf, "%d", S->cfg.seed);
  OverrideConfigParam("SIGNATURE-SEED", buf);

  sprintf(buf, "%d", S->cfg.docnamelen);
  OverrideConfigParam("MAX-DOCNAME-LENGTH", buf);

  OverrideConfigParam("SIGNATURE-METHOD", S->cfg.method);

  ConfigInit();
  
  const char *charmask = GetOptionalConfig("CHARMASK", "alnum");

  if (strcmp_lc(charmask,"alpha")==0)
    for (int i = 0; i < 256; i++) S->cfg.charmask[i] = isalpha(i);
  if (strcmp_lc(charmask,"alnum")==0)
    for (int i = 0; i < 256; i++) S->cfg.charmask[i] = isalnum(i);
  if (strcmp_lc(charmask,"all")==0)
    for (int i = 0; i < 256; i++) S->cfg.charmask[i] = isgraph(i);

  S->cfg.prf_scalingfactor = SCALING_FACTOR_DEFAULT;
  C = Config("PSEUDO-FEEDBACK-SCALEFACTOR");
  if (C) {
    if (strcmp_lc(C, "default")==0)
      S->cfg.prf_scalingfactor = SCALING_FACTOR_DEFAULT;
    if (strcmp_lc(C, "1")==0)
      S->cfg.prf_scalingfactor = SCALING_FACTOR_1;
    if (strcmp_lc(C, "1_i")==0)
      S->cfg.prf_scalingfactor = SCALING_FACTOR_1_I;
    if (strcmp_lc(C, "1_sqrt_i")==0)
      S->cfg.prf_scalingfactor = SCALING_FACTOR_1_SQRT;
    if (strcmp_lc(C, "1_hamming")==0)
      S->cfg.prf_scalingfactor = SCALING_FACTOR_1_HAMMING;
    if (strcmp_lc(C, "1_sqrt_hamming")==0)
      S->cfg.prf_scalingfactor = SCALING_FACTOR_1_SQRT_HAMMING;
    if (strcmp_lc(C, "e_-i")==0)
      S->cfg.prf_scalingfactor = SCALING_FACTOR_E_NI;
  }

  S->cfg.prf_reranktype = PSEUDO_RERANK_DEFAULT;
  C = Config("PSEUDO-FEEDBACK-METHOD");
  if (C) {
    if (strcmp_lc(C, "default")==0)
      S->cfg.prf_reranktype = PSEUDO_RERANK_DEFAULT;
    if (strcmp_lc(C, "freezing")==0)
      S->cfg.prf_reranktype = PSEUDO_RERANK_FREEZING;
    if (strcmp_lc(C, "residual")==0)
      S->cfg.prf_reranktype = PSEUDO_RERANK_RESIDUAL;
  }

  S->cfg.prf_rerankiters = GetIntegerConfig("PSEUDO-FEEDBACK-ITERATIONS", 1);
  S->cfg.prf_negfeedback = GetIntegerConfig("PSEUDO-FEEDBACK-NEGSAMPLE", 0);
  S->cfg.prf_ignorefirst = GetIntegerConfig("PSEUDO-FEEDBACK-IGNOREFIRST", 0);

  S->cfg.fb_classTable = NULL;
  C = Config("FEEDBACK-DOCUMENT-CLASSES");
  if (C) {
    FILE *fp = fopen(C, "r");
    for (;;) {
      struct DocumentClass *D = malloc(sizeof(struct DocumentClass));
      if (fscanf(fp, "%s %s\n", D->docId, D->cls) < 2) {
        free(D);
        break;
      }
      HASH_ADD_STR(S->cfg.fb_classTable, docId, D);
    }
    fclose(fp);
  }

  S->cfg.OnlyConsiderRelevant = GetIntegerConfig("FEEDBACK-ONLY-RELEVANT", 0);
  S->cfg.MaxRelevantSamples = GetIntegerConfig("FEEDBACK-MAX-RELEVANT", INT_MAX);
  S->cfg.MaxIrrelevantSamples = GetIntegerConfig("FEEDBACK-MAX-IRRELEVANT", INT_MAX);



  S->entire_file_cached = -1;
  return S;
}

Signature *CreateQuerySignature(Search *S, const char *query)
{
  Signature *sig = NewSignature("query");

  char term[TERM_MAX_LEN+1];
  const char *p = query;

  const char *termstart = NULL;
  do {
    if (S->cfg.charmask[(int)*p]) {
      if (!termstart) {
        termstart = p;
      }
    } else {
      if (termstart) {
        strncpy(term, termstart, p-termstart);
        term[p-termstart] = '\0';
        strToLower(term);
        Stem(term);

        if (!IsStopword(term)) {
          SignatureAdd(S->sigcache, sig, term, 1, 3, S->cfg.termWeightSuffixes);
        }
        termstart = NULL;
      }
    }
    p++;
  } while (*(p-1) != '\0');

  return sig;
}

int DocumentDistance_popcnt3(int sigwidth, const unsigned char *in_bsig, const unsigned char *in_bmask, const unsigned char *in_dsig)
{
  int c = 0;

  #ifdef IS64BIT
  const unsigned long long *query_sig = (const unsigned long long *)in_bsig;
  const unsigned long long *mask_sig = (const unsigned long long *)in_bmask;
  const unsigned long long *doc_sig = (const unsigned long long *)in_dsig;
  for (unsigned int i = 0; i < sigwidth / 8 / sizeof(unsigned long long); i++) {
    c += __builtin_popcountll((doc_sig[i] ^ query_sig[i]) & mask_sig[i]);
  }
  #else
  const unsigned int *query_sig = (const unsigned int *)in_bsig;
  const unsigned int *mask_sig = (const unsigned int *)in_bmask;
  const unsigned int *doc_sig = (const unsigned int *)in_dsig;
  for (unsigned int i = 0; i < sigwidth / 8 / sizeof(unsigned int); i++) {
    c += __builtin_popcount((doc_sig[i] ^ query_sig[i]) & mask_sig[i]);
  }
  #endif

  return c;
}

int DocumentDistance(int sigwidth, const unsigned char *in_bsig, const unsigned char *in_bmask, const unsigned char *in_dsig) {
  return DocumentDistance_popcnt3(sigwidth,in_bsig, in_bmask,in_dsig);
}


static int get_document_quality(unsigned char *signature_header_vals)
{
  // the 'quality' field is the 4th field in the header
  return memRead32(signature_header_vals + (3 * 4));
}

static int get_document_offset_begin(unsigned char *signature_header_vals)
{
  // the 'offset_begin' field is the 5th field in the header
  return memRead32(signature_header_vals + (4 * 4));
}
static int get_document_offset_end(unsigned char *signature_header_vals)
{
  // the 'offset_end' field is the 6th field in the header
  return memRead32(signature_header_vals + (5 * 4));
}

int result_compar(const void *a, const void *b)
{
  const struct Result *A, *B;
  A = a;
  B = b;

  if (A->dist > B->dist) return 1;
  if (A->dist < B->dist) return -1;
  if (A->qual < B->qual) return 1;
  if (A->qual > B->qual) return -1;
  if (A->docId_hash > B->docId_hash) return 1;
  if (A->docId_hash < B->docId_hash) return -1;
  int sc = strcmp(A->docId, B->docId);
  if (sc != 0) return sc;
  if (A->offset_begin > B->offset_begin) return 1;
  if (A->offset_begin < B->offset_begin) return -1;
  return 0;
}

void ApplyClassFeedback(Search *S, Results *R, char *docName)
{
  int sample = S->cfg.pseudofeedback;
  for (int pf_iteration = 0; pf_iteration < S->cfg.prf_rerankiters; pf_iteration++) {

    double dsig[S->cfg.length];
    memset(&dsig, 0, S->cfg.length * sizeof(double));
    double sample_2 = ((double)sample) * 8;

    struct DocumentClass *queryClass;
    HASH_FIND_STR(S->cfg.fb_classTable, docName, queryClass);

    int num_relevant_samples = 0;
    int num_irrelevant_samples = 0;
    for (int i = 0; i < sample; i++) {
        double di = i;


        int isRelevant = 0;

        struct DocumentClass *docClass;
        HASH_FIND_STR(S->cfg.fb_classTable, R->res[i].docId, docClass);
        if (docClass) {
          if (strcmp(queryClass->cls, docClass->cls)==0) {

            isRelevant = 1;
          }
        }

        int AddToSignature = 1;

        if (S->cfg.OnlyConsiderRelevant) {
          if (!isRelevant) {
            AddToSignature = 0;
          }
        }

        if (isRelevant) {
          num_relevant_samples++;
          if (num_relevant_samples > S->cfg.MaxRelevantSamples) {
            AddToSignature = 0;
          }
        } else {
          num_irrelevant_samples++;
          if (num_irrelevant_samples > S->cfg.MaxIrrelevantSamples) {
            AddToSignature = 0;
          }
        }


        if (AddToSignature) {

          for (int j = 0; j < S->cfg.length; j++) {
              double s = (R->res[i].signature[j/8] & (1 << (7 - (j%8))))>0 ? 1.0 : -1.0;
              int dist = R->res[i].dist;

              if (!isRelevant) {
                s *= -1.0;
              }

              double scale;

              switch (S->cfg.prf_scalingfactor) {

                case SCALING_FACTOR_1:
                  scale = 1.0;
                  break;
                case SCALING_FACTOR_1_I:
                  scale = 1.0/(di+1);
                  break;
                case SCALING_FACTOR_1_SQRT:
                  scale = 1.0/(sqrt(di+1));
                  break;
                case SCALING_FACTOR_1_HAMMING: /* same */
                  scale = 1.0/((double)dist);
                  break;
                case SCALING_FACTOR_1_SQRT_HAMMING: /* same */
                  scale = 1.0/sqrt((double)dist);
                  break;
                case SCALING_FACTOR_E_NI: /* same */
                  scale = exp(-(di + 1));
                  break;
                case SCALING_FACTOR_DEFAULT:
                default:
                  scale = exp(-di*di/sample_2);
                  break;
              }

              dsig[j] += scale * s;
          }

        }
    }

    Signature *sig = NewSignature("query");
    SignatureFillDoubles(sig, dsig);

    unsigned char bsig[S->cfg.length / 8];
    unsigned char bmask[S->cfg.length / 8];

    FlattenSignature(sig, bsig, bmask);

    memset(bmask, 255, S->cfg.length / 8);

    int rerank_k = GetIntegerConfig("PSEUDO-FEEDBACK-RERANK", 0);

    int rerank_start = 0;

    for (int i = rerank_start; i < rerank_k; i++) {
        R->res[i].dist = DocumentDistance(S->cfg.length, bsig, bmask, R->res[i].signature);
    }

    qsort(&R->res[rerank_start], rerank_k-rerank_start, sizeof(R->res[0]), result_compar);

    SignatureDestroy(sig);
  }
}

void ApplyBlindFeedback(Search *S, Results *R, int sample)
{
  int igf = S->cfg.prf_ignorefirst;
  for (int pf_iteration = 0; pf_iteration < S->cfg.prf_rerankiters; pf_iteration++) {
    double dsig[S->cfg.length];
    memset(&dsig, 0, S->cfg.length * sizeof(double));
    double sample_2 = ((double)sample) * 8;

    for (int i = 0 + igf; i < sample + igf; i++) {
        double di = (i - igf);
        for (int j = 0; j < S->cfg.length; j++) {
            double s = (R->res[i].signature[j/8] & (1 << (7 - (j%8))))>0 ? 1.0 : -1.0;
            int dist = R->res[i].dist;

            double scale;

            switch (S->cfg.prf_scalingfactor) {

              case SCALING_FACTOR_1:
                scale = 1.0;
                break;
              case SCALING_FACTOR_1_I:
                scale = 1.0/(di+1);
                break;
              case SCALING_FACTOR_1_SQRT:
                scale = 1.0/(sqrt(di+1));
                break;
              case SCALING_FACTOR_1_HAMMING: /* same */
                scale = 1.0/((double)dist);
                break;
              case SCALING_FACTOR_1_SQRT_HAMMING: /* same */
                scale = 1.0/sqrt((double)dist);
                break;
              case SCALING_FACTOR_E_NI: /* same */
                scale = exp(-(di + 1));
                break;
              case SCALING_FACTOR_DEFAULT:
              default:
                scale = exp(-di*di/sample_2);
                break;
            }

            dsig[j] += scale * s;
        }
    }

    // negative feedback
    for (int i = R->k - S->cfg.prf_negfeedback; i < R->k; i++) {
        double di = R->k - 1 - i;

        for (int j = 0; j < S->cfg.length; j++) {
            double s = (R->res[i].signature[j/8] & (1 << (7 - (j%8))))>0 ? 1.0 : -1.0;
            int dist = R->res[i].dist;

            double scale;

            switch (S->cfg.prf_scalingfactor) {
              case SCALING_FACTOR_1:
                scale = 1.0;
                break;
              case SCALING_FACTOR_1_I:
                scale = 1.0/(di+1);
                break;
              case SCALING_FACTOR_1_SQRT:
                scale = 1.0/(sqrt(di+1));
                break;
              case SCALING_FACTOR_1_HAMMING:
                scale = 1.0/((double)dist);
                break;
              case SCALING_FACTOR_1_SQRT_HAMMING:
                scale = 1.0/sqrt((double)dist);
                break;
              case SCALING_FACTOR_E_NI:
                scale = exp(-(di + 1));
                break;
              case SCALING_FACTOR_DEFAULT:
              default:
                scale = exp(-di*di/sample_2);
                break;
            }
            dsig[j] -= scale * s;
        }
    }

    // With residual ranking, the sampled documents are removed from the result list before reranking
    if (S->cfg.prf_reranktype == PSEUDO_RERANK_RESIDUAL) {
      RemoveTopKResults(R, sample, igf);
    }

    Signature *sig = NewSignature("query");
    SignatureFillDoubles(sig, dsig);

    unsigned char bsig[S->cfg.length / 8];
    unsigned char bmask[S->cfg.length / 8];

    FlattenSignature(sig, bsig, bmask);

    memset(bmask, 255, S->cfg.length / 8);

    int rerank_k = GetIntegerConfig("PSEUDO-FEEDBACK-RERANK", 0);

    int rerank_start = 0;

    // With freezing, the sampled documents are not reranked
    if (S->cfg.prf_reranktype == PSEUDO_RERANK_FREEZING) {
      rerank_start = sample;
    }
    rerank_start += igf;
    rerank_k += igf;

    for (int i = rerank_start; i < rerank_k; i++) {
        R->res[i].dist = DocumentDistance(S->cfg.length, bsig, bmask, R->res[i].signature);
    }

    qsort(&R->res[rerank_start], rerank_k-rerank_start, sizeof(R->res[0]), result_compar);

    SignatureDestroy(sig);
  }
}

void ApplyFeedback(Search *S, Results *R, const char *feedback, int k)
{
    Signature *sig = CreateQuerySignature(S, feedback);

    if (R->k < k) k = R->k;

    unsigned char bsig[S->cfg.length / 8];
    unsigned char bmask[S->cfg.length / 8];
    FlattenSignature(sig, bsig, bmask);

    for (int i = 0; i < k; i++) {
        R->res[i].dist = DocumentDistance(S->cfg.length, bsig, bmask, R->res[i].signature);
    }
    qsort(R->res, k, sizeof(R->res[0]), result_compar);

    SignatureDestroy(sig);
}

void MergeResults(Results *base, Results *add)
{
  int duplicates_ok = GetBooleanConfig("ALLOW-DUPLICATES", 0);
  
  struct Result res[base->k + add->k];
  for (int i = 0; i < base->k; i++) {
    res[i] = base->res[i];
  }
  for (int i = 0; i < add->k; i++) {
    res[i+base->k] = add->res[i];
  }

  for (int i = 0; i < base->k + add->k; i++) {
    for (int j = i + 1; j < base->k + add->k; j++) {
      if (res[j].docId_hash == res[i].docId_hash && strcmp(res[j].docId, res[i].docId) == 0 && !duplicates_ok) {
        // Punish the lowest ranker to reduce its position in the merged set
        if (result_compar(&res[j], &res[i]) > 0) {
          res[j].dist = INT_MAX;
        } else {
          res[i].dist = INT_MAX;
        }
      }
    }
  }

  qsort(res, base->k + add->k, sizeof(res[0]), result_compar);

  for (int i = base->k; i < base->k + add->k; i++) {
    free(res[i].docId);
    free(res[i].signature);
  }
  free(add);

  for (int i = 0; i < base->k; i++) {
    base->res[i] = res[i];
  }
}

Results *FindHighestScoring(Search *S, const int start, const int count, const int topk, unsigned char *bsig, unsigned char *bmask)
{
  Results *R = InitialiseResults(S, topk);
  return FindHighestScoring_ReuseResults(S, R, start, count, topk, bsig, bmask);
}

Results *InitialiseResults(Search *S, const int topk)
{
  Results *R = malloc(sizeof(Results) - sizeof(struct Result) + sizeof(struct Result)*topk);
  R->k = topk;
  #ifdef HEAP_RESULTLIST
  R->htable = NULL;
  #endif /* HEAP_RESULTLIST */
  for (int i = 0; i < topk; i++) {
    R->res[i].docId = malloc(S->cfg.docnamelen + 1);
    R->res[i].signature = malloc(S->cfg.length / 8);
    R->res[i].dist = INT_MAX;
    R->res[i].docId[0] = '_';
    R->res[i].docId[1] = '\0';
    #ifdef HEAP_RESULTLIST
    R->res[i].h = NULL;
    #endif /* HEAP_RESULTLIST */
  }
  return R;
}

#ifdef HEAP_RESULTLIST

void HeapSwap(struct Result *a, struct Result *b)
{
  struct Result tmp = *a;
  *a = *b;
  *b = tmp;
  if (a->h) a->h->ptr = a;
  if (b->h) b->h->ptr = b;
}

void HeapDel(Results *R)
{
  int n = 0;

  HeapSwap(&R->res[n], &R->res[R->k-1]);
  for (;;) {
    if (2*n >= R->k) break; // done
    int swap = -1;
    // If node n is better than one of its children, swap with that child
    if (result_compar(&R->res[2*n], &R->res[n]) > 0) {
      swap = 2*n;
    } else if ((2*n+1 < R->k) && result_compar(&R->res[2*n+1], &R->res[n]) > 0) {
      swap = 2*n+1;
    } else break; // done
    HeapSwap(&R->res[n], &R->res[swap]);
    n = swap;
  }

  if (R->res[R->k-1].h) {
    HASH_DEL(R->htable, R->res[R->k-1].h);
    free(R->res[R->k-1].h);
  }
}

void HeapAdd(Search *S, Results *R, struct Result new_res, int duplicates_ok)
{
  if (result_compar(&R->res[0], &new_res) > 0) {
    struct ResultHash *h = NULL;
    int n = -1;
    HASH_FIND_INT(R->htable, &new_res.docId_hash, h);
    if (h && strncmp(h->ptr->docId, new_res.docId, S->cfg.docnamelen)!=0)
      h = NULL;
    if (!h) {
      HeapDel(R);
      n = R->k-1;
      R->res[n].docId_hash = new_res.docId_hash;
      R->res[n].offset_begin = new_res.offset_begin;
      R->res[n].offset_end = new_res.offset_end;
      R->res[n].dist = new_res.dist;
      R->res[n].qual = new_res.qual;
      strncpy(R->res[n].docId, new_res.docId, S->cfg.docnamelen + 1);
      memcpy(R->res[n].signature, new_res.signature, S->cfg.length / 8);
      if (duplicates_ok) {
        h = malloc(sizeof(struct ResultHash));
        h->ptr = &R->res[n];
        h->docId_hash = new_res.docId_hash;
        HASH_ADD_INT(R->htable, docId_hash, h);
      }
    } else {
      if (result_compar(h->ptr, &new_res) > 0) {
        h->ptr->dist = new_res.dist;
        h->ptr->qual = new_res.qual;
        n = h->ptr - &R->res[0];
      }
    }
    if (n != -1) {
      for (;;) {
        int swap;
        if (n > 0 && result_compar(&R->res[n], &R->res[n/2]) > 0) {
          // If n is worse than its parent, swap up
          swap = n/2;
        } else if (2*n < R->k && result_compar(&R->res[2*n], &R->res[n]) > 0) {
          // If n's left child is worse than n, swap down
          swap = 2*n;
        } else if (2*n+1 < R->k && result_compar(&R->res[2*n+1], &R->res[n]) > 0) {
          // If n's right child is worse than n, swap down
          swap = 2*n+1;
        } else break;
        HeapSwap(&R->res[n], &R->res[swap]);
      }
    }
  }
}

#endif /* HEAP_RESULTLIST */

Results *FindHighestScoring_ReuseResults(Search *S, Results *R, const int start, const int count, const int topk, unsigned char *bsig, unsigned char *bmask)
{
  // Calculate the size of each signature record and the offsets to the docId and signature strings
  size_t sig_record_size = S->cfg.docnamelen + 1;
  sig_record_size += 8 * 4; // 8 32-bit ints
  size_t docId_offset = 0;
  size_t sig_offset = sig_record_size;
  int sig_length_bytes = S->cfg.length / 8;
  sig_record_size += sig_length_bytes;

  int i;
  int duplicates_ok = 0;

  int lowest_j = 0;

  for (int j = 0; j < topk; j++) {
    if (result_compar(&R->res[j], &R->res[lowest_j]) > 0) {
      lowest_j = j;
    }
  }

  duplicates_ok = GetBooleanConfig("ALLOW-DUPLICATES", 0);
  for (i = start; i < start+count; i++) {
    unsigned char *signature_header = S->cache + sig_record_size * i;
    unsigned char *signature_header_vals = signature_header + S->cfg.docnamelen + 1;
    unsigned char *signature = S->cache + sig_record_size * i + sig_offset;

    int dist = DocumentDistance(S->cfg.length, bsig, bmask, signature);
    int qual = get_document_quality(signature_header_vals);
    int offset_begin = get_document_offset_begin(signature_header_vals);
    int offset_end = get_document_offset_end(signature_header_vals);

    char *docId = (char *)(signature_header + docId_offset);
    unsigned int docId_hash = SuperFastHash(docId, strlen(docId));

    #ifndef HEAP_RESULTLIST

    struct Result new_res;
    new_res.docId = docId;
    new_res.docId_hash = docId_hash;
    new_res.signature = signature;
    new_res.dist = dist;
    new_res.qual = qual;
    new_res.offset_begin = offset_begin;
    new_res.offset_end = offset_end;

    if (result_compar(&R->res[lowest_j], &new_res) > 0) {
      int duplicate_found = -1;
      for (int j = 0; j < topk; j++) {
        if (R->res[j].docId_hash == docId_hash && strncmp(R->res[j].docId, docId, S->cfg.docnamelen) == 0 && !duplicates_ok) {
          duplicate_found = j;
          break;
        }
      }

      int dirty = 0;
      if (duplicate_found == -1) {
        if (result_compar(&R->res[lowest_j], &new_res) > 0) {
          R->res[lowest_j].docId_hash = docId_hash;
          R->res[lowest_j].dist = dist;
          R->res[lowest_j].qual = qual;
          R->res[lowest_j].offset_begin = offset_begin;
          R->res[lowest_j].offset_end = offset_end;
          strncpy(R->res[lowest_j].docId, docId, S->cfg.docnamelen + 1);
          memcpy(R->res[lowest_j].signature, signature, S->cfg.length / 8);
          dirty = 1;
        }
      } else {
        if (result_compar(&R->res[duplicate_found], &new_res) > 0) {
          R->res[duplicate_found].docId_hash = docId_hash;
          R->res[duplicate_found].dist = dist;
          R->res[duplicate_found].qual = qual;
          R->res[duplicate_found].offset_begin = offset_begin;
          R->res[duplicate_found].offset_end = offset_end;
          strncpy(R->res[duplicate_found].docId, docId, S->cfg.docnamelen + 1);
          memcpy(R->res[duplicate_found].signature, signature, S->cfg.length / 8);
          if (duplicate_found == lowest_j) dirty = 1;
        }
      }
      if (dirty) {
        for (int j = 0; j < topk; j++) {
          if (result_compar(&R->res[j], &R->res[lowest_j])>0) {
            lowest_j = j;
          }
        }
      }
    }
    #else /* HEAP_RESULTLIST */
    struct Result new_res;
    new_res.docId = docId;
    new_res.docId_hash = docId_hash;
    new_res.signature = signature;
    new_res.dist = dist;
    new_res.qual = qual;
    new_res.offset_begin = offset_begin;
    new_res.offset_end = offset_end;

    HeapAdd(S, R, new_res, duplicates_ok);

    #endif /* HEAP_RESULTLIST */
  }
  return R;
}

static void freeresult(struct Result *R)
{
  free(R->docId);
  free(R->signature);
  #ifdef HEAP_RESULTLIST
  if (R->h)
    free(R->h);
  #endif /* HEAP_RESULTLIST */
}

// This should only be called if the list has already been sorted
static void removeNullResults(Results *R)
{
  int delMode = 0;
  int deletedResults = 0;
  for (int i = 0; i < R->k; i++) {
    if (R->res[i].dist == INT_MAX) {
      delMode = 1;
    }
    if (delMode == 1) {
      deletedResults++;
      freeresult(&R->res[i]);
    }
  }
  R->k -= deletedResults;
}

Results *SearchCollection(Search *S, Signature *sig, const int topk)
{
  Results *R = NULL;

  unsigned char bsig[S->cfg.length / 8];
  unsigned char bmask[S->cfg.length / 8];

  FlattenSignature(sig, bsig, bmask);

  // Calculate the size of each signature record
  size_t sig_record_size = S->cfg.docnamelen + 1;
  sig_record_size += 8 * 4; // 8 32-bit ints
  sig_record_size += S->cfg.length / 8;

  // Determine the maximum number of signatures that can fit in the allocated cache
  size_t max_cached_sigs = (size_t)S->cache_size * 1024 * 1024 / sig_record_size;

  int reached_end = 0;
  while (!reached_end) {
    if (S->entire_file_cached != 1) {
      // Read as many signatures from the file as possible
      fprintf(stderr, "Reading from signature file... ");fflush(stderr);
      size_t sigs_read = fread(S->cache, sig_record_size, max_cached_sigs, S->sig);
      fprintf(stderr, "done\n");fflush(stderr);
      if (S->entire_file_cached == -1) {
        if (sigs_read < max_cached_sigs) {
          // As this is the first attempt at reading, we can assume that everything was read
          // and this means the signatures file will not need to be read in the future.
          S->entire_file_cached = 1;
          reached_end = 1;
        } else {
          // Everything was read so it is assumed that the signatures file is bigger than
          // the cache.
          S->entire_file_cached = 0;
        }
      } else {
        if (S->entire_file_cached == 0) {
          if (sigs_read < max_cached_sigs) {
            // The end of the file has been reached so rewind for the next read
            fseek(S->sig, S->cfg.headersize, SEEK_SET);
            reached_end = 1;
          }
        }
      }
      S->sigs_cached = sigs_read;
    } else {
      reached_end = 1;
    }

    Results *result;
    if (S->cfg.multithreading == 0) {
      result = FindHighestScoring(S, 0, S->sigs_cached, topk, bsig, bmask);
    } else {
      if (Config("SEARCH-JOBS") == NULL) {
        result = FindHighestScoring_Threaded(S, 0, S->sigs_cached, topk, bsig, bmask, S->cfg.threads);
      } else {
        result = FindHighestScoring_Threaded_X(S, 0, S->sigs_cached, topk, bsig, bmask, S->cfg.threads);
      }
    }

    if (R) {
      MergeResults(R, result);
    } else {
      R = result;
    }
  }

  qsort(R->res, topk, sizeof(R->res[0]), result_compar);
  
  removeNullResults(R);

  if (S->cfg.fb_classTable) {
    static int dinesha_queryid = 0;
    dinesha_queryid++;
    char docName[32];
    sprintf(docName, "%04d", dinesha_queryid);

    ApplyClassFeedback(S, R, docName);
  } else {

    if (S->cfg.pseudofeedback > 0) {
      ApplyBlindFeedback(S, R, S->cfg.pseudofeedback);
    }
  }

  return R;
}

Results *SearchCollectionQuery(Search *S, const char *query, const int topk)
{
  Signature *sig = CreateQuerySignature(S, query);

  Results *R = SearchCollection(S, sig, topk);
  SignatureDestroy(sig);
  return R;
}

void PrintResults(Results *R, int k)
{
  if (R->k < k) {
    k = R->k;
  }
  for (int i = 0; i < k; i++) {
    printf("%d. %s (%d)\n", i+1, R->res[i].docId, R->res[i].dist);
  }
}

void Writer_trec(FILE *out, const char *topic_id, Results *R)
{
  for (int i = 0; i < R->k; i++) {
    fprintf(out, "%s Q0 %s %d %d %s %d %d %d\n", topic_id, R->res[i].docId, i+1, 1000000-i, "Topsig", R->res[i].dist, R->res[i].offset_begin, R->res[i].offset_end);
  }
}

void FreeResults(Results *R)
{
  for (int i = 0; i < R->k; i++) {
    freeresult(&R->res[i]);
  }
  free(R);
}
void RemoveTopKResults(Results *R, int k, int ignorefirst)
{
  for (int i = ignorefirst; i < k+ignorefirst; i++) {
    freeresult(&R->res[i]);
  }
  for (int i = ignorefirst; i < R->k - k; i++) {
    R->res[i] = R->res[i + k];
  }
  R->k -= k;
}

void FreeSearch(Search *S)
{
  fclose(S->sig);
  DestroySignatureCache(S->sigcache);
  free(S->cache);
  free(S);
}

const char *GetResult(Results *R, int N)
{
  return R->res[N].docId;
}

void RemoveResult(Results *R, int N)
{
  freeresult(&R->res[N]);
  for (int i = N; i < R->k - 1; i++) {
    R->res[i] = R->res[i+1];
  }
  R->k--;
}
