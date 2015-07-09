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
  cfg.headersize = file_read32(fp); // header-size
  int version = file_read32(fp); // version
  cfg.maxnamelen = file_read32(fp); // maxnamelen
  cfg.sig_width = file_read32(fp); // sig_width
  file_read32(fp); // sig_density
  if (version >= 2) {
    file_read32(fp); // sig_seed
  }
  fread(sig_method, 1, 64, fp); // sig_method
  
  cfg.sig_offset = cfg.maxnamelen + 1;
  cfg.sig_offset += 8 * 4; // 8 32-bit ints
  cfg.sig_record_size = cfg.sig_offset + cfg.sig_width / 8;
}

int main(int argc, char **argv)
{
  if (argc < 3) {
    fprintf(stderr, "usage: {input sigfile} {output sigfile}\n");
    return 0;
  }
  FILE *fi;
  FILE *fo;
  if ((fi = fopen(argv[1], "rb"))) {
    if ((fo = fopen(argv[2], "wb"))) {
      readSigHeader(fi);
      rewind(fi);
      unsigned char *fileheader_buffer = malloc(cfg.headersize);
      fread(fileheader_buffer, 1, cfg.headersize, fi);
      fwrite(fileheader_buffer, 1, cfg.headersize, fo);
      free(fileheader_buffer);
      
      unsigned char *sigheader_buffer = malloc(cfg.sig_offset);
      unsigned char *sig_buffer = malloc(cfg.sig_width / 8);
      
      for (;;) {
        if (fread(sigheader_buffer, 1, cfg.sig_offset, fi) == 0) break;
        fwrite(sigheader_buffer, 1, cfg.sig_offset, fo);
        fread(sig_buffer, 1, cfg.sig_width / 8, fi);
        int i;
        for (i = 0; i < cfg.sig_width / 8; i++) {
          fputc(rand() & 0xFF, fo);
        }
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