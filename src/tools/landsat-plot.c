#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
  if (argc < 6) {
    fprintf(stderr, "usage: {input trec} {output pgm} {width} {height} {max hd, -1 for plot hd}\n");
    return 0;
  }
  int w = atoi(argv[3]);
  int h = atoi(argv[4]);
  int mhd = atoi(argv[5]);
  FILE *fi;
  if ((fi = fopen(argv[1], "r"))) {
    FILE *fo;
    if ((fo = fopen(argv[2], "w"))) {
      char *pixbuf = malloc(w * h);
      memset(pixbuf, 0, w * h);
      for (;;) {
        int topic_id, rank, score, hd;
        char q0[16], docname[256], runtitle[256];
        if (fscanf(fi, "%d %s %s %d %d %s %d\n", &topic_id, q0, docname, &rank, &score, runtitle, &hd) <= 0) break;
        printf("READ [%s]\n", docname);
        if (strcmp(docname, "query")!=0) {
          int imgnum, x, y, t;
          char dummy;
          sscanf(docname, "%d%c%d%c%d%c%d", &imgnum, &dummy, &x, &dummy, &y, &dummy, &t);
          if (mhd == -1) {
            printf("Plotting %d to %d,%d [%s]\n", hd, x, y, docname);
            pixbuf[y * w + x] = hd;
          } else {
            if (hd <= mhd) {
              pixbuf[y * w + x] = 255;
            }
          }
        }
      }
      fprintf(fo, "P2 %d %d\n%d\n", w, h, 255);
      for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
          fprintf(fo, "%d ", pixbuf[y * w + x]);
        }
      }
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
