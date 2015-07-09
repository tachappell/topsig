#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "uthash.h"
#include "topsig-process.h"
#include "topsig-config.h"
#include "topsig-stem.h"
#include "topsig-stop.h"
#include "topsig-stats.h"
#include "topsig-signature.h"
#include "topsig-global.h"
//#include "topsig-thread.h"
#include "topsig-progress.h"
#include "topsig-document.h"

// All code here should be sufficiently thread safe and reentrant as it
// may be run in multiple threads. This applies to all called code too.

static struct {
  int initialised;
  int charmask[256];
  struct {
    enum {SPLIT_NONE, SPLIT_HARD, SPLIT_SENTENCE} type;
    unsigned int min;
    unsigned int max;
    int overlapping;
  } split;
  enum {FILTER_NONE, FILTER_XML} filter;
  int dinesha;
} cfg;

typedef struct {
  char term[TERM_MAX_LEN+1];
  int termlen;
  int count;
  int term_begin;
  int term_end;
  
  UT_hash_handle hh;
} docterm;

static docterm *addterm(docterm *termlist, char *term, int termlen, unsigned int *docterms, int term_offset)
{
  strtolower(term);
  Stem(term);
  
  if (IsStopword(term)) {
    return termlist;
  }
  
  docterm *dterm = NULL;
  
  HASH_FIND_STR(termlist, term, dterm);
  if (dterm) {
    dterm->count++;
    if (term_offset < dterm->term_begin) dterm->term_begin = term_offset;
    if (term_offset + termlen > dterm->term_end) dterm->term_end = term_offset + termlen;
  } else {
    docterm *newterm = malloc(sizeof(docterm));
    strcpy(newterm->term, term);
    newterm->count = 1;
    newterm->termlen = termlen;
    newterm->term_begin = term_offset;
    newterm->term_end = term_offset + termlen;
    HASH_ADD_STR(termlist, term, newterm);
    
    *docterms = *docterms + 1;
  }
    
  return termlist;
}

// Create signature from the given document term set, then free it
static docterm *createsig(SignatureCache *C, docterm *currdoc, docterm *lastdoc, Document *doc, docterm **overlap_carry)
{
  // To ensure that signatures too small are not output, this uses
  // a delayed write mechanism that only outputs the previous
  // set of documents, merging the previous and the current
  // set if conditions are met.
  
  // For thread safety, the caller has to track currdoc and lastdoc.
  // This function returns 'currdoc' normally, or NULL in the case of
  // a merge.

  docterm *curr, *tmp;
  
  if (!C || !lastdoc) return currdoc;
  
  //fprintf(stderr, "%s %p %p\n", doc->docid, currdoc, lastdoc);
  /*
  char fp_name[128];
  sprintf(fp_name, "_termstats2/%s", doc->docid);
  FILE *fp = fopen(fp_name, "w");
  HASH_ITER(hh, lastdoc, curr, tmp) {
    fprintf(fp, "%s %d\n", curr->term, curr->count);
  }
  fclose(fp);
  */
  
  int merge = 0;
  if (HASH_COUNT(lastdoc) < cfg.split.min) {
    if (HASH_COUNT(lastdoc) + HASH_COUNT(currdoc) < cfg.split.max) {
      merge = 1;
    }
  }
  
  Signature *sig = NewSignature(doc->docid);
  int unique_terms = 0;
  int total_terms = 0;
  
  // Write out the overlap part of the signature, if it exists
  
  HASH_ITER(hh, *overlap_carry, curr, tmp) {
    unique_terms += 1;
    total_terms += curr->count;
    SignatureAddOffset(C, sig, curr->term, curr->count, total_terms, curr->term_begin, curr->term_end, cfg.dinesha);
    HASH_DEL(*overlap_carry, curr);
    free(curr);
  }
  
  // Definitely output all of lastdoc- these must be part of the
  // written signature

  HASH_ITER(hh, lastdoc, curr, tmp) {
    unique_terms += 1;
    total_terms += curr->count;
  }
  HASH_ITER(hh, lastdoc, curr, tmp) {
    SignatureAddOffset(C, sig, curr->term, curr->count, total_terms, curr->term_begin, curr->term_end, cfg.dinesha);
    HASH_DEL(lastdoc, curr);
    if (cfg.split.overlapping)
      HASH_ADD_STR(*overlap_carry, term, curr);
    else
      free(curr);
  }
  if (merge) {
    HASH_ITER(hh, currdoc, curr, tmp) {
      unique_terms += 1;
      total_terms += curr->count;
    }
    HASH_ITER(hh, currdoc, curr, tmp) {
      SignatureAddOffset(C, sig, curr->term, curr->count, total_terms, curr->term_begin, curr->term_end, cfg.dinesha);
      HASH_DEL(currdoc, curr);
      if (cfg.split.overlapping)
        HASH_ADD_STR(*overlap_carry, term, curr);
      else
        free(curr);
    }
    currdoc = NULL;
  }
  
  doc->stats.unique_terms = unique_terms;
  doc->stats.total_terms = total_terms;
  SignatureSetValues(sig, doc);
  SignatureWrite(C, sig, doc->docid);
  return currdoc;
}

static void addstats(docterm *currdoc)
{
  docterm *curr, *tmp;
  HASH_ITER(hh, currdoc, curr, tmp) {
    AddTermStat(curr->term, curr->count);
    HASH_DEL(currdoc, curr);
    free(curr);
  }
}

// Process the supplied file, then free both strings when done
void ProcessFile(SignatureCache *C, Document *doc)
{
  char cterm[1024];
  int cterm_len = 0;
  
  ProgressTick(doc->docid);

  docterm *currdoc = NULL;
  docterm *lastdoc = NULL;
  docterm *overlap_carry = NULL;
  unsigned int docterms = 0;
    
  // XML filter vars
  int xml_inelement = 0;
  int xml_intag = 0;
  
  int term_pos = -1;
  for (char *p = doc->data; *p != '\0'; p++) {
    int filterok = 1;
    switch (cfg.filter) {
      case FILTER_NONE:
      default:
        break;
      case FILTER_XML:
        if (*p == '&')
          if (!xml_intag)
            xml_inelement = 1;
        if (*p == '<') {
          xml_intag = 1;
          xml_inelement = 0;
        }
        if (xml_inelement && (*p == ';')) {
          xml_inelement = 0;
          filterok = 0;
        }
        if (xml_intag && (*p == '>')) {
          xml_intag = 0;
          xml_inelement = 0;
          filterok = 0;
        }
        if (xml_intag || xml_inelement) filterok = 0;
        break;
    }
    //if (strcmp(doc->docid, "2572")==0)
      //fprintf(stderr, "%d%d[%d%d]%c", cfg.charmask[(int)*p], filterok, xml_inelement, xml_intag, *p);
    if (cfg.charmask[(int)*p] && filterok) {
      if (cterm_len < 1023) {
        if (term_pos == -1) term_pos = p - doc->data;
        cterm[cterm_len++] = *p;
      }
    } else {
      cterm[cterm_len] = '\0';
      if (cterm_len > 0) {
        if (cterm_len <= TERM_MAX_LEN) {
          currdoc=addterm(currdoc, cterm, cterm_len, &docterms, term_pos);
        }
        cterm_len = 0;
        term_pos = -1;
      }
    }
    if (C) {
      if ((*p == '.') && (cfg.split.type == SPLIT_SENTENCE) && (docterms >= cfg.split.min)) {
        // Sentence split
        currdoc = createsig(C, currdoc, lastdoc, doc, &overlap_carry);
        lastdoc = currdoc;
        docterms = 0;
        currdoc = NULL;
      }
      if ((cfg.split.type != SPLIT_NONE) && (docterms >= cfg.split.max)) {
        currdoc = createsig(C, currdoc, lastdoc, doc, &overlap_carry);
        lastdoc = currdoc;
        docterms = 0;
        currdoc = NULL;
      }
    }
  }
  if (cterm_len > 0) {
    currdoc=addterm(currdoc, cterm, cterm_len, &docterms, term_pos);
    cterm_len = 0;
  }
  if (C) {
    createsig(C, currdoc, lastdoc, doc, &overlap_carry);
    createsig(C, NULL, currdoc, doc, &overlap_carry);
  }
  docterms = 0;
  
  if (!C) {
    addstats(currdoc);
  }
  currdoc = NULL;
  lastdoc = NULL;
  FreeDocument(doc);
  //printf("ProcessFile() out\n");fflush(stdout);
}

void Process_InitCfg()
{
  int alpha = lc_strcmp(Config("CHARMASK"),"alpha")==0 ? 1 : 0;
  int alnum = lc_strcmp(Config("CHARMASK"),"alnum")==0 ? 1 : 0;
  int all = lc_strcmp(Config("CHARMASK"),"all")==0 ? 1 : 0;
    
  for (int i = 0; i < 256; i++) {
    cfg.charmask[i] = (isalpha(i) && alpha) || (isalnum(i) && alnum) || (isgraph(i) && all);
  }
  
  cfg.split.type = SPLIT_NONE;
  if (lc_strcmp(Config("SPLIT-TYPE"),"hard")==0) cfg.split.type = SPLIT_HARD;
  if (lc_strcmp(Config("SPLIT-TYPE"),"sentence")==0) cfg.split.type = SPLIT_SENTENCE;
  if (cfg.split.type != 0) {
    cfg.split.max = atoi(Config("SPLIT-MAX"));
    cfg.split.min = atoi(Config("SPLIT-MIN"));
  }
  cfg.split.overlapping = 0;
  if (lc_strcmp(Config("SPLIT-OVERLAPPING"),"true")==0) cfg.split.overlapping = 1;
  
  cfg.filter = FILTER_NONE;
  if (lc_strcmp(Config("TARGET-FORMAT-FILTER"),"xml")==0) cfg.filter = FILTER_XML;

  cfg.dinesha = 0;
  if (lc_strcmp(Config("DINESHA-TERMWEIGHTS"),"true")==0) cfg.dinesha = 1;
  
  cfg.initialised = 1;
}

