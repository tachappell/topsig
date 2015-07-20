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
    fprintf(stderr, "usage: {input sigfile} {output sigfile}\n");
    return 0;
  }
  FILE *fi;
  FILE *fo;
  if ((fi = fopen(argv[1], "rb"))) {
    if ((fo = fopen(argv[2], "wb"))) {
      readSigHeader(fi);
      fseek(fi, 0, SEEK_END);
      off_t filesize = ftello(fi);
      filesize -= cfg.headersize;
      int numsigs = filesize / cfg.sig_record_size;

      rewind(fi);
      unsigned char *fileheader_buffer = malloc(cfg.headersize);
      fread(fileheader_buffer, 1, cfg.headersize, fi);
      fwrite(fileheader_buffer, 1, cfg.headersize, fo);
      free(fileheader_buffer);

      fprintf(stderr, "Reading %d signatures...\n", numsigs);
      unsigned char *sig_buffer = malloc((size_t)filesize);
      fread(sig_buffer, cfg.sig_record_size, numsigs, fi);

      int *shuffle = malloc(sizeof(int) * numsigs);
      for (int i = 0; i < numsigs; i++) {
        shuffle[i] = i;
      }
      for (int i = 0; i < numsigs-1; i++) {
        int j = rand() % (numsigs - (i+1)) + i + 1;
        int t = shuffle[i];
        shuffle[i] = shuffle[j];
        shuffle[j] = t;
      }
      for (int i = 0; i < numsigs; i++) {
        unsigned char *sig_pos = sig_buffer+ cfg.sig_record_size * shuffle[i];
        fwrite(sig_pos, cfg.sig_record_size, 1, fo);
      }


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
