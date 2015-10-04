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
  if (argc < 5) {
    fprintf(stderr, "usage: {input sigfile} {# of sigs per file} {output sigfile format string, printf format}\n");
    return 0;
  }
  FILE *fi;
  if ((fi = fopen(argv[1], "rb"))) {
    int sigcount = atoi(argv[2]);
    readSigHeader(fi);
    rewind(fi);
    unsigned char *fileheader_buffer = malloc(cfg.headersize);
    fread(fileheader_buffer, 1, cfg.headersize, fi);
    
    int sigfile_num = 0;
    
    unsigned char *sigheader_buffer = malloc(cfg.sig_offset);
    unsigned char *sig_buffer = malloc(cfg.sig_width / 8);
    int eof = 0;
    for (;;) {
      char fname[256];
      sprintf(fname, argv[3], sigfile_num);
      FILE *fo = fopen(fname, "wb");
      fwrite(fileheader_buffer, 1, cfg.headersize, fo);
      
      int sigs_copied = 0;
      for (;;) {
        if (fread(sigheader_buffer, 1, cfg.sig_offset, fi) == 0) {
          eof = 1;
          break;
        }
        fwrite(sigheader_buffer, 1, cfg.sig_offset, fo);
        fread(sig_buffer, 1, cfg.sig_width / 8, fi);
        fwrite(sig_buffer, 1, cfg.sig_width / 8, fo);

        sigs_copied++;
        if (sigs_copied >= sigcount) break;
      }
      fclose(fo);
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
