#include <stdio.h>
#include <stdlib.h>

#include "../topsig-global.h"

#define MAXNAMELEN 15


int main(int argc, char **argv)
{
  if (argc < 4) {
    fprintf(stderr, "usage: {input filename} {output sigfile} {signature width}\n");
    return 0;
  }
  FILE *fi;

  if ((fi = fopen(argv[1], "r"))) {
    FILE *fo = fopen(argv[2], "wb");
    int sig_width = atoi(argv[3]);

    int header_size = 6 * 4 + 64;
    int version = 2;
    int maxnamelen = MAXNAMELEN;
    int sig_density = 0;
    int sig_seed = 0;
    char sig_method[64] = "text_to_sigfile";
      
    fileWrite32(header_size, fo);
    fileWrite32(version, fo);
    fileWrite32(maxnamelen, fo);
    fileWrite32(sig_width, fo);
    fileWrite32(sig_density, fo);
    fileWrite32(sig_seed, fo);
    fwrite(sig_method, 1, 64, fo);
    
    char filename[MAXNAMELEN+1];
    for (int i = 0;; i++) {
      int sig[sig_width / 8];
      int reached_eof = 0;
      for (int j = 0; j < sig_width / 8; j++) {
        int val;
        if (fscanf(fi, "%d", &val) < 1) {
          reached_eof = 1;
          break;
        }
        sig[j] = val;
      }
      if (reached_eof) break;
      
      sprintf(filename, "%d", i);
      fwrite(filename, 1, MAXNAMELEN + 1, fo);
      fileWrite32(0, fo);
      fileWrite32(0, fo);
      fileWrite32(0, fo);
      fileWrite32(0, fo);
      fileWrite32(0, fo);
      fileWrite32(0, fo);
      fileWrite32(0, fo);
      fileWrite32(0, fo);
      for (int j = 0; j < sig_width / 8; j++) {
        fputc(sig[j], fo);
      }
    }
    fclose(fo);
    fclose(fi);
  } else {
    fprintf(stderr, "Unable to open input file\n");
  }

  return 0;
}
