#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "topsig-topic.h"
#include "topsig-global.h"
#include "topsig-config.h"
#include "topsig-search.h"
#include "topsig-stats.h"

static void runQuery(Search *S, const char *topic_id, int topic_num, const char *topic_txt, const char *topic_refine, FILE *fp)
{
  int num = GetIntegerConfig("K", 10);
  Results *R = NULL;
  if (strcmp_lc(Config("TOPIC-REFINE-INVERT"), "true")!=0) {
    R = SearchCollectionQuery(S, topic_txt, num);
    if (topic_refine && atoi(Config("TOPIC-REFINE-K"))>0) {
      ApplyFeedback(S, R, topic_refine, atoi(Config("TOPIC-REFINE-K")));
    }
  } else {
    R = SearchCollectionQuery(S, topic_refine, num);
    if (topic_txt && atoi(Config("TOPIC-REFINE-K"))>0) {
      ApplyFeedback(S, R, topic_txt, atoi(Config("TOPIC-REFINE-K")));
    }
  }


  OutputResults(fp, topic_id, topic_num, R);

  FreeResults(R);
}

static void readerFilelistRF(Search *S, FILE *in, FILE *out)
{
  static char topic_fname[512];
  static char topic_fquery[2048];
  static char topic_id[128];
  int topic_num = 0;
  for (;;) {
    if (fscanf(in, "%s %[^\n]\n", topic_id, topic_fname) < 2) break;
    FILE *fp = fopen(topic_fname, "rb");
    fscanf(fp, "%[^\n]\n", topic_fquery);
    fseek(fp, 0, SEEK_END);
    size_t filelen = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *topic_txt = malloc(filelen + 1);
    fread(topic_txt, 1, filelen, fp);
    fclose(fp);
    topic_txt[filelen] = '\0';
    runQuery(S, topic_id, topic_num++, topic_fquery, topic_txt, out);
    free(topic_txt);
  }
}

static void readerWSJ(Search *S, FILE *in, FILE *out)
{
  static char topic_txt[65536];
  static char topic_id[128];
  int topic_num = 0;
  for (;;) {
    if (fscanf(in, "%s %[^\n]\n", topic_id, topic_txt) < 2) break;
    runQuery(S, topic_id, topic_num++, topic_txt, NULL, out);
  }
}

static int readUTF8Char(FILE *fp)
{
  int c = fgetc(fp);

  if (c == EOF) return EOF;
  if (c >= 192) {
    int seqlen = 2;
    seqlen += c >= 224 ? 1 : 0;
    seqlen += c >= 240 ? 1 : 0;
    seqlen += c >= 248 ? 1 : 0;
    seqlen += c >= 252 ? 1 : 0;

    c = '?';
    for (int i = 1; i < seqlen; i++) {
      fgetc(fp);
    }
  }
  return c;
}

static void readerPlagDet(Search *S, FILE *in, FILE *out)
{
  int topicnum = 0;
  static char topic_txt[65536] = "";
  static char topic_txt_cur[65536];
  static char topic_id[128];
  int topic_num = 0;
  for (;;) {
    int topic_txt_cur_len = 0;
    int c = 0;
    for (;;) {
      topic_txt_cur[topic_txt_cur_len] = '\0';
      c = readUTF8Char(in);
      if (c == EOF) break;
      if (c == '.') break;
      topic_txt_cur[topic_txt_cur_len++] = c;
    }
    topic_txt_cur[topic_txt_cur_len++] = '.';
    topic_txt_cur[topic_txt_cur_len] = 0;

    if (c == EOF) break;

    strcat(topic_txt, topic_txt_cur);
    if (strlen(topic_txt) >= 5) {
      sprintf(topic_id, "%d", topicnum);
      printf("%d [%s]\n", topicnum, topic_txt);
      topicnum += strlen(topic_txt);
      runQuery(S, topic_id, topic_num++, topic_txt, NULL, out);
      topic_txt[0] = '\0';
    }
  }
  if (strlen(topic_txt) >= 5) {
    sprintf(topic_id, "%d", topicnum);
    printf("%d [%s]\n", topicnum, topic_txt);
    topicnum += strlen(topic_txt) + 1;
    runQuery(S, topic_id, topic_num++, topic_txt, NULL, out);
  }
}

void RunTopic()
{
  // Initialise term statistics (if relevant)
  Stats_Initcfg();
  
  void (*topicReader)(Search *, FILE *, FILE *) = NULL;
  const char *topicpath = GetMandatoryConfig("TOPIC-PATH", "The path to the topic file needs to be specified through -topic-path (topic file)");
  FILE *fp = fopen(topicpath, "rb");
  if (!fp) {
    fprintf(stderr, "The topic file \"%s\" could not be opened.\n", topicpath);
    exit(1);
  }
  
  const char *topicformat = GetOptionalConfig("TOPIC-FORMAT", "wsj");

  if (strcmp_lc(topicformat, "wsj")==0) topicReader = readerWSJ;
  if (strcmp_lc(topicformat, "filelist_rf")==0) topicReader = readerFilelistRF;
  if (strcmp_lc(topicformat, "plagdet")==0) topicReader = readerPlagDet;
  
  const char *topicoutput = Config("RESULTS-PATH");
  FILE *fo;
  if (topicoutput) {
    fo = fopen(topicoutput, "wb");
    if (!fo) {
      fprintf(stderr, "The results file \"%s\" could not be opened for writing.\n", topicoutput);
      exit(1);
    }
  } else {
    fo = stdout;
  }

  Search *S = InitSearch();
  topicReader(S, fp, fo);
  FreeSearch(S);
  fclose(fp);
}
