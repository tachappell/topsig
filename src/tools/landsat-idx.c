#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>

#include "../topsig-global.h"

#define PROGRESS_INTERVAL 100000

double randomval()
{
  return (double)rand() / (double)RAND_MAX;
}

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

struct {
  int loaded;
  int cols;
  int vals;
  double **norm;
} normalisation_data;

void load_normalisation_data(const char *fname)
{
  fprintf(stderr, "Loading normalisation data...");
  FILE *fp = fopen(fname, "r");
  int col, val;
  double norm;
  int maxcol = 0, maxval = 0;
  while (fscanf(fp, "%d %d %lf\n", &col, &val, &norm) > 0) {
    if (col > maxcol) maxcol = col;
    if (val > maxval) maxval = val;
  }
  rewind(fp);
  normalisation_data.loaded = 1;
  normalisation_data.cols = maxcol + 1;
  normalisation_data.vals = maxval + 1;
  
  normalisation_data.norm = malloc(sizeof(double *) * normalisation_data.cols);
  for (int i = 0; i < normalisation_data.cols; i++) {
    normalisation_data.norm[i] = malloc(sizeof(double) * normalisation_data.vals);
  }
  while (fscanf(fp, "%d %d %lf\n", &col, &val, &norm) > 0) {
    normalisation_data.norm[col][val] = norm;
  }
  fclose(fp);
  fprintf(stderr, " done\n");
}

double normalise(int col, int val)
{
  int minimum_val = 0;
  int maximum_val = 10000;
  if (normalisation_data.loaded) {
    return normalisation_data.norm[col][val];
  } else {
    return ((double)val - (double)minimum_val) / (double)(maximum_val - minimum_val);
  }
}

double ***kernel_sigs; // [kernels][cols][]

int cols = 12;
int kernels = 8;
int sigwidth = 64;
double *bands_weight;
int *bands_enabled;
double spreadfactor = 1.0;

void GenTermSignatureMag(double *termsig, int col, double nval)
{
  // Generate magnitude signatures
  int bbegin = sigwidth * col / cols;
  int bend = sigwidth * (col + 1) / cols;
  for (int i = 0; i < sigwidth; i++) {
    termsig[i] = 0.0;
  }
  int bits_set = 0;
  for (int i = bbegin; i < bend; i++) {
    if (randomval() < nval) {
      bits_set++;
      termsig[i] = 1.0 * 99999999.0;
    } else {
      termsig[i] = 0.0;
    }
  }
  //fprintf(stderr, "nval %f, bits set %d\n", nval, bits_set);
}


void GenTermSignatureDefault(double *termsig, int col, double nval)
{
  /*
  int val = nval * kernels
  if (val >= kernels) val = kernels - 1;
  */
  double spread = (1.0 / (kernels-1)) * spreadfactor;
  double termdiv[sigwidth];
  for (int i = 0; i < sigwidth; i++) {
    termsig[i] = 0.0;
    termdiv[i] = 0.0;
  }
  
  //fprintf(stderr, "nval: %f (spread: %f)\n", nval, spread);
  for (int i = 0; i < kernels; i++) {
    double kernelval = (double)i / (double)(kernels - 1);
    double kdist = fabs(kernelval - nval);
    //kdist *= kdist;
    double kweight = spread - kdist;
    if (kweight < 0.0) kweight = 0.0;
    //kweight = kweight * kweight;
    //fprintf(stderr, "  k%d: %f - %f %f\n", i, kernelval, kdist, kweight);

    for (int j = 0; j < sigwidth; j++) {
      termsig[j] += kernel_sigs[i][col][j] * kweight;
      termdiv[j] += kweight;
    }
  }
  for (int j = 0; j < sigwidth; j++) {
    termsig[j] /= termdiv[j];
  }
}

void GenTermSignature(double *termsig, int col, double nval)
{
  GenTermSignatureDefault(termsig, col, nval);
  //GenTermSignatureMag(termsig, col, nval);
}

int main(int argc, char **argv)
{
  if (argc < 3) {
    fprintf(stderr, "usage: {input landsat} {output sigfile} (vector fields = 12) (normalisation file or NONE) (kernels = 8) (sigwidth = 64) (bands file) (spreadfactor = 1.0)\n");
    return 0;
  }
  if (argc >= 4) {
    cols = atoi(argv[3]);
  }
  normalisation_data.loaded = 0;
  if (argc >= 5) {
    if (strcmp(argv[4], "NONE") != 0)
      load_normalisation_data(argv[4]);
  }
  if (argc >= 6) {
    kernels = atoi(argv[5]);
  }
  
  if (argc >= 7) {
    sigwidth = atoi(argv[6]);
  }
  
  if (argc >= 9) {
    spreadfactor = atof(argv[8]);
  }
  
  bands_weight = malloc(sizeof(double) * cols);
  bands_enabled = malloc(sizeof(int) * cols);
  for (int i = 0; i < cols; i++) {
    bands_weight[i] = 1.0;
    bands_enabled[i] = 1;
  }
  if (argc >= 8) {
    for (int i = 0; i < cols; i++) {
      bands_weight[i] = 0.0;
      bands_enabled[i] = 0;
    }
    FILE *fp = fopen(argv[7], "r");
    for (;;) {
      int band_num;
      double band_wgt;
      if (fscanf(fp, "%d %lf", &band_num, &band_wgt) <= 0) break;
      bands_weight[band_num - 1] = band_wgt;
      bands_enabled[band_num - 1] = 1;
    }
    fclose(fp);
  }
  
  kernel_sigs = malloc(sizeof(double **) * kernels);
  
  for (int i = 0; i < kernels; i++) {
    kernel_sigs[i] = malloc(sizeof(double *) * cols);
    for (int j = 0; j < cols; j++) {
      kernel_sigs[i][j] = malloc(sizeof(double) * sigwidth);
      for (int k = 0; k < sigwidth; k++) {
        kernel_sigs[i][j][k] = randomval();
      }
    }
  }

  FILE *fi;
  if ((fi = fopen(argv[1], "rb"))) {
    FILE *fo;
    if ((fo = fopen(argv[2], "wb"))) {
      file_write32(6 * 4 + 64, fo);
      file_write32(2, fo);
      file_write32(63, fo);
      file_write32(sigwidth, fo);
      file_write32(0, fo);
      file_write32(0, fo);
      
      char sig_method[64] = "LANDSAT-IDX";
      fwrite(sig_method, 1, 64, fo);
        
      char docid[64];
      
      int lines_read = 0;
      
      for (;;) {
        double sig[sigwidth];
        double div[sigwidth];
        for (int i = 0; i < sigwidth; i++) {
          sig[i] = 0.0;
          div[i] = 0.0;
        }
        if (fscanf(fi, "%s", docid) <= 0) break;
        int cols_used = 0;
        for (int i = 0; i < cols; i++) {
          int val;
          fscanf(fi, "%d", &val);
          if (!bands_enabled[i]) continue;
          cols_used++;
          double termsig[sigwidth];
          
          GenTermSignature(termsig, i, normalise(i, val));
          double wgt = bands_weight[i];
          for (int j = 0; j < sigwidth; j++) {
            sig[j] += termsig[j] * wgt;
            div[j] += wgt;
          }
        }
        lines_read++;
        
        unsigned char bsig[sigwidth / 8];
        memset(bsig, 0, sigwidth / 8);
        for (int j = 0; j < sigwidth; j++) {
          double v = sig[j] / div[j];
          bsig[j / 8] |= v >= 0.5 ? 128 >> (j % 8) : 0;
        }
        fwrite(docid, 1, 64, fo);
        file_write32(cols, fo);
        file_write32(0, fo);
        file_write32(cols, fo);
        file_write32(cols, fo);
        file_write32(0, fo);
        file_write32(0, fo);
        file_write32(0, fo);
        file_write32(0, fo);
        fwrite(bsig, 1, sigwidth/8, fo);
        
        if (lines_read % PROGRESS_INTERVAL == 0) {
          fprintf(stderr, "...%d\n", lines_read);
        }
      }
      printf("Read %d lines\n", lines_read);

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
