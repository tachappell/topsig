#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "topsig-experimental-rf.h"
#include "topsig-global.h"
#include "topsig-config.h"
#include "topsig-search.h"

#define MAX_TOPIC_LENGTH 127
#define MAX_FEEDBACK_LENGTH 1048575

#define NUM_RESULTS 1000
char topic[MAX_TOPIC_LENGTH + 1];
char readFeedback[MAX_FEEDBACK_LENGTH + 1];
char oldFeedback[MAX_FEEDBACK_LENGTH + 1] = "";
char feedback[MAX_FEEDBACK_LENGTH + MAX_TOPIC_LENGTH * 2 + 3];

void RunExperimentalRF()
{
  fprintf(stderr, "TopSig experimental relevance feedback mode\n");
  fflush(stderr);
  Search *S = InitSearch();



  int topic_num;
  for (topic_num = 0;; topic_num++) { // Topics
    // Get the current topic
    fgets(topic, MAX_TOPIC_LENGTH + 1, stdin);
    //scanf("%[^\n]\n", topic);

    // Strip off the final \n, if necessary
    int topic_len = strlen(topic);
    if (topic[topic_len - 1] == '\n') topic[topic_len - 1] = '\0';

    if (strcmp(topic, "EOF")==0) break;
    fprintf(stderr, "Topic: %s\n", topic);
    fflush(stderr);

    oldFeedback[0] = '\0';

    Results *R = SearchCollectionQuery(S, topic, NUM_RESULTS);
    for (int n = 0; n < NUM_RESULTS; n++) {
      const char *docId = GetResult(R, 0);
      //fprintf(stderr, "result %d: [%s]\n", n+1, docid); fflush(stderr);
      printf("%s\n", docId);
      fflush(stdout);
      //fprintf(stderr, "presented. removing\n"); fflush(stderr);
      RemoveResult(R, 0);
      //fprintf(stderr, "done. awaiting feedback\n"); fflush(stderr);

      int feedback_lines;
      char nl;


      scanf("%d%c", &feedback_lines, &nl);
      //fprintf(stderr, "received %d lines of feedback\n", feedback_lines); fflush(stderr);

      int i;
      sprintf(feedback, "%s", topic);
      for (i = 0; i < feedback_lines; i++) {
        fgets(readFeedback, MAX_FEEDBACK_LENGTH + 1, stdin);
        sprintf(feedback, "%s %s", feedback, readFeedback);
        strcpy(oldFeedback, readFeedback);
      }
      if (feedback_lines > 0) {
        ApplyFeedback(S, R, feedback, 50);
      }

    }
    FreeResults(R);
    printf("EOF\n");
    fflush(stdout);
  }
  fprintf(stderr, "End of input\n");
  fflush(stderr);

  FreeSearch(S);
}
