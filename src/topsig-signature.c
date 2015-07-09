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

struct cacheterm {
  UT_hash_handle hh;

  int hits;
  char term[TERM_MAX_LEN+1];
  int S[1];
};

struct SignatureCache {
  struct cacheterm *cache_map;
  struct cacheterm **cache_list;
  int cache_pos;
  int iswriter;
};

static void initcache(); //forward declaration

struct Signature {
  char *id;
  int unique_terms;
  int document_char_length;
  int total_terms;
  int quality;
  int offset_begin;
  int offset_end;
  int unused_7;
  int unused_8;
  
  int S[1];
};

static struct {
  int length;
  int density;
  int seed;
  int docnamelen;
  int termcachesize;
  int thread_mode;
  
  enum {
    TRADITIONAL,
    SKIP,
    OLD
  } method;
} cfg;

SignatureCache *NewSignatureCache(int iswriter, int iscached)
{
  SignatureCache *C = malloc(sizeof(SignatureCache));
  C->cache_map = NULL;
  
  if (iscached) {
    C->cache_list = malloc(sizeof(struct cacheterm *) * cfg.termcachesize);
    memset(C->cache_list, 0, sizeof(struct cacheterm *) * cfg.termcachesize);
  } else {
    C->cache_list = NULL;
  }
  
  C->cache_pos = 0;
  C->iswriter = iswriter;
  
  if (iswriter) {
    initcache();
  }
  return C;
}

void DestroySignatureCache(SignatureCache *C)
{
  if (C->cache_list) {
    for (int i = 0; i < cfg.termcachesize; i++) {
      if (C->cache_list[i]) free(C->cache_list[i]);
    }
    free(C->cache_list);
    
    HASH_CLEAR(hh, C->cache_map);
  }
  free(C);
}

Signature *NewSignature(const char *docid)
{
  size_t sigsize = sizeof(Signature) - sizeof(int);
  sigsize += cfg.length * sizeof(int);
  Signature *sig = malloc(sigsize);
  memset(sig, 0, sigsize);
  sig->id = malloc(strlen(docid) + 1);
  strcpy(sig->id, docid);
  sig->offset_end = -1;
  sig->offset_begin = INT_MAX;
  
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
  sig->unique_terms = doc->stats.unique_terms;
  sig->document_char_length = doc->data_length;
  sig->total_terms = doc->stats.total_terms;
  sig->quality = DocumentQuality(doc);
  // offset_begin and offset_end not touched here
  
  sig->unused_7 = 0;
  sig->unused_8 = 0;
}

// Forward declarations for signature methods
static void sig_TRADITIONAL_add(int *, randctx *);
static void sig_SKIP_add(int *, randctx *);
static void sig_OLD_add(int *, randctx *);

// Signature term cache setup

void SignatureAddOffset(SignatureCache *C, Signature *sig, const char *term, int count, int total_count, int offset_begin, int offset_end, int dinesha)
{
  SignatureAdd(C, sig, term, count, total_count, dinesha);
  if (sig->offset_begin > offset_begin) sig->offset_begin = offset_begin;
  if (sig->offset_end < offset_end) sig->offset_end = offset_end;
}

void SignatureAdd(SignatureCache *C, Signature *sig, const char *term, int count, int total_count, int dinesha)
{
  double weight = 1.0;
  char *term_tmp = NULL;
  const char *term_tmp_const = term;
  if (dinesha) {
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
  SignatureAddWeighted(C, sig, term_tmp_const, count, total_count, weight);
  if (term_tmp) free(term_tmp);
}

//#define TS_LOGLIKELIHOOD
//#define TS_TFIDF_RAW
//#define TS_BM25
//#define TS_NOTHING

#if !defined(TS_LOGLIKELIHOOD) && !defined(TS_TFIDF_RAW) && !defined(TS_BM25) && !defined(TS_NOTHING)
#define TS_LOGLIKELIHOOD
#endif

void SignatureAddWeighted(SignatureCache *C, Signature *sig, const char *term, int count, int total_count, double weight_multiplier)
{
  //fprintf(stderr, "[%s]-%f\n", term, weight_multiplier);
  int weight = count * 1000;
  
  double num_sigs = 2666198.0; // Wikipedia
  //double num_sigs = 173252.0; // WSJ
  
  int termStats = TermFrequencyStats(term);
  if (termStats != -1) {
    int tcf = termStats ? termStats : count;
    #ifdef TS_LOGLIKELIHOOD
    double logLikelihood = log((double) count / (double) total_count * (double) total_terms / (double) tcf);
//      printf("LL=%lf, count=%d, total_count=%d, total_term=%d, tcf=%d\n",logLikelihood,count,total_count,total_terms,tcf);
    if (logLikelihood > 0) {
      weight = logLikelihood * (count-0.5) * 1000.0;
    } else {
//        return;
    }
    //weight = logLikelihood * (count-0.9) * 1000.0;
    #endif
    #ifdef TS_TFIDF_RAW
    double tf = count;
    double idf = TermFrequencyDF(term);
    double df = idf;
    idf = log(num_sigs / (1 + idf));
    //idf = log((num_sigs - idf + 0.5) / (idf + 0.5));
    if (idf < 0.0) idf = 0.0;
    weight = tf * idf * 1000;
    //if (weight < 0) weight = 1;
    //fprintf(stderr, "[%s] count: %.0f\t\tdf: %.0f\t\tidf: %f\t\twt: %d\n", term, tf, df, idf, weight);

    #endif
    #ifdef TS_BM25
    double tf = count;
    double idf = TermFrequencyDF(term);
    //double avg_len = 263.720996; // wsj
    //double avg_len = 291.304799; // wsj
    double avg_len = 333.774137; // Wikipedia
    
    // BM25 IDF
    idf = log((num_sigs - idf + 0.5) / (idf + 0.5));
    // TFIDF IDF
    //idf = log(num_sigs / (1 + idf));
    
    if (idf < 0.0) idf = 0.0;
    
    double k1 = 2.0;
    double b = 0.75;
    
    double sigma = -0.725;
    //double sigma = 0;
    //double sigma = 0.5;
    
    //fprintf(stderr, "tf %f idf %f\n", tf, idf);
    
    //fprintf(stderr, "ff %s %d %f\n", term, total_count, avg_len);
    
    double lb2 = k1 / (k1 + 2.0);
    double bm25 = (tf * (k1 + 1)) / (tf + k1 * (1.0 - b + b * (1.0 * total_count / avg_len))) + sigma;
    if (bm25 < 0) bm25 = 0;
    //if (!(lb2 < bm25)) {
    //fprintf(stderr, "! %s: %f < %f\n", term, lb2, bm25);
    //}
    weight = bm25 * idf * 1000.0;
    //if (weight < 0) weight = 1;
    #endif
    #ifdef TS_NOTHING
    weight = count;
    //This change increases map in wsj from .0790 to .0811
    //weight = (1.0 * count - 0.9) * 1000.0;
    if (weight < 0) weight = 1;
    #endif
    
    
  }
  //fprintf(stderr, "weight (%s): %d -> %d\n", term, count, weight);
  weight *= weight_multiplier;
  
  //printf("SignatureAdd() in\n");fflush(stdout);
  int sigarray[cfg.length];
  memset(sigarray, 0, cfg.length * sizeof(int));
  
  int cached = 0;
  
  struct cacheterm *ct;
  
  if (cfg.termcachesize > 0) {
    HASH_FIND_STR(C->cache_map, term, ct);
    if (ct) {
      cached = 1;
      memcpy(sigarray, ct->S, cfg.length * sizeof(int));
      //_cache_hit++;
    }
  }
  
  if (!cached) {
    // Seed the random number generator with the term used
    randctx R;
    memset(R.randrsl, 0, sizeof(R.randrsl));
    strcpy((char *)(R.randrsl + 1), term);
    mem_write32(cfg.seed, (unsigned char *)(R.randrsl));
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
    
    if ((cfg.termcachesize > 0) && (C->cache_list != NULL)) {
      struct cacheterm *newterm = NULL;
      
      if (C->cache_list[C->cache_pos] == NULL) {
        C->cache_list[C->cache_pos] = malloc(sizeof(struct cacheterm) - sizeof(int) + cfg.length * sizeof(int));
        newterm = C->cache_list[C->cache_pos];
      } else {
        newterm = C->cache_list[C->cache_pos];
        HASH_DEL(C->cache_map, newterm);
      }
      C->cache_pos = (C->cache_pos + 1) % cfg.termcachesize;
      newterm->hits = 0;
      strcpy(newterm->term, term);
      memcpy(newterm->S, sigarray, cfg.length * sizeof(int));
      
      HASH_ADD_STR(C->cache_map, term, newterm);
    }
  }
  
  
  
  for (int i = 0; i < cfg.length; i++) {
    //int oi = sig->S[i];
    sig->S[i] += sigarray[i] * weight;
    //if (oi != 0 && sig->S[i] == 0)
    //  sig->S[i] = oi > 0 ? 1 : -1;
  }
  //printf("SignatureAdd() out\n");fflush(stdout);
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
    int pos = 0;
    int set; // number of bits to set
    int max_set = cfg.length/cfg.density/2; // half the number of bits
    
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

static void initcache()
{
  cache.fp = fopen(Config("SIGNATURE-PATH"), "wb");
  tsem_init(&sem_cachefree, 0, SIGCACHESIZE);
  for (int i = 0; i < SIGCACHESIZE; i++) {
    tsem_init(&sem_cacheused[i], 0, 0);
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
  
  int header_size = 6 * 4 + 64;
  int version = 2;
  int maxnamelen = cfg.docnamelen;
  int sig_width = cfg.length;
  int sig_density = cfg.density;
  int sig_seed = cfg.seed;
  char sig_method[64] = {0};
  
  strncpy(sig_method, Config("SIGNATURE-METHOD"), 64);
  
  file_write32(header_size, cache.fp);
  file_write32(version, cache.fp);
  file_write32(maxnamelen, cache.fp);
  file_write32(sig_width, cache.fp);
  file_write32(sig_density, cache.fp);
  file_write32(sig_seed, cache.fp);
  fwrite(sig_method, 1, 64, cache.fp);
}

static void dumpsignature(Signature *sig)
{
  // Write out the signature header
  
  // Each signature has a header consisting of the document id (as a null-terminated string of maximum length defined in config)
  // and 8 signed 32-bit little-endian integer values (to allow room for expansion)
  //int unique_terms;
  //int document_char_length;
  //int total_terms;
  //int quality;
  //int unused_5;
  //int unused_6;
  //int unused_7;
  //int unused_8;
  int docnamelen = cfg.docnamelen;
  //printf("Testing printf\n");
  //printf("Testing printf with val %d\n", 166);
  //printf("docnamelen: %d\n", docnamelen);
  char sigheader[cfg.docnamelen+1];
  memset(sigheader, 0, cfg.docnamelen+1);
  //printf("Sizeof %d\n", sizeof(sigheader));
  
  //strncpy((char *)sigheader, sig->id, cfg.docnamelen);
  strcpy(sigheader, sig->id);
  sigheader[cfg.docnamelen] = '\0'; // clip
  
  fwrite(sigheader, 1, sizeof(sigheader), cache.fp);

  file_write32(sig->unique_terms, cache.fp);
  file_write32(sig->document_char_length, cache.fp);
  file_write32(sig->total_terms, cache.fp);
  file_write32(sig->quality, cache.fp);
  file_write32(sig->offset_begin, cache.fp);
  file_write32(sig->offset_end, cache.fp);
  file_write32(sig->unused_7, cache.fp);
  file_write32(sig->unused_8, cache.fp);
  
  unsigned char bsig[cfg.length / 8];
  FlattenSignature(sig, bsig, NULL);
  fwrite(bsig, 1, cfg.length / 8, cache.fp);
}

void SignatureWrite(SignatureCache *C, Signature *sig, const char *docid)
{
  // Write the signature to a file. This takes some care as
  // this needs to be a thread-safe function. SignatureFlush
  // will be called at the end to write out any remaining
  // signatures
  
  tsem_wait(&sem_cachefree);

  int available = atomic_add(&cache.state.available, 1);

  cache.sigs[available % SIGCACHESIZE] = sig;
  atomic_add(&cache.state.complete, 1);
  tsem_post(&sem_cacheused[available % SIGCACHESIZE]);
  if (C->iswriter) {
    SignatureFlush();
  }
}

void SignatureFlush()
{
  while (tsem_trywait(&sem_cacheused[cache.state.written%SIGCACHESIZE]) == 0) {
    dumpsignature(cache.sigs[cache.state.written%SIGCACHESIZE]);
    SignatureDestroy(cache.sigs[cache.state.written%SIGCACHESIZE]);
    atomic_add(&cache.state.written, 1);
    tsem_post(&sem_cachefree);
  };
}

void Signature_InitCfg()
{
  char *C = Config("SIGNATURE-WIDTH");
  if (C == NULL) {
    fprintf(stderr, "SIGNATURE-WIDTH unspecified\n");
    exit(1);
  }
  cfg.length = atoi(C);
  
  C = Config("SIGNATURE-DENSITY");
  if (C == NULL) {
    fprintf(stderr, "SIGNATURE-DENSITY unspecified\n");
    exit(1);
  }
  cfg.density = atoi(C);
  
  cfg.seed = 0;
  C = Config("SIGNATURE-SEED");
  if (C) {
    cfg.seed = atoi(C);
  }
  
  C = Config("MAX-DOCNAME-LENGTH");
  if (C == NULL) {
    fprintf(stderr, "MAX-DOCNAME-LENGTH unspecified\n");
    exit(1);
  }
  cfg.docnamelen = atoi(C);
  
  C = Config("TERM-CACHE-SIZE");
  if (C) {
    cfg.termcachesize = atoi(C);
  } else {
    cfg.termcachesize = 0;
  }
  
  C = Config("INDEX-THREADING");
  if (C && strcmp(C, "multi") == 0) {
    cfg.thread_mode = 1;
  } else {
    cfg.thread_mode = 0;
  }
  
  C = Config("SIGNATURE-METHOD");
  if (C == NULL) {
    fprintf(stderr, "SIGNATURE-METHOD unspecified\n");
    exit(1);
  }
  if (lc_strcmp(C, "TRADITIONAL")==0) cfg.method = TRADITIONAL;
  if (lc_strcmp(C, "SKIP")==0) cfg.method = SKIP;
  if (lc_strcmp(C, "OLD")==0) cfg.method = OLD;
}
