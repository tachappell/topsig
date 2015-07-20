#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    fprintf(stderr, "usage: {input sigfile} {sig id...}\n");
    return 0;
  }
  FILE *fi;
  if ((fi = fopen(argv[1], "rb"))) {
    readSigHeader(fi);
    rewind(fi);
    unsigned char *fileheader_buffer = malloc(cfg.headersize);
    fread(fileheader_buffer, 1, cfg.headersize, fi);
    free(fileheader_buffer);
    int *prev = malloc(sizeof(int) * cfg.sig_width);
    for (int i = 0; i < cfg.sig_width; i++) prev[i] = 0;

    unsigned char *sigheader_buffer = malloc(cfg.sig_offset);
    unsigned char *sig_buffer = malloc(cfg.sig_width / 8);

    for (;;) {
      if (fread(sigheader_buffer, 1, cfg.sig_offset, fi) == 0) break;
      fread(sig_buffer, 1, cfg.sig_width / 8, fi);

      for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], (char *)sigheader_buffer)==0) {
          printf("%s\n", sigheader_buffer);
          int diff = 0;
          for (int j = 0; j < cfg.sig_width; j++) {
            int byte = j/8;
            int bit = j%8;
            if (sig_buffer[byte] & (128>>bit)) {
              printf("1");
              if (prev[j] != 1) diff++;
              prev[j] = 1;
            } else {
              printf("0");
              if (prev[j] != 0) diff++;
              prev[j] = 0;
            }
          }
          printf("\nDiff: %d\n", diff);
        }
      }
    }
    free(sigheader_buffer);
    free(sig_buffer);
    fclose(fi);
  } else {
    fprintf(stderr, "Unable to open input file\n");
  }

  return 0;
}
