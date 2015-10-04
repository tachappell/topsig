#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct vote {
  int cat;
  int votes;
};

struct qvote {
  int cat;
  double score;
};

int main(int argc, char **argv)
{
  if (argc < 5) {
    fprintf(stderr, "usage: {input trec} {input truth} {width} {height} (radius = 0) (output pgm filename) (score_to_change = 6) (weighting_scheme = 111) (decay rate = 1) (iterations = 1) (k folds = 0) (fold # = 0) (cv dir = 'h')\n");
    return 0;
  }
  int w = atoi(argv[3]);
  int h = atoi(argv[4]);
  int radius = 0;
  if (argc >= 6) {
    radius = atoi(argv[5]);
  }
  int score_to_change = 6;
  if (argc >= 8) {
    score_to_change = atoi(argv[7]);
  }
  char weighting_scheme[128] = "111";
  if (argc >= 9) {
    sprintf(weighting_scheme, "%s", argv[8]);
  }
  double decay_rate = 1.0;
  if (argc >= 10) {
    decay_rate = atof(argv[9]);
  }
  int iterations = 1;
  if (argc >= 11) {
    iterations = atoi(argv[10]);
  }
  int k_folds = 0;
  if (argc >= 12) {
    k_folds = atoi(argv[11]);
  }
  int fold_n = 0;
  if (argc >= 13) {
    fold_n = atoi(argv[12]);
  }
  char cv_dir[128] = "h";
  if (argc >= 14) {
    sprintf(cv_dir, "%s", argv[13]);
  }
  
  FILE *fi_trec;
  if ((fi_trec = fopen(argv[1], "r"))) {
    FILE *fi_truth;
    if ((fi_truth = fopen(argv[2], "r"))) {
      int *truth = malloc(w * h * sizeof(int));
      int *rcs = malloc(w * h * sizeof(int));
      double *cfcs = malloc(w * h * sizeof(double));
      int *ecs = malloc(w * h * sizeof(int));
      
      for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
          int rc;
          double cf;
          fscanf(fi_truth, "%d %lf", &rc, &cf);
          truth[y * w + x] = rc;
          rcs[y * w + x] = rc;
          cfcs[y * w + x] = cf;
        }
      }
      
      // k-fold cross-validation
      int x_begin = 0;
      int y_begin = 0;
      int x_end = w;
      int y_end = h;
      if (k_folds != 0) {
        
        if (strcmp(cv_dir, "h")==0) { // Horizontal
          y_begin = fold_n * h / k_folds;
          y_end = (fold_n + 1) * h / k_folds;
        }
        if (strcmp(cv_dir, "v")==0) { // Vertical
          x_begin = fold_n * w / k_folds;
          x_end = (fold_n + 1) * w / k_folds;
        }
        if (strcmp(cv_dir, "g")==0) { // Grid
          int cells_w = sqrt(k_folds);
          int cells_h = k_folds / cells_w;
          int cell_x = fold_n % cells_w;
          int cell_y = fold_n / cells_w;
          x_begin = cell_x * w / cells_w;
          x_end = (cell_x + 1) * w / cells_w;
          y_begin = cell_y * h / cells_h;
          y_end = (cell_y + 1) * h / cells_h;
        }
        for (int y = 0; y < h; y++) {
          for (int x = 0; x < w; x++) {
            if (x >= x_begin && x < x_end && y >= y_begin && y < y_end) {
              // This pixel is part of the validation slice, so eliminate the rcs and cfcs (confidence) data
              rcs[y * w + x] = 0;
              cfcs[y * w + x] = 0.0;
            } else {
              // This pixel is part of the training data, so eliminate the truth data so it will not be scored
              truth[y * w + x] = 0;
            }
          }
        }
      }
      
      int *ccs = malloc(w * h * sizeof(int));
      
      for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
          ccs[y * w + x] = -1;
        }
      }
      int last_topic_id = -1;
      int initial_hd = 0;
      
      struct qvote qv[100];
      int qv_n = 0;
      
      for (;;) {
        int topic_id, rank, score, hd;
        char q0[16], docname[256], runtitle[256];
        if (fscanf(fi_trec, "%d %s %s %d %d %s %d\n", &topic_id, q0, docname, &rank, &score, runtitle, &hd) <= 0) break;
        // If this is a new topic, reset the voting and initial hd
        if (topic_id != last_topic_id) {
          //printf("# NEW TOPIC #\n");
          qv_n = 0;
          initial_hd = -1;
        }
        
        // Calculate X and Y pos from topic id
        int xpos = topic_id % w;
        int ypos = topic_id / w;
        int imgnum, x, y, t;
        char dummy;
        sscanf(docname, "%d%c%d%c%d%c%d", &imgnum, &dummy, &x, &dummy, &y, &dummy, &t);
        if (x != xpos && y != ypos) {
          if (rcs[y * w + x] != 0) {
            //printf("%d,%d : %d,%d cfcs %f\n", xpos, ypos, x, y, cfcs[y * w + x]);
            if (initial_hd == -1) initial_hd = hd;
            int qv_i = qv_n;
            int this_cat = rcs[y * w + x];
            for (int i = 0; i < qv_n; i++) {
              if (qv[i].cat == this_cat) {
                qv_i = i;
                break;
              }
            }
            if (qv_i == qv_n) {
              qv[qv_i].score = 0.0;
              qv[qv_i].cat = this_cat;
              qv_n++;
            }
            //printf("Decay_rate: %f\n", decay_rate);
            //printf("hd: %d\n", hd);
            //printf("initial hd: %d\n", initial_hd);
            
            double this_score = 1.0 - (decay_rate * (hd - initial_hd));
            //this_score *= cfcs[y * w + x];
            //printf("this_score: %f\n", this_score);
            this_score *= cfcs[y * w + x];
            if (this_score > 0.0)
              qv[qv_i].score += this_score;
            
            int qv_highest_cat = qv[0].cat;
            double qv_highest_score = 0;
            //printf("----------\n");
            for (int i = 0; i < qv_n; i++) {
              //printf("%d - %d %f\n", i, qv[i].cat, qv[i].score);
              if (qv[i].score > qv_highest_score) {
                qv_highest_score = qv[i].score;
                qv_highest_cat = qv[i].cat;
              }
            }
            //printf("Best cat: %d\n", qv_highest_cat);
            
            ecs[ypos * w + xpos] = qv_highest_cat;
            /*
            if (ccs[ypos * w + xpos] == -1) {
              ccs[ypos * w + xpos] = rcs[y * w + x];
              ecs[ypos * w + xpos] = rcs[y * w + x];
            }
            */
          }
        }
        last_topic_id = topic_id;
      }
      
      if (radius > 0) {
        for (int iter = 0; iter < iterations; iter++) {
          for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
              ccs[y * w + x] = ecs[y * w + x];
            }
          }

          for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
              int vs = (radius * 2 + 1);
              vs *= vs;
              
              struct vote votes[vs];
              for (int v = 0; v < vs; v++) {
                votes[v].cat = -1;
                votes[v].votes = 0;
              }
              for (int ry = -radius; ry <= radius; ry++) {
                for (int rx = -radius; rx <= radius; rx++) {
                  int xpos = x + rx;
                  int ypos = y + ry;
                  if (xpos >= 0 && ypos >= 0 && xpos < w && ypos < h) {
                    for (int v = 0; v < vs; v++) {
                      int dist = abs(rx) + abs(ry);
                      int score = weighting_scheme[dist] - '0';
                      if (votes[v].cat == -1) {
                        votes[v].cat = ccs[ypos * w + xpos];
                        votes[v].votes = score;
                        break;
                      } else {
                        if (votes[v].cat == ccs[ypos * w + xpos]) {
                          votes[v].votes += score;
                          break;
                        }
                      }
                    }

                  }
                }
              }
              
              /*
              int most_voted_for = -1;
              int most_votes_for = -1;
              for (int v = 0; v < vs; v++) {
                if (votes[v].votes > most_votes_for) {
                  most_votes_for = votes[v].votes;
                  most_voted_for = votes[v].cat;
                }
              }
              */
              
              int most_voted_for = -1;
              int most_votes_for = -1;
              int cats_most_voted = 0;
              for (int v = 0; v < vs; v++) {
                if (votes[v].votes >= score_to_change) {
                  most_votes_for = votes[v].votes;
                  most_voted_for = votes[v].cat;
                  cats_most_voted++;
                }
              }
              
              if (cats_most_voted == 1) {
                //fprintf(stderr, "%d,%d: replaced %d with %d (%d score)\n", x, y, ccs[y * w + x], most_voted_for, most_votes_for);
                ecs[y * w + x] = most_voted_for;
              } else {
                ecs[y * w + x] = ccs[y * w + x];
              }
            }
          }
        }
      }
      
      // Calculate error rate
      double correctly_coded = 0.0;
      double incorrectly_coded = 0.0;
      for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
          if (truth[y * w + x] != 0) { 
            if (ecs[y * w + x] == truth[y * w + x]) {
              correctly_coded += 1.0;
            } else {
              incorrectly_coded += 1.0;
            }
              
            
          } else {
            //ecs[y * w + x] = 0;
          }
        }
      }
      
      // Write output image
      if (argc >= 7) {
        FILE *fo = fopen(argv[6], "w");
        fprintf(fo, "P2 %d %d\n255\n", x_end - x_begin, y_end - y_begin);
        for (int y = y_begin; y < y_end; y++) {
          for (int x = x_begin; x < x_end; x++) {
            fprintf(fo, "%d ", ecs[y * w + x]);
          }
        }
        fclose(fo);
      }
      
      fprintf(stderr, "Error rate: %f\n", incorrectly_coded / (correctly_coded + incorrectly_coded));
      
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
