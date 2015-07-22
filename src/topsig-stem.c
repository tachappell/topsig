// Thread-safe stemming


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "topsig-stem.h"
#include "topsig-global.h"
#include "topsig-config.h"
#include "topsig-porterstemmer.h"
#include "topsig-thread.h"

volatile enum {
  UNSET,
  NONE,
  PORTER,
  S
} currentStemmer = UNSET;

void s_stem(char *s)
{
  // S-stemmer, described by Harman in 'How Effective is Suffixing'
  int s_len = strlen(s);

  if (s_len >= 4) {
    if (strcmp(s+s_len-3, "ies")==0) {
      if (strcmp(s+s_len-4, "eies")!=0) {
        strcpy(s+s_len-3, "y");
      }
    }
  }
  if (s_len >= 3) {
    if (strcmp(s+s_len-2, "es")==0) {
      if (strcmp(s+s_len-3, "aes")!=0) {
        strcpy(s+s_len-2, "e");
      }
    }
  }
  if (s_len >= 2) {
    if (strcmp(s+s_len-1, "s")==0) {
      if ((strcmp(s+s_len-2, "us")!=0)&&(strcmp(s+s_len-2, "ss")!=0)) {
        strcpy(s+s_len-1, "");
      }
    }
  }
}

// Stem the provided string in-place. Assume input string is lowercase
char *Stem(char *str) {
  switch (currentStemmer) {
    case NONE:
      break;
    case PORTER:
      {
        int newlen = stem_ts2(str, strlen(str)-1) + 1;
        str[newlen] = '\0';
      }
      break;
    case S:
        s_stem(str);
      break;
    default:
      break;
  }

  return str;
}


void InitStemmingConfig()
{
  const char *s = GetOptionalConfig("STEMMER", "none");

  if (strcmp_lc(s, "porter")==0) {
    currentStemmer = PORTER;
    return;
  }
  if (strcmp_lc(s, "none")==0) {
    currentStemmer = NONE;
    return;
  }
  if (strcmp_lc(s, "s")==0) {
    currentStemmer = S;
    return;
  }
  fprintf(stderr, "Error: The specified stemmer \"%s\" is not implemented.\n", s);
  exit(1);
}
