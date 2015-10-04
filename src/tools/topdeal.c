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
    fprintf(stderr, "usage: {input sigfile} {output sigfile #1} (output sigfile #2...)\n");
    return 0;
  }
  FILE *fi;
  if ((fi = fopen(argv[1], "rb"))) {
    readSigHeader(fi);
    rewind(fi);
    unsigned char *fileheader_buffer = malloc(cfg.headersize);
    fread(fileheader_buffer, 1, cfg.headersize, fi);
    
    unsigned char *sigheader_buffer = malloc(cfg.sig_offset);
    unsigned char *sig_buffer = malloc(cfg.sig_width / 8);
    
    int outputfiles = argc - 2;
    
    FILE *fo[outputfiles];
    for (int i = 0; i < outputfiles; i++) {
      fo[i] = fopen(argv[2 + i], "wb");
      fwrite(fileheader_buffer, 1, cfg.headersize, fo[i]);
    }
    
    for (;;) {
      if (fread(sigheader_buffer, 1, cfg.sig_offset, fi) == 0) {
        break;
      }
      int n = rand() % outputfiles;
      fwrite(sigheader_buffer, 1, cfg.sig_offset, fo[n]);
      fread(sig_buffer, 1, cfg.sig_width / 8, fi);
      fwrite(sig_buffer, 1, cfg.sig_width / 8, fo[n]);
    }
    
    for (int i = 0; i < outputfiles; i++) {
      fclose(fo[i]);
    }
    
    free(fileheader_buffer);
    free(sigheader_buffer);
    free(sig_buffer);

    fclose(fi);
  } else {
    fprintf(stderr, "Unable to open input file\n");
  }

  return 0;
}
