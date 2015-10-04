#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
  if (argc < 6) {
    fprintf(stderr, "usage: {input trec} {input truth} {width} {height} {truth code}\n");
    return 0;
  }
  int w = atoi(argv[3]);
  int h = atoi(argv[4]);
  int code = atoi(argv[5]);
  FILE *fi_trec;
  if ((fi_trec = fopen(argv[1], "r"))) {
    FILE *fi_truth;
    if ((fi_truth = fopen(argv[2], "r"))) {
      int *pixbuf = malloc(w * h * sizeof(int));
      memset(pixbuf, 0, w * h * sizeof(int));
      for (;;) {
        int topic_id, rank, score, hd;
        char q0[16], docname[256], runtitle[256];
        if (fscanf(fi_trec, "%d %s %s %d %d %s %d\n", &topic_id, q0, docname, &rank, &score, runtitle, &hd) <= 0) break;
        if (strcmp(docname, "query")!=0) {
          int imgnum, x, y, t;
          char dummy;
          sscanf(docname, "%d%c%d%c%d%c%d", &imgnum, &dummy, &x, &dummy, &y, &dummy, &t);
          pixbuf[y * w + x] = hd;
          //fprintf(stderr, "Pixel %d,%d has HD %d\n", x, y, hd);
        }
      }
      
      
      int coded_region[256], uncoded_region[256];
      for (int i = 0; i < 256; i++) {
        coded_region[i] = 0;
        uncoded_region[i] = 0;
      }
      int *rcs = malloc(w * h * sizeof(int));
      
      double average_coded = 0.0;
      double average_uncoded = 0.0;
      double total_coded = 0.0;
      double total_uncoded = 0.0;
      
      for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
          int rc;
          fscanf(fi_truth, "%d", &rc);
          rcs[y * w + x] = rc;
          //fprintf(stderr, "Pixel %d,%d is coded %d\n", x, y, rc);
          
          if (rc == code) {
            coded_region[pixbuf[y * w + x]]++;
            average_coded += pixbuf[y * w + x];
            total_coded += 1.0;
          } else {
            uncoded_region[pixbuf[y * w + x]]++;
            average_uncoded += pixbuf[y * w + x];
            total_uncoded += 1.0;
          }
        }
      }
      
      average_coded /= total_coded;
      average_uncoded /= total_uncoded;
      /*
      for (int i = 0; i < 256; i++) {
        printf("%d %d\n", coded_region[i], uncoded_region[i]);
      }
      */
      printf("Average coded: %f\n", average_coded );
      printf("Average uncoded: %f\n", average_uncoded);
      printf("Contrast: %f\n", average_uncoded - average_coded);
      
      fclose(fi_truth);
    } else {
      fprintf(stderr, "Unable to open input truth file\n");
    }
    fclose(fi_trec);
  } else {
    fprintf(stderr, "Unable to open input trec file\n");
  }

  return 0;
}
