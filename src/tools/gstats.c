#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "../topsig-global.h"

static struct {
  int headersize;
  int maxnamelen;
  int sig_width;
  size_t sig_record_size;
  size_t sig_offset;
} cfg;

void readSigHeader(FILE *fi)
{
  char sig_method[64];
  cfg.headersize = fileRead32(fi); // header-size
  int version = fileRead32(fi); // version
  cfg.maxnamelen = fileRead32(fi); // maxnamelen
  cfg.sig_width = fileRead32(fi); // sig_width
  fileRead32(fi); // sig_density
  if (version >= 2) {
    fileRead32(fi); // sig_seed
  }
  fread(sig_method, 1, 64, fi); // sig_method

  cfg.sig_offset = cfg.maxnamelen + 1;
  cfg.sig_offset += 8 * 4; // 8 32-bit ints
  cfg.sig_record_size = cfg.sig_offset + cfg.sig_width / 8;
}

int main(int argc, char **argv)
{
  if (argc < 2) {
    fprintf(stderr, "usage: {input sigfile}\n");
    return 0;
  }
  FILE *fi;
  if ((fi = fopen(argv[1], "rb"))) {
    readSigHeader(fi);

    unsigned char *fname_buffer = malloc(cfg.maxnamelen + 1);
    unsigned char *sig_buffer = malloc(cfg.sig_width / 8);

    int min_words = 10000000;
    int max_words = -1;
    int total_sigs = 0;
    long long total_words = 0;
    long long total_uniques = 0;
    for (;;) {
      if (fread(fname_buffer, 1, cfg.maxnamelen + 1, fi) == 0) break;
      int uniqueTerms = fileRead32(fi);
      int char_len = fileRead32(fi);
      int term_count = fileRead32(fi);
      fileRead32(fi);
      fileRead32(fi);
      fileRead32(fi);
      fileRead32(fi);
      fileRead32(fi);
      total_sigs++;
      total_words += term_count;
      total_uniques += uniqueTerms;
      if (term_count > max_words)
        max_words = term_count;
      if (term_count < min_words)
        min_words = term_count;
      fread(fname_buffer, 1, cfg.sig_width / 8, fi);
    }
    rewind(fi);
    readSigHeader(fi);

    double avg_len = (double)total_words / (double)total_sigs;
    double stdev_num = 0.0;
    for (;;) {
      if (fread(fname_buffer, 1, cfg.maxnamelen + 1, fi) == 0) break;
      fileRead32(fi);
      fileRead32(fi);
      int term_count = fileRead32(fi);
      fileRead32(fi);
      fileRead32(fi);
      fileRead32(fi);
      fileRead32(fi);
      fileRead32(fi);
      double s = (double)term_count - avg_len;
      stdev_num += s * s;
      fread(fname_buffer, 1, cfg.sig_width / 8, fi);
    }
    double stdev = sqrt(stdev_num / total_sigs);


    printf("Statistics for signature %s\n", argv[1]);
    printf("  Total words: %lld\n", total_words);
    printf("  Total uniques: %lld\n", total_uniques);
    printf("  Total signatures: %d\n", total_sigs);
    printf("  Average words per sig: %f\n", avg_len);
    printf("  Standard deviation: %f\n", stdev);
    printf("  Min: %d    Max: %d\n", min_words, max_words);
    free(fname_buffer);
    free(sig_buffer);
    fclose(fi);
  } else {
    fprintf(stderr, "Unable to open input file\n");
  }

  return 0;
}
