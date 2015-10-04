#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "../topsig-global.h"

#define PROGRESS_INTERVAL 100000

static struct {
  int headersize;
  int maxnamelen;
  int sig_width;
  size_t sig_record_size;
  size_t sig_offset;
  char sig_method[64];
  int version;
  int density;
  int seed;
} cfg;

void readSigHeader(FILE *fp)
{
  cfg.headersize = file_read32(fp); // header-size
  cfg.version = file_read32(fp); // version
  cfg.maxnamelen = file_read32(fp); // maxnamelen
  cfg.sig_width = file_read32(fp); // sig_width
  cfg.density = file_read32(fp); // sig_density
  if (cfg.version >= 2) {
    cfg.seed = file_read32(fp); // sig_seed
  }
  fread(cfg.sig_method, 1, 64, fp); // sig_method
  
  cfg.sig_offset = cfg.maxnamelen + 1;
  cfg.sig_offset += 8 * 4; // 8 32-bit ints
  cfg.sig_record_size = cfg.sig_offset + cfg.sig_width / 8;
}

int main(int argc, char **argv)
{
  if (argc < 3) {
    fprintf(stderr, "usage: {input landsat} {output normfile} (vector fields = 12)\n");
    return 0;
  }
  int cols = 12;
  if (argc >= 4) {
    cols = atoi(argv[3]);
  }
  FILE *fi;
  if ((fi = fopen(argv[1], "r"))) {
    FILE *fo;
    if ((fo = fopen(argv[2], "w"))) {
      char docid[64];
      int vals[cols];
      
      int minval[cols];
      int maxval[cols];
      for (int i = 0; i < cols; i++) {
        minval[i] = INT_MAX;
        maxval[i] = 0;
      }
      
      fprintf(stderr, "Finding min/max...\n");
      
      int lines_read = 0;
      for (;;) {
        if (fscanf(fi, "%s", docid) <= 0) break;
        for (int i = 0; i < cols; i++) {
          fscanf(fi, "%d", vals+i);
          if (vals[i] < minval[i])
            minval[i] = vals[i];
          if (vals[i] > maxval[i])
            maxval[i] = vals[i];
        }
        lines_read++;
        
        if (lines_read % PROGRESS_INTERVAL == 0) {
          fprintf(stderr, "...%d\n", lines_read);
        }
      }
      
      fprintf(stderr, "Read %d lines\n", lines_read);
      fprintf(stderr, "Min vals: ");
      for (int i = 0; i < cols; i++)
        fprintf(stderr, "%d, ", minval[i]);
      fprintf(stderr, "Max vals: ");
      for (int i = 0; i < cols; i++)
        fprintf(stderr, "%d, ", maxval[i]);
      
      int **hist = malloc(sizeof(int *) * cols);
      for (int i = 0; i < cols; i++) {
        int range = maxval[i] - minval[i] + 1;
        size_t range_siz = sizeof(int) * range;
        hist[i] = malloc(range_siz);
        memset(hist[i], 0, range_siz);
      }
      rewind(fi);

      fprintf(stderr, "\nGenerating histogram...\n");
      lines_read = 0;
      for (;;) {
        if (fscanf(fi, "%s", docid) <= 0) break;
        for (int i = 0; i < cols; i++) {
          fscanf(fi, "%d", vals+i);
          hist[i][vals[i] - minval[i]]++;
        }
        lines_read++;
        
        if (lines_read % PROGRESS_INTERVAL == 0) {
          fprintf(stderr, "...%d\n", lines_read);
        }
      }
      
      for (int i = 0; i < cols; i++) {
        int range = maxval[i] - minval[i] + 1;
        int pos = 0;
        for (int j = 0; j < range; j++) {
          int hmag = hist[i][j];
          hist[i][j] = pos;
          pos += hmag;
        }
        int posmax = hist[i][maxval[i] - minval[i]];
        for (int j = 0; j < range; j++) {
          int v = j + minval[i];
          double p = (double)hist[i][j] / (double)posmax;
          fprintf(fo, "%d %d %f\n", i, v, p);
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
