#include <stdio.h>
#include <stdlib.h>

#include "../topsig-global.h"

static struct {
  int headersize;
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
  int version = fileRead32(fp); // version
  cfg.maxnamelen = fileRead32(fp); // maxnamelen
  cfg.sig_width = fileRead32(fp); // sig_width
  cfg.density = fileRead32(fp); // sig_density
  if (version >= 2) {
    cfg.seed = fileRead32(fp); // sig_seed
  }
  fread(cfg.sig_method, 1, 64, fp); // sig_method

  cfg.sig_offset = cfg.maxnamelen + 1;
  cfg.sig_offset += 8 * 4; // 8 32-bit ints
  cfg.sig_record_size = cfg.sig_offset + cfg.sig_width / 8;
}

void writeSigHeader(FILE *fo)
{
  fileWrite32(cfg.headersize, fo);
  fileWrite32(2, fo);
  fileWrite32(cfg.maxnamelen, fo);
  fileWrite32(cfg.sig_width, fo);
  fileWrite32(cfg.density, fo);
  fileWrite32(cfg.seed, fo);
  fwrite(cfg.sig_method, 1, 64, fo);
}

int main(int argc, char **argv)
{
  if (argc < 4) {
    fprintf(stderr, "usage: {input sigfile} {output sigfile} {signature width (must be less than sigwidth of source)}\n");
    return 0;
  }
  FILE *fi;
  FILE *fo;
  if ((fi = fopen(argv[1], "rb"))) {
    if ((fo = fopen(argv[2], "wb"))) {
      int output_sigwidth = atoi(argv[3]);
      readSigHeader(fi);
      if (output_sigwidth > cfg.sig_width) {
        fprintf(stderr, "Error: signature width of source file is %d-bit. Output signature width must be less than that.\n", cfg.sig_width);
        exit(1);
      }
      int input_sigwidth = cfg.sig_width;
      
      // Temporarily modify cfg.sig_width so the modifed value will be written to the output file, then change it back
      cfg.sig_width = output_sigwidth;
      writeSigHeader(fo);
      cfg.sig_width = input_sigwidth;

      unsigned char *sigheader_buffer = malloc(cfg.sig_offset);
      unsigned char *sig_buffer = malloc(cfg.sig_width / 8);

      int sigs_copied = 0;
      for (;;) {
        if (fread(sigheader_buffer, 1, cfg.sig_offset, fi) == 0) break;
        fwrite(sigheader_buffer, 1, cfg.sig_offset, fo);
        fread(sig_buffer, 1, input_sigwidth / 8, fi);
        fwrite(sig_buffer, 1, output_sigwidth / 8, fo);

        sigs_copied++;
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
