#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include "../topsig-global.h"
#include "uthash.h"
typedef struct {
  char name[32];
  char run_name[32];
  int hd;
  int count;
  UT_hash_handle hh;
} result;

typedef struct {
  char name[32];
  result *list;
  UT_hash_handle hh;
} topic;

topic *topichash = NULL;

int hamming_compar(const void *A, const void *B) {
  const result *a = A;
  const result *b = B;
  if (a->hd != b->hd)
    return a->hd - b->hd;
  return b->count - a->count;
}

int hist[101] = {0};
int hist_div = 0;

int histcount(const char *topic_name, const char *result_name, int curr_focus)
{
  const char *us = strchr(result_name, '_');
  if (!us) return 0;
  int base_len = us - result_name;
  if (strncmp(topic_name, result_name, base_len) == 0) {
    int pct = atoi(us+1);
    if ((curr_focus != -1) && (pct != curr_focus)) return 1;
    hist[pct]++;
  }
  return 0;
}

int main(int argc, char **argv)
{
  int top_k = INT_MAX;
  
  if (argc < 3) {
    fprintf(stderr, "usage: {input result file} {output result file} (top-k) (hard) (exclude-other)\n");
    return 0;
  }
  FILE *fi;
  FILE *fo;
  int hard_cutoff = 0;
  int exclude_other = 0;
  if (argc >= 4) top_k = atoi(argv[3]);
  if (argc >= 5) {
    hard_cutoff |= strcmp(argv[4], "hard")==0;
    exclude_other |= strcmp(argv[4], "exclude-other")==0;
  }
  if (argc >= 6) {
    hard_cutoff |= strcmp(argv[5], "hard")==0;
    exclude_other |= strcmp(argv[5], "exclude-other")==0;
  }
  
  if ((fi = fopen(argv[1], "rb"))) {
    if ((fo = fopen(argv[2], "wb"))) {
      char topic_name[32];
      char q0[8];
      char result_name[32];
      int rank_ignore;
      int score_ignore;
      char run_name[32];
      int hd;
      
      //clueweb09-en0000-07-18829 Q0 clueweb09-en0003-93-25406 92 999909 Topsig-Exhaustive 353
      for (;;) {
        if (fscanf(fi, "%s %s %s %d %d %s %d\n", topic_name, q0, result_name, &rank_ignore, &score_ignore, run_name, &hd) < 7) break;
        topic *T;
        HASH_FIND_STR(topichash, topic_name, T);
        if (!T) {
          T = malloc(sizeof(topic));
          strcpy(T->name, topic_name);
          T->list = NULL;
          HASH_ADD_STR(topichash, name, T);
        }
        result *R;
        HASH_FIND_STR(T->list, result_name, R);
        if (!R) {
          R = malloc(sizeof(result));
          strcpy(R->name, result_name);
          strcpy(R->run_name, run_name);
          R->hd = INT_MAX;
          R->count = 1;
          HASH_ADD_STR(T->list, name, R);
        } else {
          R->count++;
        }
        if (hd < R->hd) {
          R->hd = hd;
        }
      }
      
      topic *current_T, *tmp_T;

      int collisions = 0;

      HASH_ITER(hh, topichash, current_T, tmp_T) {
        HASH_SORT(current_T->list, hamming_compar);
        printf("%d results for topic %s\n", HASH_COUNT(current_T->list), current_T->name);
        
        result *current_R, *tmp_R;
        int rank = 1;
        int score = 1000000;
        int last_hd = -1;
        int last_count = -1;
        HASH_ITER(hh, current_T->list, current_R, tmp_R) {
          if ((rank > top_k) && ((current_R->hd != last_hd) || (current_R->count != last_count))) break;
          if ((rank > top_k) && hard_cutoff) break;
          collisions++;
          last_hd = current_R->hd;
          last_count = current_R->count;
          fprintf(fo, "%s %s %s %d %d %s %d (%d)\n", current_T->name, "Q0", current_R->name, rank, score, current_R->run_name, current_R->hd, current_R->count);
          rank++;
          score--;
          if (!exclude_other) {
            histcount(current_T->name, current_R->name, -1);
          }
        }
        if (exclude_other) {
          for (int i = 1; i <= 100; i++) {
            rank = 1;
            score = 1000000;
            last_hd = -1;
            last_count = -1;
            HASH_ITER(hh, current_T->list, current_R, tmp_R) {
              if ((rank > top_k) && ((current_R->hd != last_hd) || (current_R->count != last_count))) break;
              if ((rank > top_k) && hard_cutoff) break;
              last_hd = current_R->hd;
              last_count = current_R->count;
              rank++;
              score--;
              
              if (histcount(current_T->name, current_R->name, i) == 1) {
                rank--;
                score++;
              }
            }
          }
        }
        hist_div++;
      }
      double running_total = 0.0;
      for (int i = 100; i >= 1; i--) {
        double score = hist[i];
        score /= (double)hist_div;
        running_total += hist[i];
        printf("%d %d\n", i, hist[i]);
      }
      double avg_collisions = collisions;
      printf("Average: %f\n", running_total / 100.0);
      printf("Avg Collisions: %f\n", avg_collisions / 100.0);

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
