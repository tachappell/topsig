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
  int *sig;
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
int signature_width;
char filename[MAXNAMELEN + 1];
char excludechar = 'X';

int **shuffle;

void CreateShuffles()
{
  // For each residue in the kmer create a random shuffle
  static randctx R;
  memset(R.randrsl, 0, sizeof(R.randrsl));
  randinit(&R, TRUE);
  
  int kmerSize = atoi(window_size_param);
  
  shuffle = malloc(sizeof(int *) * kmerSize);
  for (int i = 0; i < kmerSize; i++) {
    shuffle[i] = malloc(sizeof(int) * signature_width);
    for (int j = 0; j < signature_width; j++) {
      shuffle[i][j] = j;
    }
    for (int j = 0; j < signature_width; j++) {
      int shufTo = j + rand(&R) % (signature_width - j);
      int t = shuffle[i][j];
      shuffle[i][j] = shuffle[i][shufTo];
      shuffle[i][shufTo] = t;
    }
  }
  
}

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
    S->sig[i] = 0;
  }
  S->sigs_included = 0;
  S->begin = -1;
  S->end = -1;
}

typedef struct {
  char txt[KMER_MAXLEN+1];
  int *sig;
  UT_hash_handle hh;
} TermSig;
TermSig *termsigs = NULL;

void ReadTermSigs(const char *fname)
{
  fprintf(stderr, "Reading term sigs\n");
  FILE *fp = fopen(fname, "r");
  // First, find the signature width.
  fscanf(fp, "%*s");
  int sw = 0;
  for (;;) {
    char tmp[32];
    fscanf(fp, "%s", tmp);
    if (!isalpha(tmp[0])) sw++;
    else break;
  }
  fprintf(stderr, "Detected signature width to be %d\n", sw);
  signature_width = sw;
  rewind(fp);
  
  for (;;) {
    char txt[KMER_MAXLEN+1];
    if (fscanf(fp, "%s", txt) < 1) break;
    TermSig *ts = malloc(sizeof(TermSig));
    strcpy(ts->txt, txt);
    ts->sig = malloc(sizeof(int) * signature_width);
    for (int i = 0; i < signature_width; i++) {
      fscanf(fp, "%d", &ts->sig[i]);
    }
    HASH_ADD_STR(termsigs, txt, ts);
  }
  fclose(fp);
}

void AddSigFinal(struct Signature *S, const char *term, double wgt)
{
  
  
  char residue[2] = ".";
  
  int termLen = strlen(term);
  for (int i = 0; i < termLen; i++) {
    residue[0] = term[i];
    TermSig *ts;
    HASH_FIND_STR(termsigs, residue, ts);
    
    for (int j = 0; j < signature_width; j++) {
      S->sig[j] += ts->sig[shuffle[i][j]];
    }
  }
  
  
  
  /*
  HASH_FIND_STR(termsigs, term, ts);
  if (!ts) {
    fprintf
  } else {
    for (int i = 0; i < signature_width; i++) {
      double r = ts->sig[i];
      S->sig[i] += r * wgt;
    }
    S->sigdiv += wgt;
  }
  */
}

/*
void AddSigPermute(struct Signature *S, const char *term, int permute, int start, int fullpermute, double overallweight)
{
  if (permute == 0) {
    double wgt = (double)(window_size - fullpermute) / window_size;
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
*/

void AddSig(struct Signature *S, const char *term, double weight)
{
  AddSigFinal(S, term, weight);
}

void WriteSig(struct Signature *S)
{
  if (S->sigs_included == 0) return;
  FILE *fp = S->fp;
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
  
  //fprintf(stderr, "W (%d)", signature_width);
  unsigned char bsig[signature_width / 8];
  memset(bsig, 0, signature_width / 8);
  for (int i = 0; i < signature_width; i++) {
    //fprintf(stderr, "%s", S->sig[i] > 0 ? "1" : "0");
    if (S->sig[i] > 0) {
      int byte = i / 8;
      int bit = i % 8;
      bsig[byte] = bsig[byte] | (128 >> bit);
    }
  }
  //fprintf(stderr, "\n");
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
    fprintf(stderr, "usage: (@#) {input fasta} {output sigfile} {protein signatures file} (kmer size = 6) (excludechar = 'X') (sig window size) (sig window advance)\n");
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
      ReadTermSigs(argv[apos+3]);
      if (argc >= apos+5) window_size_param = argv[apos+4];
      if (argc >= apos+6) excludechar = argv[apos+5][0];
      if (argc >= apos+7) sig_window_size = atoi(argv[apos+6]);
      sig_window_advance = sig_window_size;
      if (argc >= apos+8) sig_window_advance = atoi(argv[apos+7]);
      
      CreateShuffles();

      struct Signature S;
      S.sig = malloc(sizeof(double) * signature_width);
      S.fp = fo;
      CleanSignature(&S);
      writeSigHeader(fo);
      
      //ProcessFile(fi, TrackStats, NULL, 0, 1, 1000000000, 1000000000);
      //rewind(fi);
      ProcessFile(fi, CreateKmerSig, &S, process_no, total_processes, sig_window_size, sig_window_advance);
      WriteSig(&S);

      
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
