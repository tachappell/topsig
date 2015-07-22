#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <limits.h>
#include "topsig-signature.h"
#include "topsig-config.h"
#include "topsig-atomic.h"
#include "topsig-global.h"
#include "topsig-thread.h"
#include "ISAAC-rand.h"
#include "uthash.h"
#include "topsig-semaphore.h"
#include "topsig-stats.h"

typedef struct {
  UT_hash_handle hh;

  int hits;
  char term[TERM_MAX_LEN+1];
  int S[1];
} CachedTerm;

struct SignatureCache {
  CachedTerm *cacheMap;
  CachedTerm **cacheList;
  int cachePosition;
  int isWriterThread;
};

static void initCache(); //forward declaration

struct Signature {
  char *id;
  int uniqueTerms;
  int documentCharLength;
  int totalTerms;
  int quality;
  int offsetBegin;
  int offsetEnd;
  int unused7;
  int unused8;

  int S[1];
};

static struct {
  int length;
  int density;
  int seed;
  int maxNameLength;
  int termCacheSize;

  enum {
    TRADITIONAL,
    SKIP,
    OLD
  } method;
} cfg;

SignatureCache *NewSignatureCache(int isWriterThread, int isCached)
{
  SignatureCache *C = malloc(sizeof(SignatureCache));
  C->cacheMap = NULL;

  if (isCached) {
    C->cacheList = malloc(sizeof(CachedTerm *) * cfg.termCacheSize);
    memset(C->cacheList, 0, sizeof(CachedTerm *) * cfg.termCacheSize);
  } else {
    C->cacheList = NULL;
  }

  C->cachePosition = 0;
  C->isWriterThread = isWriterThread;

  if (isWriterThread) {
    initCache();
  }
  return C;
}

void DestroySignatureCache(SignatureCache *C)
{
  if (C->cacheList) {
    for (int i = 0; i < cfg.termCacheSize; i++) {
      if (C->cacheList[i]) free(C->cacheList[i]);
    }
    free(C->cacheList);

    HASH_CLEAR(hh, C->cacheMap);
  }
  free(C);
}

Signature *NewSignature(const char *docId)
{
  size_t sigsize = sizeof(Signature) - sizeof(int);
  sigsize += cfg.length * sizeof(int);
  Signature *sig = malloc(sigsize);
  memset(sig, 0, sigsize);
  sig->id = malloc(strlen(docId) + 1);
  strcpy(sig->id, docId);
  sig->offsetEnd = -1;
  sig->offsetBegin = INT_MAX;

  return sig;
}

void SignatureFillDoubles(Signature *sig, double *array)
{
  for (int i = 0; i < cfg.length; i++) {
    sig->S[i] = array[i] > 0 ? 1 : -1;
  }
}

void SignatureDestroy(Signature *sig)
{
  free(sig->id);
  free(sig);
}

void SignaturePrint(Signature *sig)
{
  for (int i = 0; i < 128; i++) {
    printf("%d ", sig->S[i]);
  }
  printf("\n");
}

void FlattenSignature(Signature *sig, void *bsig, void *bmask)
{
  if (bsig) {
    unsigned char *out = bsig;
    // Write the supplied signature out as a flattened bitmap. Values >0 become 1, values <=0 become 0.

    for (int i = 0; i < cfg.length; i += 8) {
      unsigned char c = 0;
      for (int j = 0; j < 8; j++) {
        c |= (sig->S[i+j]>0) << (7-j);
      }
      *out++ = c;
    }
  }
  if (bmask) {
    unsigned char *out = bmask;
    // Write the supplied signature out as a flattened mask. Values !=0 become 1, values =0 become 0.

    for (int i = 0; i < cfg.length; i += 8) {
      unsigned char c = 0;
      for (int j = 0; j < 8; j++) {
        c |= (sig->S[i+j]!=0) << (7-j);
      }
      *out++ = c;
    }
  }
}

void SignatureSetValues(Signature *sig, Document *doc) {
  sig->uniqueTerms = doc->stats.uniqueTerms;
  sig->documentCharLength = doc->dataLength;
  sig->totalTerms = doc->stats.totalTerms;
  sig->quality = DocumentQuality(doc);
  // offsetBegin and offsetEnd not touched here

  sig->unused7 = 0;
  sig->unused8 = 0;
}

// Forward declarations for signature methods
static void sig_TRADITIONAL_add(int *, randctx *);
static void sig_SKIP_add(int *, randctx *);
static void sig_OLD_add(int *, randctx *);

// Signature term cache setup

void SignatureAddOffset(SignatureCache *C, Signature *sig, const char *term, int count, int totalCount, int offsetBegin, int offsetEnd, int termWeightSuffixes)
{
  SignatureAdd(C, sig, term, count, totalCount, termWeightSuffixes);
  if (sig->offsetBegin > offsetBegin) sig->offsetBegin = offsetBegin;
  if (sig->offsetEnd < offsetEnd) sig->offsetEnd = offsetEnd;
}

void SignatureAdd(SignatureCache *C, Signature *sig, const char *term, int count, int totalCount, int termWeightSuffixes)
{
  double weight = 1.0;
  char *term_tmp = NULL;
  const char *term_tmp_const = term;
  if (termWeightSuffixes) {
    const char *p = strchr(term, ':');
    if (p) {
      int termlen = p - term;
      term_tmp = malloc(termlen + 1);
      strncpy(term_tmp, term, termlen);
      term_tmp[termlen] = '\0';
      weight = atof(p+1);
      term_tmp_const = term_tmp;
    }
  }
  SignatureAddWeighted(C, sig, term_tmp_const, count, totalCount, weight);
  if (term_tmp) free(term_tmp);
}

#if !defined(TS_LOGLIKELIHOOD) && !defined(TS_TFIDF_RAW) && !defined(TS_BM25) && !defined(TS_NOTHING)
#define TS_LOGLIKELIHOOD
#endif

void SignatureAddWeighted(SignatureCache *C, Signature *sig, const char *term, int count, int totalCount, double weight_multiplier)
{
  int weight = count * 1000;


  int termStats = TermFrequencyStats(term);
  if (termStats != -1) {
    int tcf = termStats ? termStats : count;
    #ifdef TS_LOGLIKELIHOOD
    double logLikelihood = log((double) count / (double) totalCount * (double) totalTerms / (double) tcf);
    if (logLikelihood > 0) {
      weight = logLikelihood * (count-0.5) * 1000.0;
    }
    #endif
    #ifdef TS_TFIDF_RAW
    double tf = count;
    double idf = TermFrequencyDF(term);
    double df = idf;
    idf = log(num_sigs / (1 + idf));
    if (idf < 0.0) idf = 0.0;
    weight = tf * idf * 1000;

    #endif
    #ifdef TS_BM25
    double tf = count;
    double idf = TermFrequencyDF(term);
    double avg_len = 333.774137; // Wikipedia

    idf = log((num_sigs - idf + 0.5) / (idf + 0.5));

    if (idf < 0.0) idf = 0.0;

    double k1 = 2.0;
    double b = 0.75;

    double sigma = -0.725;

    double lb2 = k1 / (k1 + 2.0);
    double bm25 = (tf * (k1 + 1)) / (tf + k1 * (1.0 - b + b * (1.0 * totalCount / avg_len))) + sigma;
    if (bm25 < 0) bm25 = 0;
    weight = bm25 * idf * 1000.0;
    #endif
    #ifdef TS_NOTHING
    weight = count;

    if (weight < 0) weight = 1;
    #endif


  }
  weight *= weight_multiplier;

  int sigarray[cfg.length];
  memset(sigarray, 0, cfg.length * sizeof(int));

  int cached = 0;

  CachedTerm *ct;

  if (cfg.termCacheSize > 0) {
    HASH_FIND_STR(C->cacheMap, term, ct);
    if (ct) {
      cached = 1;
      memcpy(sigarray, ct->S, cfg.length * sizeof(int));
    }
  }

  if (!cached) {
    // Seed the random number generator with the term used
    randctx R;
    memset(R.randrsl, 0, sizeof(R.randrsl));
    strcpy((char *)(R.randrsl + 1), term);
    memWrite32(cfg.seed, (unsigned char *)(R.randrsl));
    randinit(&R, TRUE);

    switch (cfg.method) {
      case TRADITIONAL:
        sig_TRADITIONAL_add(sigarray, &R);
        break;
      case SKIP:
        sig_SKIP_add(sigarray, &R);
        break;
      case OLD:
        sig_OLD_add(sigarray, &R);
        break;
      default:
        break;
    }

    if ((cfg.termCacheSize > 0) && (C->cacheList != NULL)) {
      CachedTerm *newterm = NULL;

      if (C->cacheList[C->cachePosition] == NULL) {
        C->cacheList[C->cachePosition] = malloc(sizeof(CachedTerm) - sizeof(int) + cfg.length * sizeof(int));
        newterm = C->cacheList[C->cachePosition];
      } else {
        newterm = C->cacheList[C->cachePosition];
        HASH_DEL(C->cacheMap, newterm);
      }
      C->cachePosition = (C->cachePosition + 1) % cfg.termCacheSize;
      newterm->hits = 0;
      strcpy(newterm->term, term);
      memcpy(newterm->S, sigarray, cfg.length * sizeof(int));

      HASH_ADD_STR(C->cacheMap, term, newterm);
    }
  }



  for (int i = 0; i < cfg.length; i++) {
    sig->S[i] += sigarray[i] * weight;
  }
}


static void sig_TRADITIONAL_add(int *sig, randctx *R) {
    int pos = 0;
    int set; // number of bits to set
    int max_set = cfg.length/cfg.density/2; // half the number of bits

    // set half the bits to +1
    for (set=0;set<max_set;) {
        pos = rand(R)%cfg.length;
        if (!sig[pos]) {
            // here if not set already
            sig[pos] = 1;
            ++set;
        }
    }
    // set half the bits to -1
    for (set=0;set<max_set;) {
        pos = rand(R)%cfg.length;
        if (!sig[pos]) {
            // here if not set already
            sig[pos] = -1;
            ++set;
        }
    }
}

static void sig_SKIP_add(int *sig, randctx *R) {
    int pos = 0;
    int set;
    int max_set = cfg.length/cfg.density; // number of bits to set
    for (set=0;set<max_set;) {
        unsigned int r = rand(R);
        unsigned int skip = r % (cfg.density * 2 - 1) + 1;
        pos = (pos+skip)%cfg.length; //wrap around
        if (!sig[pos]) {
            // here if not set already
                sig[pos] += (r / (cfg.density * 2 - 1)) % 2 ? 1 : -1;
                ++set;
        }
    }
}

static void sig_OLD_add(int *sig, randctx *R) {
    for (int i = 0; i < cfg.length; i++) {
      int r = rand(R)%(cfg.density * 2);
      if (r == 0) {
        sig[i] = -1;
      } else if (r == 1) {
        sig[i] = 1;
      }
    }
}

#define SIGCACHESIZE 4096
static volatile struct {
  Signature *sigs[SIGCACHESIZE];
  FILE *fp;
  struct {
    int available;
    int complete;
    int written;
  } state;
} cache;
TSemaphore sem_cachefree;
TSemaphore sem_cacheused[SIGCACHESIZE];

static void initCache()
{
  cache.fp = fopen(GetMandatoryConfig("SIGNATURE-PATH", "Error: You need to specify a SIGNATURE-PATH to write the signature file out to."), "wb");
  InitSemaphore(&sem_cachefree, 0, SIGCACHESIZE);
  for (int i = 0; i < SIGCACHESIZE; i++) {
    InitSemaphore(&sem_cacheused[i], 0, 0);
  }

  // Write out the signature file header
  // SIGNATURE FILE FORMAT is:
  // (int = 32-bit little endian)
  // int header-size (in bytes, including this)
  // int version
  // int maxnamelen
  // int sig-width
  // int sig-density
  // int sig-seed
  // char[64] sig-method (null-terminated)

  int headerSize = 6 * 4 + 64;
  int version = 2;
  int maxnamelen = cfg.maxNameLength;
  int signatureWidth = cfg.length;
  int signatureDensity = cfg.density;
  int signatureSeed = cfg.seed;
  char signatureMethod[64] = {0};
  
  const char *method = NULL;
  switch (cfg.method) {
    case TRADITIONAL:
      method = "TRADITIONAL";
      break;
    case SKIP:
      method = "SKIP";
      break;
    case OLD:
      method = "OLD";
      break;
  }

  strncpy(signatureMethod, method, 64);

  fileWrite32(headerSize, cache.fp);
  fileWrite32(version, cache.fp);
  fileWrite32(maxnamelen, cache.fp);
  fileWrite32(signatureWidth, cache.fp);
  fileWrite32(signatureDensity, cache.fp);
  fileWrite32(signatureSeed, cache.fp);
  fwrite(signatureMethod, 1, 64, cache.fp);
}

static void dumpsignature(Signature *sig)
{
  // Write out the signature header

  // Each signature has a header consisting of the document id (as a null-terminated string of maximum length defined in config)
  // and 8 signed 32-bit little-endian integer values (to allow room for expansion)

  char sigHeader[cfg.maxNameLength+1];
  memset(sigHeader, 0, cfg.maxNameLength+1);

  strcpy(sigHeader, sig->id);
  sigHeader[cfg.maxNameLength] = '\0'; // clip

  fwrite(sigHeader, 1, sizeof(sigHeader), cache.fp);

  fileWrite32(sig->uniqueTerms, cache.fp);
  fileWrite32(sig->documentCharLength, cache.fp);
  fileWrite32(sig->totalTerms, cache.fp);
  fileWrite32(sig->quality, cache.fp);
  fileWrite32(sig->offsetBegin, cache.fp);
  fileWrite32(sig->offsetEnd, cache.fp);
  fileWrite32(sig->unused7, cache.fp);
  fileWrite32(sig->unused8, cache.fp);

  unsigned char bsig[cfg.length / 8];
  FlattenSignature(sig, bsig, NULL);
  fwrite(bsig, 1, cfg.length / 8, cache.fp);
}

void SignatureWrite(SignatureCache *C, Signature *sig)
{
  // Write the signature to a file. This takes some care as
  // this needs to be a thread-safe function. SignatureFlush
  // will be called at the end to write out any remaining
  // signatures

  WaitSemaphore(&sem_cachefree);

  int available = atomicFetchAndAdd(&cache.state.available, 1);

  cache.sigs[available % SIGCACHESIZE] = sig;
  atomicFetchAndAdd(&cache.state.complete, 1);
  PostSemaphore(&sem_cacheused[available % SIGCACHESIZE]);
  if (C->isWriterThread) {
    SignatureFlush();
  }
}

void SignatureFlush()
{
  while (TryWaitSemaphore(&sem_cacheused[cache.state.written%SIGCACHESIZE]) == 0) {
    dumpsignature(cache.sigs[cache.state.written%SIGCACHESIZE]);
    SignatureDestroy(cache.sigs[cache.state.written%SIGCACHESIZE]);
    atomicFetchAndAdd(&cache.state.written, 1);
    PostSemaphore(&sem_cachefree);
  };
}

void InitSignatureConfig()
{
  // 1024-bit signatures by default- good compromise between size and quality
  cfg.length = GetIntegerConfig("SIGNATURE-WIDTH", 1024);
  
  // 1/21 density used as a sensible default
  cfg.density = GetIntegerConfig("SIGNATURE-DENSITY", 21);

  cfg.seed = GetIntegerConfig("SIGNATURE-SEED", 0);

  // 255 is perhaps overly long, but it is still a reasonable default to use for handling
  // a variety of documents
  cfg.maxNameLength = GetIntegerConfig("MAX-DOCNAME-LENGTH", 255);
  
  if (cfg.maxNameLength % 8 != 7) {
    fprintf(stderr, "Warning: MAX-DOCNAME-LENGTH should be one less than a multiple of 8 to maintain alignment for 64-bit reads.\n");
  }

  // A default term cache size of 10,000 consumes little memory but greatly improves indexing speed
  cfg.termCacheSize = GetIntegerConfig("TERM-CACHE-SIZE", 10000);

  const char *C = GetOptionalConfig("SIGNATURE-METHOD", "TRADITIONAL");
  if (strcmp_lc(C, "TRADITIONAL")==0) cfg.method = TRADITIONAL;
  if (strcmp_lc(C, "SKIP")==0) cfg.method = SKIP;
  if (strcmp_lc(C, "OLD")==0) cfg.method = OLD;
}
