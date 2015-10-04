#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "uthash.h"

#define MAX_PER_TOPIC 1000000
int top_k = MAX_PER_TOPIC;

typedef struct {
  char q0[8];
  char docname[128];
  int rank;
  int score;
  char remain[512];
  UT_hash_handle hh;
} result;

result *results_hash = NULL;
result results[MAX_PER_TOPIC];
int num_results = 0;

int compar(const void *A, const void *B)
{
  const result *a = A, *b = B;
  int hamming_distance_a, hamming_distance_b;
  sscanf(a->remain, "%*s %d", &hamming_distance_a);
  sscanf(b->remain, "%*s %d", &hamming_distance_b);
  if (hamming_distance_a != hamming_distance_b) {
    return hamming_distance_a - hamming_distance_b;
  } else {
    return strcmp(a->docname, b->docname);
  }
}

void flush_results(const char *topicname)
{
  if (num_results == 0) return;
  qsort(results, num_results, sizeof(result), compar);
  int k = num_results;
  if (k > top_k) k = top_k;
  for (int i = 0; i < k; i++) {
    result *R = &results[i];
    printf("%s %s %s %d %d %s\n", topicname, R->q0, R->docname, i+1, 1000000-i, R->remain);
  }
  num_results = 0;
  HASH_CLEAR(hh,results_hash);
}

int main(int argc, char **argv)
{
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-k")==0) {
       top_k = atoi(argv[i+1]);
    }
  }

  char current_topic[128];
  strcpy(current_topic, "");
  for (;;) {
    char topicname[128];
    char q0[8];
    char docname[128];
    int rank;
    int score;
    char remain[512];
    if (scanf("%s %s %s %d %d %[^\n]\n", topicname, q0, docname, &rank, &score, remain) < 6) {
      break;
    }
    if (strcmp(current_topic, topicname) != 0) {
      flush_results(current_topic);
    }
    strcpy(current_topic, topicname);
    
    result *R;
    HASH_FIND_STR(results_hash, docname, R);
    if (R) {
      // Determine whether to replace this result
      //fprintf(stderr, "Dup: %s %s %s %d %d %s vs %s %s %s %d %d %s\n", topicname, q0, docname, rank, score, remain, topicname, R->q0, R->docname, R->rank, R->score, R->remain);
      
      int replace = 0;
      
      int hamming_distance_old, hamming_distance_new;
      sscanf(remain, "%*s %d", &hamming_distance_new);
      sscanf(R->remain, "%*s %d", &hamming_distance_old);
      
      if (hamming_distance_new < hamming_distance_old) {
        replace = 1;
      }
      
      if (replace) {
        //fprintf(stderr, "Replaced!\n");
        //fprintf(stderr, "%s %s %s %d %d %s vs %s %s %s %d %d %s\n", topicname, q0, docname, rank, score, remain, topicname, R->q0, R->docname, R->rank, R->score, R->remain);
        strcpy(R->q0, q0);
        R->rank = rank;
        R->score = score;
        strcpy(R->remain, remain);
      }
    } else {
      if (num_results == MAX_PER_TOPIC) {
        fprintf(stderr, "Results limit reached. Increase MAX_PER_TOPIC\n");
        exit(1);
      }
      R = &results[num_results];
      strcpy(R->q0, q0);
      strcpy(R->docname, docname);
      R->rank = rank;
      R->score = score;
      strcpy(R->remain, remain);
      HASH_ADD_STR(results_hash, docname, R);
      num_results++;
    }
    

  }
  flush_results(current_topic);
  return 0;
}
