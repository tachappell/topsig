#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../topsig-global.h"

#define SIG_PROGRESS_INTERVAL 1000

static struct {
  int headersize;
  int maxnamelen;
  int sig_width;
  size_t sig_record_size;
  size_t sig_offset;
  char sig_method[64];
  int version;
  int density;
  int seed;
} cfg;

void readSigHeader(FILE *fp)
{
  cfg.headersize = file_read32(fp); // header-size
  cfg.version = file_read32(fp); // version
  cfg.maxnamelen = file_read32(fp); // maxnamelen
  cfg.sig_width = file_read32(fp); // sig_width
  cfg.density = file_read32(fp); // sig_density
  if (cfg.version >= 2) {
    cfg.seed = file_read32(fp); // sig_seed
  }
  fread(cfg.sig_method, 1, 64, fp); // sig_method
  
  cfg.sig_offset = cfg.maxnamelen + 1;
  cfg.sig_offset += 8 * 4; // 8 32-bit ints
  cfg.sig_record_size = cfg.sig_offset + cfg.sig_width / 8;
}

int checkSigHeader(FILE *fp, const char *filename)
{
  if (cfg.headersize != file_read32(fp)) {
    fprintf(stderr, "Error: %s is an incompatible signature (differing header sizes)\n", filename);
    return 0;
  }
  if (cfg.version != file_read32(fp)) {
    fprintf(stderr, "Error: %s is an incompatible signature (differing signature versions)\n", filename);
    return 0;
  }
  if (cfg.maxnamelen != file_read32(fp)) {
    fprintf(stderr, "Error: %s is an incompatible signature (differing maximum name lengths)\n", filename);
    return 0;
  }
  if (cfg.sig_width != file_read32(fp)) {
    fprintf(stderr, "Error: %s is an incompatible signature (differing signature widths)\n", filename);
    return 0;
  }
  if (cfg.density != file_read32(fp)) {
    fprintf(stderr, "Error: %s is an incompatible signature (differing signature densities)\n", filename);
    return 0;
  }
  if (cfg.version >= 2) {
    if (cfg.seed != file_read32(fp)) {
      fprintf(stderr, "Error: %s is an incompatible signature (differing seeds)\n", filename);
      return 0;
    }
  }
  
  char sig_method[64];
  fread(sig_method, 1, 64, fp); // sig_method
  if (strcmp(sig_method, cfg.sig_method) != 0) {
    fprintf(stderr, "Error: %s is an incompatible signature (differing signature methods)\n", filename);
    return 0;
  }

  return 1;
}

int main(int argc, char **argv)
{
  if (argc < 3) {
    fprintf(stderr, "usage: {output sigfile} {input sigfile #1} (input sigfile #2) ...\n");
    return 0;
  }
  FILE *fo;
  if ((fo = fopen(argv[1], "wb"))) {
    FILE *fi;
    if ((fi = fopen(argv[2], "rb"))) {
      readSigHeader(fi);
      rewind(fi);
      unsigned char *fileheader_buffer = malloc(cfg.headersize);
      fread(fileheader_buffer, 1, cfg.headersize, fi);
      fwrite(fileheader_buffer, 1, cfg.headersize, fo);
      fclose(fi);
      
      unsigned char *sig_buffer = malloc(cfg.sig_record_size);
      
      int sig_count = 0;
      
      for (int argi = 2; argi < argc; argi++) {

        if ((fi = fopen(argv[argi], "rb"))) {
          checkSigHeader(fi, argv[argi]);
          fseek(fi, 0, SEEK_END);
          long filelen = ftell(fi);
          int records = -1;
          if (filelen != -1) {
            records = (filelen - cfg.headersize) / cfg.sig_record_size;
          }
          fseek(fi, cfg.headersize, SEEK_SET);
          int file_sig_count = 0;
          for (;;) {
            if (fread(sig_buffer, 1, cfg.sig_record_size, fi) == 0) break;
            fwrite(sig_buffer, 1, cfg.sig_record_size, fo);
            
            file_sig_count++;
            sig_count++;
            if ((file_sig_count % SIG_PROGRESS_INTERVAL)==0) {
              int pips = (file_sig_count * 10 + (records / 2)) / records;
              if (pips > 10) pips = 10;
              char pipbar[] = "          ";
              for (int i = 0; i < pips; i++) {
                pipbar[i] = '*';
              }
              fprintf(stderr, "Reading %s [%s] %d/%d\r", argv[argi], pipbar, file_sig_count, records);
            }
          }
          fprintf(stderr, "Reading %s [**********] %d/%d\n", argv[argi], file_sig_count, records);
          printf("%s %d\n", argv[argi], file_sig_count);
        } else {
          fprintf(stderr, "Unable to open input file %s\n", argv[argi]);
        }
      }
      
      fprintf(stderr, "Finished writing %s. %d signatures written\n", argv[1], sig_count);
      free(sig_buffer);

      fclose(fi);
    } else {
      fprintf(stderr, "Unable to open input file\n");
    }
    fclose(fo);
  } else {
    fprintf(stderr, "Unable to open output file\n");
  }

  return 0;
}