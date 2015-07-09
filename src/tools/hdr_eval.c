#include <stdio.h>
#include <stdlib.h>

typedef struct {
  int topic;
  char q0[4];
  char docname[256];
  int rank;
  int score;
  char runname[256];
  int hd;
  //char eol[1024];
} TREC_Record;

int read_TREC_record(FILE *fp, TREC_Record *T)
{
  char line[1024];
  if (!fgets(line, 1024, fp)) return 0;
  int r = sscanf(line, "%d %s %s %d %d %s %d", &T->topic, T->q0, T->docname, &T->rank, &T->score, T->runname, &T->hd);
  //if (r < 6) return 0;
  return 1;
}

int main(int argc, char **argv)
{
  if (argc < 3) {
    fprintf(stderr, "Usage: %s [truth file] {one or more inputs...}\n", argv[0]);
    fprintf(stderr, "Calculates the Hamming Distance Ratio of all topics in all\n");
    fprintf(stderr, "input files. The truth file and input files should be TREC\n");
    fprintf(stderr, "top files with an additional column after the run name with\n");
    fprintf(stderr, "the Hamming distance from the query to that result.\n");
    fprintf(stderr, "The topics in all files must be in the same order.\n");
  } else {
    for (int arg = 2; arg < argc; arg++) {
      FILE *ft = fopen(argv[1], "r");
      FILE *fi = fopen(argv[arg], "r");
      int lineno = 1;
      int eof = 0;
      int prev_topic = -1;
      double hdr_total[100] = {0.0};
      int hdr_topics[100] = {0};
      
      double dvsums[100] = {0.0};
      int dvsums_n[100] = {0};
      
      int hdr_numer = 0;
      int hdr_denom = 0;
      int hdr_i = 0;
      int hdr_imax = 0;

      printf("%s", argv[arg]);
      for (;;) {
        TREC_Record truth;
        TREC_Record input;
        if (!read_TREC_record(ft, &truth) || !read_TREC_record(fi, &input)) eof = 1;
        
        if (eof || (prev_topic != -1 && prev_topic != truth.topic)) {
	  for (int i = 0; i < hdr_i; i++) {
            hdr_total[i] += dvsums[i] / dvsums_n[i];
            hdr_topics[i]++;
            dvsums[i] = 0.0;
            dvsums_n[i] = 0;
          }
          hdr_numer = 0;
          hdr_denom = 0;
	  if (hdr_i > hdr_imax) hdr_imax = hdr_i;
	  hdr_i = 0;
        }
        if (eof) break;
        if (truth.topic != input.topic) {
          fprintf(stderr, "Error: topic mismatch on line %d of files %s and %s\n", lineno, argv[1], argv[arg]);
          exit(1);
        }
        
        prev_topic = truth.topic;
        
        hdr_numer += truth.hd;
        hdr_denom += input.hd;
        if (hdr_denom > 0) {
          dvsums[hdr_i] += ((double)hdr_numer) / hdr_denom;
        } else {
          dvsums[hdr_i] += 1.0;
        }
        dvsums_n[hdr_i]++;
	hdr_i++;
        
        lineno++;
      }
      printf("(%d)", hdr_imax);
      for (int i = 0; i < hdr_imax; i++) {
        printf("\t%.3f", hdr_total[i] / hdr_topics[i]);
      }
      printf("\n");
      
      fclose(fi);
      fclose(ft);
    }
  }
  return 0;
}

