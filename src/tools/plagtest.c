#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "uthash.h"

#define DOC_NUMS 100000
#define MAX_THRESH 300

typedef struct {
  char docname[32];
  int score;
  int plag;
  int current_run;
  int current_run_lastoffset;
  int longest_run;
  //int offset_begin;
  //int offset_end;
  UT_hash_handle hh;
} doc;

typedef struct {
  doc *D;
  int offset_begin;
  int offset_end;
} doct;

typedef struct {
  int topicnum;
  doct **docs;
  UT_hash_handle hh;
} topiclist;

typedef struct {
  char fname[256];
  UT_hash_handle hh;
} plagiarised;

doc doclist[DOC_NUMS];
int doclist_n = 0;

int compar(const void *a, const void *b)
{
  const doc *A = a;
  const doc *B = b;
  
  return B->score - A->score;
}

int main(int argc, char **argv)
{
  int res_threshold = 8;
  int vote_threshold = 8;
  int runlen_threshold = 0;
  int runlen_maxrank = 100;
  float score_pct_threshold = 0.02f;
  topiclist *topiclisthash = NULL;
  
  int dbg_truepositives = 0;
  int dbg_falsepositives = 0;
  int dbg_truenegatives = 0;
  int dbg_falsenegatives = 0;
  plagiarised *plagiarised_documents = NULL;
  
  if (argc < 3) {
    fprintf(stderr, "Usage: {result file} {top file} (res threshold) (vote threshold) (score threshold) (run length threshold) (run length maxrank) (assessment file- for optional scoring)\n");
  } else {
    printf("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    printf("<document\n");
    printf("    xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n");
    printf("    xsi:noNamespaceSchemaLocation=\"http://www.uni-weimar.de/medien/webis/research/corpora/pan-pc-09/document.xsd\"\n");
    printf("    reference=\"%s\">\n", argv[2]);
    FILE *fp = fopen(argv[1], "r");
    if (argc >= 4) res_threshold = atoi(argv[3]);
    if (argc >= 5) vote_threshold = atoi(argv[4]);
    if (argc >= 6) score_pct_threshold = atof(argv[5]);
    if (argc >= 7) runlen_threshold = atof(argv[6]);
    if (argc >= 8) runlen_maxrank = atof(argv[7]);
    if (argc >= 9) {
      FILE *fi = fopen(argv[8], "r");
      fseek(fi, 0, SEEK_END);
      int fis = ftell(fi);
      char *fid = malloc(fis + 1);
      fseek(fi, 0, SEEK_SET);
      fread(fid, 1, fis, fi);
      fid[fis] = '\0';
      fclose(fi);
      char *s = fid;
      for (;;) {
        const char *search = "source_reference=\"";
        s = strstr(s, search);
        if (!s) break;
        s += strlen(search);
        char *e = strchr(s, '.');
        int fname_len = e - s;
        char fname[256];
        memcpy(fname, s, fname_len);
        fname[fname_len] = '\0';
        //fprintf(stderr, "[%s]\n", fname);
        plagiarised *P;
        HASH_FIND_STR(plagiarised_documents, fname, P);
        if (!P) {
          P = malloc(sizeof(plagiarised));
          strcpy(P->fname, fname);
          HASH_ADD_STR(plagiarised_documents, fname, P);
        }
      }
    }
    
    int threshold = res_threshold > vote_threshold ? res_threshold : vote_threshold;

    doc *dochash = NULL;
    int total_scores = 0;
    int last_topicnum = -1;
    int last_topicnum_tmp = -1;
    for (;;) {
      //1 Q0 WSJ900816-0060 1 1000000 Topsig 104
      int topicnum;
      char q0[16];
      char docname[32];
      int rank;
      int score;
      char runname[32];
      int dist;
      int offset_begin;
      int offset_end;
      if (fscanf(fp, "%d %s %s %d %d %s %d %d %d\n", &topicnum, q0, docname, &rank, &score, runname, &dist, &offset_begin, &offset_end) < 9) break;
      
      topiclist *T;
      HASH_FIND_INT(topiclisthash, &topicnum, T);
      if (!T) {
        T = malloc(sizeof(topiclist));
        
        T->topicnum = topicnum;
        T->docs = malloc(sizeof(doc *) * MAX_THRESH);
        HASH_ADD_INT(topiclisthash, topicnum, T);
      }
      
      if (rank <= MAX_THRESH) {
        doc *D;
        HASH_FIND_STR(dochash, docname, D);
        if (!D) {
          if (doclist_n == DOC_NUMS) {
            fprintf(stderr, "Out of list space. Increase DOC_NUMS (currently %d)\n", DOC_NUMS);
          }
          D = &doclist[doclist_n++];
          strcpy(D->docname, docname);
          D->score = 0;
          D->current_run = 1;
          D->current_run_lastoffset = -2;
          D->longest_run = 0;
          //D->offset_begin = offset_begin;
          //D->offset_end = offset_end;
          HASH_ADD_STR(dochash, docname, D);
        }
        if (rank <= res_threshold) {
          D->score++;
          total_scores++;
        }
        
        if (rank <= runlen_maxrank) {
          if (D->current_run_lastoffset >= last_topicnum) {
            D->current_run++;
          } else {
            D->current_run = 1;
          }
          if (D->current_run > D->longest_run) {
            D->longest_run = D->current_run;
          }
          D->current_run_lastoffset = topicnum;
        }
        //T->docs[rank-1] = D;
        //fprintf(stderr,"Topic %d res %d = %s (num %d)\n", T->topicnum, rank, docname, D-doclist);
        T->docs[rank-1] = malloc(sizeof(doct));
        T->docs[rank-1]->D = D;
        T->docs[rank-1]->offset_begin = offset_begin;
        T->docs[rank-1]->offset_end = offset_end;
        
        
        
      }
      
      if (last_topicnum_tmp != topicnum) {
        last_topicnum = last_topicnum_tmp;
      }
      last_topicnum_tmp = topicnum;
    }
    //qsort(doclist, doclist_n, sizeof(doc), compar);
    for (int i = 0; i < doclist_n; i++) {
      doclist[i].plag = (score_pct_threshold * total_scores <= doclist[i].score) && (doclist[i].longest_run >= runlen_threshold) ? 1 : 0;
      int is_plag = 0;
      plagiarised *P;
      
      //fprintf(stderr, "Searching [%s] - run %d\n", doclist[i].docname, doclist[i].longest_run);
      HASH_FIND_STR(plagiarised_documents, doclist[i].docname, P);
      if (P) {
        is_plag = 1;
      }
      
      if (doclist[i].plag) {
        if (is_plag) {
          dbg_truepositives++;
        } else {
          dbg_falsepositives++;
        }
      } else {
        if (is_plag) {
          dbg_falsenegatives++;
        } else {
          dbg_truenegatives++;
        }
      }
    }
    int report_results = doclist_n;

    if (report_results > 10) report_results = 10;
    //for (int i = 0; i < report_results; i++) {
      //printf("%s %d %s\n", doclist[i].docname, doclist[i].score, doclist[i].plag ? "(*)" : "");
    //}
    fclose(fp);
    
    //fp = fopen(argv[2], "r");
    
    //for (;;) {
      int topicnum;
      char topic[1024];
      /*
      if (fscanf(fp, "%d %[^\n]\n", &topicnum, topic) < 2) break;
      
      topiclist *T;
      HASH_FIND_INT(topiclisthash, &topicnum, T);
      
      int plag = 0;

      if (T) {
        for (int i = 0; i < vote_threshold; i++) {
          if (T->docs[i]->plag) plag = 1;
        }
      }
      
      printf("%s %.60s...\n", plag ? "**" : "  ", topic);
      
      */
      
      topiclist *T, *tmp;
      
      const char *current_run = NULL;
      const char *current_run_ref = NULL;
      int current_run_tbegin = 0;
      int current_run_tend = 0;
      int current_run_sbegin = 0;
      int current_run_send = 0;
        
      HASH_ITER(hh, topiclisthash, T, tmp) {
        const char *plag = NULL;
        int plag_i = -1;
        for (int i = 0; i < vote_threshold; i++) {
          //fprintf(stderr,"Topic %d res %d = %s (num %d)\n", T->topicnum, i, T->docs[i]->D->docname, T->docs[i]->D - doclist);
          if (T->docs[i]->D->plag) {
            plag = T->docs[i]->D->docname;
            plag_i = i;
            break;
          }
          //plag_i = i;
        }
        if (plag) {
          const char *source_reference = plag;
          int this_offset = T->topicnum;
          int this_length = 0;
          if (T->hh.next) this_length = ((topiclist *)(T->hh.next))->topicnum - T->topicnum;
          int source_offset = T->docs[plag_i]->offset_begin;
          int source_length = T->docs[plag_i]->offset_end - T->docs[plag_i]->offset_begin;
          
          if (current_run == plag) {
            if (this_offset < current_run_tbegin) current_run_tbegin = this_offset;
            if (source_offset < current_run_sbegin) current_run_sbegin = source_offset;
            if (this_offset+this_length > current_run_tend) current_run_tend = this_offset+this_length;
            if (source_offset+source_length > current_run_send) current_run_send = source_offset+source_length;
          } else {
            if (current_run) {
               printf("<feature name=\"detected-plagiarism\" this_offset=\"%d\" this_length=\"%d\" source_reference=\"%s.txt\" source_offset=\"%d\" source_length=\"%d\" />\n", current_run_tbegin, current_run_tend-current_run_tbegin, current_run_ref, current_run_sbegin, current_run_send-current_run_sbegin);
               current_run = NULL;
            }
          }
          if (current_run == NULL) {
            current_run = plag;
            current_run_ref = source_reference;
            current_run_tbegin = this_offset;
            current_run_sbegin = source_offset;
            current_run_tend = this_offset+this_length;
            current_run_send = source_offset+source_length;
          }
          
          //printf("%d %s\n", T->topicnum, plag);
        }
        
      }
      if (current_run) {
         printf("<feature name=\"detected-plagiarism\" this_offset=\"%d\" this_length=\"%d\" source_reference=\"%s.txt\" source_offset=\"%d\" source_length=\"%d\" />\n", current_run_tbegin, current_run_tend-current_run_tbegin, current_run_ref, current_run_sbegin, current_run_send-current_run_sbegin);
         current_run = NULL;
      }
    //}
    printf("</document>\n");
    //fclose(fp);
  }
  
  if (plagiarised_documents) {
    fprintf(stderr, "%s %s %s t f fn = %d %d %d\n", argv[3], argv[4], argv[5], dbg_truepositives, dbg_falsepositives, dbg_falsenegatives);
    //fprintf(stderr, "dbg_falsepositives = %d\n", dbg_falsepositives);
    ////fprintf(stderr, "dbg_truenegatives = %d\n", dbg_truenegatives);
    //fprintf(stderr, "dbg_falsenegatives = %d\n", dbg_falsenegatives);
  }
  return 0;
}
