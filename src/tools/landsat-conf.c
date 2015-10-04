#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
  if (argc < 5) {
    fprintf(stderr, "usage: {input trec} {input truth} {w} {h} (decay_wgt = 0.05) (penalty = 200)\n");
    return 0;
  }
  int w = atoi(argv[3]);
  int h = atoi(argv[4]);
  double decay_wgt = 0.05;
  int penalty = 200;
  if (argc >= 6) {
    decay_wgt = atof(argv[5]);
  }
  if (argc >= 7) {
    penalty = atoi(argv[6]);
  }
  FILE *fi_trec;
  if ((fi_trec = fopen(argv[1], "r"))) {
    FILE *fi_truth;
    if ((fi_truth = fopen(argv[2], "r"))) {
      int *rcs = malloc(w * h * sizeof(int));
      int *total_hd = malloc(w * h * sizeof(int));
      int *div_hd = malloc(w * h * sizeof(int));
      int *max_correct_pos = malloc(w * h * sizeof(int));
      int max_correct_code[256];
      for (int i = 0; i < 256; i++) {
        max_correct_code[i] = 0;
      }
      int max_correctly_coded = 0;
      
      double *correct_v = malloc(w * h * sizeof(double));
      double *incorrect_v = malloc(w * h * sizeof(double));

      for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
          int rc;
          fscanf(fi_truth, "%d", &rc);
          rcs[y * w + x] = rc;
          correct_v[y * w + x] = 0;
          incorrect_v[y * w + x] = 0;
          total_hd[y * w + x] = 0;
          div_hd[y * w + x] = 0;
          max_correct_pos[y * w + x] = 0;
        }
      }

      
      for (;;) {
        int topic_id, rank, score, hd;
        char q0[16], docname[256], runtitle[256];
        if (fscanf(fi_trec, "%d %s %s %d %d %s %d\n", &topic_id, q0, docname, &rank, &score, runtitle, &hd) <= 0) break;
        
        // Calculate X and Y pos from topic id
        int xpos = topic_id % w;
        int ypos = topic_id / w;
        int imgnum, x, y, t;
        char dummy;
        sscanf(docname, "%d%c%d%c%d%c%d", &imgnum, &dummy, &x, &dummy, &y, &dummy, &t);
        //double wgt = 1.0 - hd * decay_wgt;
        double wgt = 1.0;
        if (rcs[y * w + x] != 0) {
          if (rcs[y * w + x] == rcs[ypos * w + xpos]) {
            correct_v[ypos * w + xpos] += wgt;
            total_hd[ypos * w + xpos] += hd;
            div_hd[ypos * w + xpos] += 1;
          } else {
            incorrect_v[ypos * w + xpos] += wgt;
            if (penalty == -1) {
              total_hd[ypos * w + xpos] += penalty;
              div_hd[ypos * w + xpos] += 1;
            }
          }
        }
        if (correct_v[ypos * w + xpos] > max_correct_code[rcs[ypos * w + xpos]]) {
          max_correct_code[rcs[ypos * w + xpos]] = correct_v[ypos * w + xpos];
        }
        if (correct_v[ypos * w + xpos] > max_correctly_coded) {
          max_correctly_coded = correct_v[ypos * w + xpos];
        }
        
        for (int ry = -4; ry <= 4; ry++) {
          for (int rx = -4; rx <= 4; rx++) {
            int x = xpos + rx;
            int y = ypos + ry;
            if ((x >= 0) && (y >= 0) && (x < w) && (y < h)) {
              if (correct_v[ypos * w + xpos] > max_correct_pos[y * w + x]) {
                max_correct_pos[y * w + x] = correct_v[ypos * w + xpos];
              }
            }
          }
        }
      }
      
      FILE *fo = fopen("conf-map.pgm", "w");
      fprintf(fo, "P2 %d %d\n255\n", w, h);
      for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
          double conf;
          if ((correct_v[y * w + x] + incorrect_v[y * w + x]) >= 0.000001) {
            conf = correct_v[y * w + x] / (correct_v[y * w + x] + incorrect_v[y * w + x]);
          } else {
            conf = 0.0;
          }
          
          /*
          double avg_hd = total_hd[y * w + x];
          avg_hd /= div_hd[y * w + x];
          //fprintf(stderr, "%d,%d Average hd: %f\n", x, y, avg_hd);
          conf = 1.0 - decay_wgt * avg_hd;
          if (conf <= 0) conf = 0.0;

          
          conf = correct_v[y * w + x] / max_correct_code[rcs[y * w + x]];
          conf = correct_v[y * w + x] / max_correctly_coded;
          conf = correct_v[y * w + x] / max_correct_pos[y * w + x];
          */
          
          if (rcs[y * w + x] == 0) conf = 0.0;
          
          printf("%d %f ", rcs[y * w + x], conf);
          fprintf(fo, "%d ", (int)(conf * 255));
        }
        printf("\n");
      }
      
      /*
      // Write output image
      if (argc >= 7) {
        FILE *fo = fopen(argv[6], "w");
        fprintf(fo, "P2 %d %d\n255\n", w, h);
        for (int y = 0; y < h; y++) {
          for (int x = 0; x < w; x++) {
            fprintf(fo, "%d ", ecs[y * w + x]);
          }
        }
        fclose(fo);
      }
      */
      
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
