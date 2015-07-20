#include <stdio.h>
#include <stdlib.h>

#include "../topsig-global.h"

static struct {
  int headersize;
  int version;
  int maxnamelen;
  int sig_width;
  int density;
  int seed;
  size_t sig_record_size;
  size_t sig_offset;
  char sig_method[64];
} cfg;

void readSigHeader(FILE *fp)
{
  cfg.headersize = fileRead32(fp); // header-size
  cfg.version = fileRead32(fp); // version
  cfg.maxnamelen = fileRead32(fp); // maxnamelen
  cfg.sig_width = fileRead32(fp); // sig_width
  cfg.density = fileRead32(fp); // sig_density
  cfg.seed = 0;
  if (cfg.version >= 2) {
    cfg.seed = fileRead32(fp); // sig_seed
  }
  fread(cfg.sig_method, 1, 64, fp); // sig_method

  cfg.sig_offset = cfg.maxnamelen + 1;
  cfg.sig_offset += 8 * 4; // 8 32-bit ints
  cfg.sig_record_size = cfg.sig_offset + cfg.sig_width / 8;
}

int sig_compar(const void *A, const void *B)
{
  const unsigned char *a = A;
  const unsigned char *b = B;
  size_t i;
  size_t len = cfg.sig_width / 8;
  for (i = 0; i < len; i++) {
    if (a[i] != b[i]) return (const int)a[i] - (const int)b[i];
  }
  return 0;
}

int main(int argc, char **argv)
{
  if (argc < 4) {
    fprintf(stderr, "usage: {input sigfile} {output sigfile} {number to merge}\n");
    return 0;
  }
  FILE *fi;
  FILE *fo;
  if ((fi = fopen(argv[1], "rb"))) {
    if ((fo = fopen(argv[2], "wb"))) {
      int mergecount = atoi(argv[3]);
      readSigHeader(fi);

      fileWrite32(cfg.headersize, fo);
      fileWrite32(cfg.version, fo);
      fileWrite32(cfg.maxnamelen, fo);
      fileWrite32(cfg.sig_width * mergecount, fo);
      fileWrite32(cfg.density, fo);
      if (cfg.version >= 2) {
        fileWrite32(cfg.seed, fo);
      }
      fwrite(cfg.sig_method, 1, 64, fo);

      unsigned char *sigheader_buffer = malloc(cfg.sig_offset);
      unsigned char *sig_buffer = malloc(cfg.sig_width / 8 * mergecount);

      for (;;) {
        if (fread(sigheader_buffer, 1, cfg.sig_offset, fi) == 0) break;

        fwrite(sigheader_buffer, 1, cfg.sig_offset, fo);
        fread(sig_buffer, 1, cfg.sig_width / 8, fi);
        //fwrite(sig_buffer, 1, cfg.sig_width / 8, fo);

        for (int i = 1; i < mergecount; i++) {
          fread(sigheader_buffer, 1, cfg.sig_offset, fi);
          fread(sig_buffer + (cfg.sig_width / 8) * i, 1, cfg.sig_width / 8, fi);
        }

        qsort(sig_buffer, mergecount, (cfg.sig_width / 8), sig_compar);
        fwrite(sig_buffer, (cfg.sig_width / 8), mergecount, fo);
      }
      free(sigheader_buffer);
      free(sig_buffer);

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
