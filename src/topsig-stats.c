#include <stdio.h>
#include <stdlib.h>
#include "uthash.h"
#include "topsig-global.h"
#include "topsig-config.h"
#include "topsig-stats.h"
#include "superfasthash.h"

typedef struct {
  int t; // 32-bit hash
  unsigned int freq_docs;
  unsigned int freq_terms;
  UT_hash_handle hh;
} StatTerm;

static StatTerm *termtable = NULL;
static StatTerm *termlist = NULL;
int termlist_count = 0;
int termlist_size = 0;
int totalTerms;

static int hash(const char *term)
{
  uint32_t h = SuperFastHash(term, strlen(term));
  return (int)h;
}

int TermFrequencyStats(const char *term)
{
  if (termtable == NULL) return -1;
  StatTerm *cterm;
  int term_hash = hash(term);
  HASH_FIND_INT(termtable, &term_hash, cterm);
  if (cterm)
    return cterm->freq_terms;
  else
    return 0;
}

int TermFrequencyDF(const char *term)
{
  if (termtable == NULL) return -1;
  StatTerm *cterm;
  int term_hash = hash(term);
  HASH_FIND_INT(termtable, &term_hash, cterm);
  if (cterm)
    return cterm->freq_docs;
  else
    return 0;
}

void AddTermStat(const char *word, int count)
{
  StatTerm *cterm;
  int word_hash = hash(word);
  HASH_FIND_INT(termtable, &word_hash, cterm);
  if (!cterm) {
    if (termlist_size == 0) {
      termlist_size = atoi(Config("TERMSTATS-SIZE"));
      termlist = malloc(sizeof(StatTerm) * termlist_size);
    }
    if (termlist_count < termlist_size) {
      cterm = termlist + termlist_count;
      cterm->t = word_hash;
      cterm->freq_docs = 1;
      cterm->freq_terms = count;

      HASH_ADD_INT(termtable, t, cterm);
      termlist_count++;
    }
  } else {
    cterm->freq_terms += count;
    cterm->freq_docs += 1;
  }
}

void Stats_Initcfg()
{
  if (termlist) return;
  totalTerms = 0;
  char *termstats_path = Config("TERMSTATS-PATH");
  if (termstats_path) {
    FILE *fp = fopen(termstats_path, "rb");
    if (fp == NULL) return;
    fseek(fp, 0, SEEK_END);
    int records = ftell(fp) / (4 + 4 + 4);
    fseek(fp, 0, SEEK_SET);
    termlist = malloc(sizeof(StatTerm) * records);

    int pips_drawn = -1;
    fprintf(stderr, "\n");
    for (int i = 0; i < records; i++) {
      termlist[i].t = fileRead32(fp);
      termlist[i].freq_docs = fileRead32(fp);
      termlist[i].freq_terms = fileRead32(fp);
      totalTerms += termlist[i].freq_terms;
      StatTerm *cterm = termlist + i;

      HASH_ADD_INT(termtable, t, cterm);

      int pips = ((i + 1) * 10 + (records / 2)) / records;
      if (pips > pips_drawn) {
        pips_drawn = pips;
        fprintf(stderr, "\rReading term stats: [");
        for (int p = 0; p < 10; p++) {
          fprintf(stderr, p < pips ? "*" : " ");
        }
        fprintf(stderr, "]");
      }

    }
    fprintf(stderr, "\n");

    fclose(fp);
  }
}

void WriteStats()
{
  FILE *fp;
  if (Config("TERMSTATS-PATH-OUTPUT")) {
    fp = fopen(Config("TERMSTATS-PATH-OUTPUT"), "wb");
  } else if (Config("TERMSTATS-PATH")) {
    fp = fopen(Config("TERMSTATS-PATH"), "wb");
  } else {
    fprintf(stderr, "Error: undefined termstats output path\n");
    exit(1);
  }

  if (!fp) {
    fprintf(stderr, "Error: unable to write termstats\n");
    exit(1);
  }
  int totalTerms = 0;
  StatTerm *cterm = termlist;
  int termlist_added = 0;
  for (int i = 0; i < termlist_count; i++) {
    if (cterm->freq_docs > 1) {
      fileWrite32(cterm->t, fp);
      fileWrite32(cterm->freq_docs, fp);
      fileWrite32(cterm->freq_terms, fp);
      termlist_added++;
    }

    totalTerms += cterm->freq_terms;
    cterm++;
  }
  fclose(fp);

  fprintf(stderr, "\n%d unique terms (%d written)\n", termlist_count, termlist_added);
  fprintf(stderr, "%d total terms\n", totalTerms);
}
