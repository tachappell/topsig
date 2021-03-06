#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#include "../topsig-global.h"
#include "../ISAAC-rand.h"
#include "../uthash.h"

#define MAXNAMELEN 255
#define KMER_MAXLEN 31

// randctx R;
// memset(R.randrsl, 0, sizeof(R.randrsl));

char bufr[65536];
int bufr_sz;
char docid[256];

// Read characters from fp up to the next instance of c, or until EOF is hit. If EOF is hit, this returns EOF, otherwise it returns c
int read_until(FILE *fp, int c)
{
  for (;;) {
    int r = fgetc(fp);
    if (r == EOF) return EOF;
    if (r == c) {
      ungetc(c, fp);
      return c;
    }
  }
}

struct Signature {
  double *sig;
  double sigdiv;
  int sigs_included;
  int begin;
  int end;
  FILE *fp;
};

char *window_size_param = "6";
int window_size;
//int sigs_per_document = 10;
//int signature_kmers = 6;
int sig_window_size = 10000000;
int sig_window_advance = 10000000;
int signature_width = 384;
char filename[MAXNAMELEN + 1];
int starred_permutations = 0;
double density = 1.0;
char excludechar = 'X';

void writeSigHeader(FILE *fo)
{
  int header_size = 6 * 4 + 64;
  int version = 2;
  int maxnamelen = MAXNAMELEN;
  int sig_width = signature_width;
  int sig_density = 0;
  int sig_seed = 0;
  char sig_method[64] = "fastasig";
    
  fileWrite32(header_size, fo);
  fileWrite32(version, fo);
  fileWrite32(maxnamelen, fo);
  fileWrite32(sig_width, fo);
  fileWrite32(sig_density, fo);
  fileWrite32(sig_seed, fo);
  fwrite(sig_method, 1, 64, fo);
}

void CleanSignature(struct Signature *S)
{
  for (int i = 0; i < signature_width; i++) {
    S->sig[i] = 0.0;
  }
  S->sigs_included = 0;
  S->begin = -1;
  S->end = -1;
  S->sigdiv = 0;
}

typedef struct {
  char txt[KMER_MAXLEN+1];
  double *sig;
  UT_hash_handle hh;
} TermSig;
TermSig *termsigs = NULL;

void ReadTermSigs(const char *fname)
{
  if (strcmp(fname, "NONE")==0) return;
  FILE *fp = fopen(fname, "r");
  for (;;) {
    char txt[KMER_MAXLEN+1];
    if (fscanf(fp, "%s", txt) < 1) break;
    TermSig *ts = malloc(sizeof(TermSig));
    strcpy(ts->txt, txt);
    ts->sig = malloc(sizeof(double) * signature_width);
    for (int i = 0; i < signature_width; i++) {
      fscanf(fp, "%lf", &ts->sig[i]);
    }
    HASH_ADD_STR(termsigs, txt, ts);
  }
  fclose(fp);
}

#define MAX_MISMATCH_LINE 5
char *mismatch[256];

void ReadMismatchFile(const char *fname)
{
  for (int i = 0; i < 256; i++) {
    mismatch[i] = NULL;
  }
  if (strcmp(fname, "NONE")==0) return;

  FILE *fp = fopen(fname, "r");
  for (;;) {
    char mismatch_symbol[2];
    char mismatch_line[MAX_MISMATCH_LINE+1];
    
    if (fscanf(fp, "%s%s", mismatch_symbol, mismatch_line) < 2) break;
    mismatch[mismatch_symbol[0]] = malloc(strlen(mismatch_line)+1);
    strcpy(mismatch[mismatch_symbol[0]], mismatch_line);
  }
  fclose(fp);
}

void AddSigFinal(struct Signature *S, const char *term, double wgt)
{
  TermSig *ts;
  HASH_FIND_STR(termsigs, term, ts);
  if (!ts) {
    //fprintf(stderr, "AddSigFinal(%s, %f)\n", term, wgt);
    static randctx R;
    memset(R.randrsl, 0, sizeof(R.randrsl));
    strcpy((char *)(R.randrsl), term);
    randinit(&R, TRUE);
    
    for (int i = 0; i < signature_width; i++) {
      unsigned int rrr = rand(&R);
      double r = rrr;
      r /= UB4MAXVAL;
      
      if (density > 0.999999) {
        S->sig[i] += r * wgt;
        
        //fprintf(stderr, "## %f %f\n", r, density);
      } else {
        if (r < density) {
          rrr = rand(&R);
          r = rrr;
          r /= UB4MAXVAL;
          //fprintf(stderr, "R: %f\n", r * wgt);
          S->sig[i] += r * wgt;
        }
      }
    }
    S->sigdiv += wgt * density;
  } else {
    for (int i = 0; i < signature_width; i++) {
      double r = ts->sig[i];
      S->sig[i] += r * wgt;
    }
    S->sigdiv += wgt;
  }
}

void AddMismatches(struct Signature *S, const char *term, double wgt, int startFrom)
{
  char tmp[KMER_MAXLEN+1];
  for (const char *p = term+startFrom; *p != '\0'; p++) {
    if (mismatch[*p]) {
      for (const char *m = mismatch[*p]; *m != '\0'; m++) {
        strcpy(tmp, term);
        *(p - term + tmp) = *m;
        //fprintf(stderr, "mismatch %s - %s\n", term, tmp);
        AddSigFinal(S, tmp, wgt);
        AddMismatches(S, tmp, wgt, p - term + 1);
      }
    }
  }
}

void AddSigPermute(struct Signature *S, const char *term, int permute, int start, int fullpermute, double overallweight)
{
  if (permute == 0) {
    double wgt = (double)(window_size - fullpermute) / window_size;
    
    AddMismatches(S, term, wgt * overallweight, 0);
    AddSigFinal(S, term, wgt * overallweight);
    return;
  }
  if (start >= window_size) return;
  char tmpterm[window_size + 1];
  for (int i = start; i < window_size; i++) {
    strcpy(tmpterm, term);
    tmpterm[i] = '*';
    AddSigPermute(S, tmpterm, permute - 1, i + 1, fullpermute, overallweight);
  }
}

void AddSig(struct Signature *S, const char *term, double weight)
{
  for (int i = 0; i <= starred_permutations; i++) {
    AddSigPermute(S, term, i, 0, i, weight);
  }
}

void WriteSig(struct Signature *S)
{
  if (S->sigs_included == 0) return;
  FILE *fp = S->fp;
  // Write out signature
  // ...TODO...
  
/*
  char sigheader[cfg.docnamelen+1];
  memset(sigheader, 0, cfg.docnamelen+1);
  //printf("Sizeof %d\n", sizeof(sigheader));
  
  //strncpy((char *)sigheader, sig->id, cfg.docnamelen);
  strcpy(sigheader, sig->id);
  sigheader[cfg.docnamelen] = '\0'; // clip
  
  fwrite(sigheader, 1, sizeof(sigheader), cache.fp);

  fileWrite32(sig->unique_terms, cache.fp);
  fileWrite32(sig->document_char_length, cache.fp);
  fileWrite32(sig->total_terms, cache.fp);
  fileWrite32(sig->quality, cache.fp);
  fileWrite32(sig->offset_begin, cache.fp);
  fileWrite32(sig->offset_end, cache.fp);
  fileWrite32(sig->unused_7, cache.fp);
  fileWrite32(sig->unused_8, cache.fp);
  
  unsigned char bsig[cfg.length / 8];
  FlattenSignature(sig, bsig, NULL);
  fwrite(bsig, 1, cfg.length / 8, cache.fp);
  */
  
  fwrite(filename, 1, MAXNAMELEN + 1, fp);
  fileWrite32(S->sigs_included, fp);
  fileWrite32(window_size, fp);
  fileWrite32(S->sigs_included, fp);
  fileWrite32(0, fp);
  fileWrite32(S->begin, fp);
  fileWrite32(S->end, fp);
  fileWrite32(0, fp);
  fileWrite32(0, fp);
  
  //printf("    Writing sig: %d-%d\n", S->begin, S->end);
  
  unsigned char bsig[signature_width / 8];
  memset(bsig, 0, signature_width / 8);
  for (int i = 0; i < signature_width; i++) {
    double sigval = S->sig[i] / S->sigdiv;
    if (sigval >= 0.5) {
      int byte = i / 8;
      int bit = i % 8;
      bsig[byte] = bsig[byte] | (128 >> bit);
      //printf("    Setting %d,%d\n", byte, bit);
    }
  }
  fwrite(bsig, 1, signature_width / 8, S->fp);
  CleanSignature(S);
}


typedef struct {
  char txt[KMER_MAXLEN+1];
  int total_count;
  int num_docs;
  int last_doc;
  int last_doc_used;
  UT_hash_handle hh;
} TermStat;
TermStat *termstats = NULL;
int overall_total_termcount = 0;
int total_documents = 0;
int this_doc_len = 0;
int this_doc_num = 0;

void TrackStat(const char *window, int global_current_doc)
{
  TermStat *T;
  HASH_FIND_STR(termstats, window, T);
  if (!T) {
    T = malloc(sizeof(TermStat));
    strcpy(T->txt, window);
    T->total_count = 0;
    T->num_docs = 0;
    T->last_doc = 0;
    T->last_doc_used = 0;
    HASH_ADD_STR(termstats, txt, T);
  }
  if (T->last_doc != global_current_doc) {
    T->last_doc = global_current_doc;
    T->num_docs++;
  }
  T->total_count++;
  overall_total_termcount++;
  
  //fprintf(stderr, "TS[%s] %d %d\n", window, T->total_count, T->num_docs);
}


void AddKmer(struct Signature *S, const char *window)
{
  double weight = 1.0;
  // Are we using termstats?
  if (termstats) {
    TermStat *T;
    HASH_FIND_STR(termstats, window, T);
    if (T) {
    
      /*
      // TF-IDF
      weight = 1 * log(total_documents / (1.0 + T->num_docs));
      */
      
      // BM25
      /*
      double tf = 1;
      double idf = log((total_documents - T->num_docs + 0.5) / (T->num_docs + 0.5));
      double avg_len = (double)(overall_total_termcount) / (double)(total_documents);

      if (idf < 0.0) idf = 0.0;

      double k1 = 2.0;
      double b = 0.75;

      double sigma = 0.0;

      double lb2 = k1 / (k1 + 2.0);
      double bm25 = (tf * (k1 + 1)) / (tf + k1 * (1.0 - b + b * (1.0 * total_documents / avg_len))) + sigma;
      if (bm25 < 0) bm25 = 0;
      weight = bm25 * idf;
      */
      
      // Log-Likelihood
      double logLikelihood = log(1.0 / (double) this_doc_len * (double) overall_total_termcount / (double) T->num_docs);
      if (logLikelihood > 0) {
        weight = logLikelihood;
        if (T->last_doc_used != this_doc_num) {
          weight -= 0.5;
          T->last_doc_used = this_doc_num;
        }
      }
      
    }
  }
  // Add this signature
  AddSig(S, window, weight);
  
  S->sigs_included++;
}

void CreateKmerSig(void *param, const char *window, int pos, int len)
{
  this_doc_num++;
  struct Signature *S = param;
  S->begin = pos;
  S->end = pos + len;
  
  // If there is an X in the window, invalidate it
  /*
  for (int i = 0; i < len; i++) {
    if (window[i] == 'X') {
      //WriteSig(S);
      return;
    }
  }
  */
  
  char *window_size_ptr = window_size_param;
  while (*window_size_ptr) {
    window_size = atoi(window_size_ptr);
    while (*window_size_ptr != '\0' && *window_size_ptr != ',') {
      window_size_ptr++;
    }
    if (*window_size_ptr == ',') window_size_ptr++;
    
    char subwindow[window_size + 1];
    subwindow[window_size] = '\0';
    
    this_doc_len = len - window_size;
    
    for (int i = 0; i < len + 1 - window_size; i++) {
      int invalidWindow = 0;
      memcpy(subwindow, window+i, window_size);
      for (int j = 0; j < window_size; j++) {
        if (subwindow[j] == excludechar) {
          invalidWindow = 1;
          break;
        }
      }
      //printf(">[%s]\n", subwindow);
      if (!invalidWindow) {
        //AddKmer(S, subwindow, i + pos, window_size);
        AddKmer(S, subwindow);
      }
    }
  }
  WriteSig(S);
}

void TrackStats(void *param, const char *window, int pos, int len)
{
  total_documents++;
  char *window_size_ptr = window_size_param;
  while (*window_size_ptr) {
    window_size = atoi(window_size_ptr);
    while (*window_size_ptr != '\0' && *window_size_ptr != ',') {
      window_size_ptr++;
    }
    if (*window_size_ptr == ',') window_size_ptr++;
    
    char subwindow[window_size + 1];
    subwindow[window_size] = '\0';
    
    for (int i = 0; i < len + 1 - window_size; i++) {
      int invalidWindow = 0;
      memcpy(subwindow, window+i, window_size);
      for (int j = 0; j < window_size; j++) {
        if (subwindow[j] == excludechar) {
          invalidWindow = 1;
          break;
        }
      }
      //printf(">[%s]\n", subwindow);
      if (!invalidWindow) {
        TrackStat(subwindow, total_documents);
      }
    }
  }
}


void ProcessFile(FILE *fi, void (*func)(void *, const char *, int, int), void *S, int process_no, int total_processes, int window_size, int window_advance)
{
  int lineno = 0;
  while (read_until(fi, '>') != EOF) {
    
    if ((lineno++ + process_no) % total_processes != 0) {
      fgetc(fi);
      continue;
    }
    
    fscanf(fi, "%[^\n]\n", bufr);
    strcpy(docid, bufr+1);
    
    // Ends at first space / comma
    char *ptr = strchr(docid, ' ');
    if (ptr) *ptr = '\0';
    ptr = strchr(docid, ',');
    if (ptr) *ptr = '\0';
    
    bufr_sz = 0;
    int c;
    for (;;) {
      c = fgetc(fi);
      if (c == EOF || c == '>') break;
      if (isgraph(c)) {
        bufr[bufr_sz++] = c;
      }
    }
    bufr[bufr_sz] = '\0';
    ungetc(c, fi);
    
    int this_window_size = window_size;
    if (this_window_size > bufr_sz) {
      this_window_size = bufr_sz;
    }
    
    //printf("== %s ==\n", docid);
    strcpy(filename, docid);
    char window[this_window_size+1];
    window[this_window_size] = '\0';
    //for (int i = 0; i < bufr_sz + 1 - sig_window_size; i++) {
    int i = 0;
    
    while (i <= bufr_sz - this_window_size) {
      memcpy(window, bufr + i, this_window_size);
      //printf("[%s]\n", window);
      
      //AddKmer(&S, window, i, this_window_size);
      func(S, window, i, this_window_size);
      
      int endpos = bufr_sz - this_window_size;
      if (i == endpos) {
        break;
      } else {
        i += window_advance;
        if (i > endpos) {
          i = endpos;
        }
      }
    }
  }
}

int main(int argc, char **argv)
{
  if (argc < 3) {
    fprintf(stderr, "usage: (@#) {input fasta} {output sigfile} (kmer size = 6) (sig width = 384) (*perm = 0) (density = 1.0) (excludechar = 'X') (signatures file or NONE) (sig window size) (sig window advance) (mismatch file)\n");
    return 0;
  }
  int apos = 0;
  
  int process_no = 0;
  int total_processes = 1;
  
  if (argv[apos+1][0] == '@') {
    process_no = atoi(argv[apos+1]+1);
    total_processes = 16;
    apos++;
    fprintf(stderr, "Process %d of %d\n", process_no, total_processes);
  }
  FILE *fi;
  if ((fi = fopen(argv[apos+1], "r"))) {
    FILE *fo;
    if ((fo = fopen(argv[apos+2], "wb"))) {
      if (argc >= apos+4) window_size_param = argv[apos+3];
      if (argc >= apos+5) signature_width = atoi(argv[apos+4]);
      if (argc >= apos+6) starred_permutations = atoi(argv[apos+5]);
      if (argc >= apos+7) density = atof(argv[apos+6]);
      if (argc >= apos+8) excludechar = argv[apos+7][0];
      if (argc >= apos+9) ReadTermSigs(argv[apos+8]);
      if (argc >= apos+10) sig_window_size = atoi(argv[apos+9]);
      sig_window_advance = sig_window_size;
      if (argc >= apos+11) sig_window_advance = atoi(argv[apos+10]);
      if (argc >= apos+12) ReadMismatchFile(argv[apos+11]);

      struct Signature S;
      S.sig = malloc(sizeof(double) * signature_width);
      S.fp = fo;
      CleanSignature(&S);
      writeSigHeader(fo);
      
      ProcessFile(fi, TrackStats, NULL, 0, 1, 1000000000, 1000000000);
      rewind(fi);
      ProcessFile(fi, CreateKmerSig, &S, process_no, total_processes, sig_window_size, sig_window_advance);
      WriteSig(&S);
      //rewind(fi);
      //ProcessFile(fi, CreateKmerSig);
      
      

      
      /*
      unsigned char *fname_buffer = malloc(cfg.maxnamelen + 1);
      unsigned char *sig_buffer = malloc(cfg.sig_width / 8);
      
      int min_words = 10000000;
      int max_words = -1;
      int total_sigs = 0;
      long long total_words = 0;
      long long total_uniques = 0;
      for (;;) {
        if (fread(fname_buffer, 1, cfg.maxnamelen + 1, fi) == 0) break;
        int unique_terms = file_read32(fi);
        int char_len = file_read32(fi);
        int term_count = file_read32(fi);
        file_read32(fi);
        file_read32(fi);
        file_read32(fi);
        file_read32(fi);
        file_read32(fi);
        total_sigs++;
        total_words += term_count;
        total_uniques += unique_terms;
        if (term_count > max_words)
          max_words = term_count;
        if (term_count < min_words)
          min_words = term_count;
        fread(fname_buffer, 1, cfg.sig_width / 8, fi); 
      }
      rewind(fi);
      readSigHeader(fi);
      
      double avg_len = (double)total_words / (double)total_sigs;
      double stdev_num = 0.0;
      for (;;) {
        if (fread(fname_buffer, 1, cfg.maxnamelen + 1, fi) == 0) break;
        file_read32(fi);
        file_read32(fi);
        int term_count = file_read32(fi);
        file_read32(fi);
        file_read32(fi);
        file_read32(fi);
        file_read32(fi);
        file_read32(fi);
        double s = (double)term_count - avg_len;
        stdev_num += s * s;
        fread(fname_buffer, 1, cfg.sig_width / 8, fi); 
      }
      double stdev = sqrt(stdev_num / total_sigs);
      
      
      printf("Statistics for signature %s\n", argv[1]);
      printf("  Total words: %lld\n", total_words);
      printf("  Total uniques: %lld\n", total_uniques);
      printf("  Total signatures: %d\n", total_sigs);
      printf("  Average words per sig: %f\n", avg_len);
      printf("  Standard deviation: %f\n", stdev);
      printf("  Min: %d    Max: %d\n", min_words, max_words);
      free(fname_buffer);
      free(sig_buffer);
      */
      
      fclose(fo);
    } else {
      fprintf(stderr, "Unable to open output file\n");
    }
    fclose(fi);
  } else {
    fprintf(stderr, "Unable to open input file\n");
  }

  return 0;
}
