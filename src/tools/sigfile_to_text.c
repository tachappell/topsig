#include <stdio.h>
#include <stdlib.h>

#include "../topsig-global.h"

static struct {
  int headersize;
  int maxnamelen;
  int sig_width;
  size_t sig_record_size;
  size_t sig_offset;
} cfg;

void readSigHeader(FILE *fp)
{
  char sig_method[64];
  cfg.headersize = fileRead32(fp); // header-size
  int version = fileRead32(fp); // version
  cfg.maxnamelen = fileRead32(fp); // maxnamelen
  cfg.sig_width = fileRead32(fp); // sig_width
  fileRead32(fp); // sig_density
  if (version >= 2) {
    fileRead32(fp); // sig_seed
  }
  fread(sig_method, 1, 64, fp); // sig_method

  cfg.sig_offset = cfg.maxnamelen + 1;
  cfg.sig_offset += 8 * 4; // 8 32-bit ints
  cfg.sig_record_size = cfg.sig_offset + cfg.sig_width / 8;
}

int main(int argc, char **argv)
{
  if (argc < 3) {
    fprintf(stderr, "usage: {input sigfile} {output filename}\n");
    return 0;
  }
  FILE *fi;

  if ((fi = fopen(argv[1], "rb"))) {
    FILE *fo = fopen(argv[2], "w");
    readSigHeader(fi);

    for (;;) {
      char namebuf[cfg.maxnamelen + 1];
      unsigned char sigdata[cfg.sig_width / 8];
      unsigned int param[8];

      if (fread(namebuf, 1, cfg.maxnamelen + 1, fi) == 0) break;
      for (int i = 0; i < 8; ++i) {
        param[i] = fileRead32(fi);
      }
      fread(sigdata, 1, cfg.sig_width / 8, fi);
      for (int i = 0; i < cfg.sig_width / 8; i++) {
        fprintf(fo, "%d ", sigdata[i]);
      }
      fprintf(fo, "\n");
    }

    fclose(fo);
    fclose(fi);
  } else {
    fprintf(stderr, "Unable to open input file\n");
  }

  return 0;
}
