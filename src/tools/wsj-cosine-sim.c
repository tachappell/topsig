#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "uthash.h"
#include "../topsig-porterstemmer.h"

#define BUFFER_SIZE (512*1024)
#define TERM_LEN 31

typedef struct {
  char txt[TERM_LEN+1];
  float tf_a;
  float tf_b;
  UT_hash_handle hh;
} attribute;

typedef struct {
  char txt[TERM_LEN+1];
  int count;
  UT_hash_handle hh;
} term;

typedef struct {
  char *doctitle;
  term *termsearch;
  int uterms;
  int tterms;
  UT_hash_handle hh;
} doc;

doc docs[200000];
doc *docsearch = NULL;

int docs_n = 0;

int tc_total = 0;
int tc_uniq = 0;

void processterm(int docId, char *w)
{
  int newlen = stem_ts2(w, strlen(w)-1) + 1;
  w[newlen] = '\0';

  if (newlen > TERM_LEN) return;

  term *T;
  docs[docId].tterms++;
  tc_total++;
  HASH_FIND_STR(docs[docId].termsearch, w, T);
  if (T) {
    T->count++;
  } else {
    T = malloc(sizeof(term));
    strcpy(T->txt, w);
    T->count = 1;
    HASH_ADD_STR(docs[docId].termsearch, txt, T);
    docs[docId].uterms++;
    tc_uniq++;
  }
}

void processfile(char *doctitle, const char *document)
{
  int docId = docs_n;
  docs[docId].doctitle = doctitle;
  docs[docId].termsearch = NULL;
  docs[docId].uterms = 0;
  docs[docId].tterms = 0;
  docs_n++;

  if ((docId % 1000)==0) fprintf(stderr, "%d/%d\n", docId, 173252);

  doc *D = docs+docId;
  HASH_ADD_STR(docsearch, doctitle, D);

  char term[1024*1024];
  int term_n = 0;
  const char *p = document;

  for (;;) {
    if (isalpha(*p)) {
      term[term_n++] = tolower(*p);
    } else {
      if (term_n > 0) {
        term[term_n] = '\0';
        processterm(docId, term);
        term_n = 0;
      }
    }

    if (*p == '\0') break;
    p++;
  }
}

static void AR_wsj(FILE *fp)
{
  int archiveSize;
  char *doc_start;
  char *doc_end;
  char buf[BUFFER_SIZE];
  int buflen = fread(buf, 1, BUFFER_SIZE-1, fp);
  int doclen;
  buf[buflen] = '\0';

  for (;;) {
    if ((doc_start = strstr(buf, "<TEXT>")) != NULL) {
      if ((doc_end = strstr(buf, "</TEXT>")) != NULL) {
        char *doc_start_i = doc_start;
        char *doc_end_i = doc_end;
        doc_end += 7;
        doclen = doc_end-buf;
        //printf("Document found, %d bytes large\n", doclen);

        char *title_start = strstr(buf, "<DOCNO>");
        char *title_end = strstr(buf, "</DOCNO>");

        title_start += 1;
        title_end -= 1;

        title_start += 7;

        int title_len = title_end - title_start;
        char *filename = malloc(title_len + 1);
        memcpy(filename, title_start, title_len);
        filename[title_len] = '\0';

        archiveSize = (doc_end-7)-(doc_start + 7);

        char *filedat = malloc(archiveSize + 1);
        memcpy(filedat, doc_start + 7, archiveSize);
        filedat[archiveSize] = '\0';

        processfile(filename, filedat, &checkera, &checkerb);
        free(filedat);

        memmove(buf, doc_end, buflen-doclen);
        buflen -= doclen;

        buflen += fread(buf+buflen, 1, BUFFER_SIZE-1-buflen, fp);
        buf[buflen] = '\0';

        // STOP EARLY -- TESTING
        //if (docs_n >= 10000) break;
      }
    } else {
      break;
    }
  }
  fprintf(stderr, "Finished\n");
}


int main(int argc, char **argv)
{
  FILE *fp = fopen(argv[1], "rb");
  AR_wsj(fp);
  fclose(fp);
  FILE *fo = fopen(argv[2], "w");

  fprintf(stderr, "Collected %d documents with %d unique terms (%d total)\n", docs_n, tc_uniq, tc_total);
  fflush(stderr);

  int i;
  int j;
  for (i = 0; i < docs_n; i++) {
    for (j = 0; j < docs_n; j++) {
      //printf("Comparing %s with %s\n", docs[i].doctitle, docs[j].doctitle);
      attribute *attribute_set = NULL;

      term *i_current, *i_tmp;
      float max_a = 0.0;
      HASH_ITER(hh, docs[i].termsearch, i_current, i_tmp) {
        attribute *A;
        HASH_FIND_STR(attribute_set, i_current->txt, A);
        if (!A) {
          A = malloc(sizeof(attribute));
          A->tf_a = 0.0f;
          A->tf_b = 0.0f;
          strcpy(A->txt, i_current->txt);
          HASH_ADD_STR(attribute_set, txt, A);
          //printf("add A %s\n", A->txt);
        }
        A->tf_a = (float)i_current->count;
        if (A->tf_a > max_a) max_a = A->tf_a;
      }
      term *j_current, *j_tmp;

      float max_b = 0.0;
      HASH_ITER(hh, docs[j].termsearch, j_current, j_tmp) {
        attribute *A;
        HASH_FIND_STR(attribute_set, j_current->txt, A);
        if (!A) {
          A = malloc(sizeof(attribute));
          A->tf_a = 0.0f;
          A->tf_b = 0.0f;
          strcpy(A->txt, j_current->txt);
          HASH_ADD_STR(attribute_set, txt, A);
          //printf("add B %s\n", A->txt);
        }
        A->tf_b = (float)j_current->count;
        if (A->tf_b > max_b) max_b = A->tf_b;
      }

      attribute *a_current, *a_tmp;
      double numer = 0.0;
      double denom_a = 0.0;
      double denom_b = 0.0;
      HASH_ITER(hh, attribute_set, a_current, a_tmp) {
        //printf("%s %f %f ND %f %f\n", a_current->txt, a_current->tf_a, a_current->tf_b, numer, denom);
        numer += a_current->tf_a/max_a * a_current->tf_b/max_b;
        denom_a += a_current->tf_a/max_a * a_current->tf_a/max_a;
        denom_b += a_current->tf_b/max_b * a_current->tf_b/max_b;

        HASH_DEL(attribute_set, a_current);
        free(a_current);
      }
      double denom = sqrt(denom_a) * sqrt(denom_b);
      fprintf(fo, "%s %s %f\n", docs[i].doctitle, docs[j].doctitle, numer/denom);
    }
  }
  fclose(fo);

  return 0;
}
