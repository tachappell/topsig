#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

// enable bloom filter for faster stoplist
//#define HASH_BLOOM 27
#include "uthash.h"
#include "topsig-stop.h"
#include "topsig-global.h"
#include "topsig-config.h"
#include "topsig-stem.h"
#include "topsig-atomic.h"
#include "topsig-thread.h"

// Thread-safe stopping

struct stopword {
  char t[TERM_MAX_LEN+1];
  UT_hash_handle hh;
};

struct stopword *stoplist = NULL;

int IsStopword(const char *term)
{
  struct stopword *S;
  HASH_FIND_STR(stoplist, term, S);
  if (S != NULL) return 1;
  
  return 0;
}

void Stop_InitCfg()
{
  char *stoplist_path = Config("STOPLIST");
  if (stoplist_path == NULL) {
    return;
  }
  
  FILE *fp = fopen(stoplist_path, "rb");
  if (fp == NULL) {
    fprintf(stderr, "Unable to load stoplist: %s\n", stoplist_path);
    return;
  }
  char term[TERM_MAX_LEN+1];
  
  for (;;) {
    if (fscanf(fp, "%s\n", term) < 1) break;
    strtolower(term);
    Stem(term);
    struct stopword *newStopword;
    HASH_FIND_STR(stoplist, term, newStopword);
    if (newStopword == NULL) {
      newStopword = malloc(sizeof(struct stopword));
      strcpy(newStopword->t, term);
      HASH_ADD_STR(stoplist, t, newStopword);
    }
  }
  fclose(fp);
}
