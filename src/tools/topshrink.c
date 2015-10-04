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
  if (argc < 3) {
    fprintf(stderr, "usage: {input sigfile} {output sigfile}\n");
    return 0;
  }
  FILE *fi;
  FILE *fo;
  if ((fi = fopen(argv[1], "rb"))) {
    if ((fo = fopen(argv[2], "wb"))) {
      readSigHeader(fi);

      unsigned char *sigheader_buffer = malloc(cfg.sig_offset);
      unsigned char *sig_buffer = malloc(cfg.sig_width / 8);
      
      int maxnamelen = 0;

      for (;;) {
        if (fread(sigheader_buffer, 1, cfg.sig_offset, fi) == 0) break;
        int namelen = strlen((const char *)sigheader_buffer);
        if (namelen > maxnamelen) {
          maxnamelen = namelen;
        }
        fread(sig_buffer, 1, cfg.sig_width / 8, fi);
      }
      
      // Round maxnamelen up to be a multiple of 8 bytes including the NIL byte
      maxnamelen = (maxnamelen + 1 + 7) / 8 * 8 - 1;
      
      // Rewind to after header
      fseek(fi, cfg.headersize, SEEK_SET);
      
      int old_maxnamelen = cfg.maxnamelen;
      
      // Temporarily modify cfg.maxnamelen so the modifed value will be written to the output file, then change it back
      cfg.maxnamelen = maxnamelen;
      writeSigHeader(fo);
      cfg.maxnamelen = old_maxnamelen;

      for (;;) {
        if (fread(sigheader_buffer, 1, cfg.maxnamelen + 1, fi) == 0) break;
        // Truncate name to new length and write it out
        sigheader_buffer[maxnamelen] = '\0';
        fwrite(sigheader_buffer, 1, maxnamelen + 1, fo);
        
        // Copy params
        fread(sigheader_buffer, 1, 8 * 4, fi);
        fwrite(sigheader_buffer, 1, 8 * 4, fo);
        
        // Copy sig data
        fread(sig_buffer, 1, cfg.sig_width / 8, fi);
        fwrite(sig_buffer, 1, cfg.sig_width / 8, fo);

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
