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
  if (argc < 2) {
    fprintf(stderr, "usage: {input sigfile} (input results file) (output results file)\n");
    return 0;
  }
  FILE *fsig;
  FILE *fi;
  FILE *fo;


  if ((fsig = fopen(argv[1], "rb"))) {
    if (argc == 3)
      fi = fopen(argv[2], "r");
    else
      fi = stdin;
    if (argc == 4)
      fo = fopen(argv[3], "w");
    else
      fo = stdout;

    readSigHeader(fsig);
    fseek(fsig, 0, SEEK_END);
    int sigcount = (ftell(fsig) - cfg.headersize) / cfg.sig_record_size;
    fseek(fsig, cfg.headersize, SEEK_SET);

    //unsigned char *sigheader_buffer = malloc(cfg.sig_offset);
    //unsigned char *sig_buffer = malloc(cfg.sig_width / 8);
    unsigned char *sig_buffer = malloc(cfg.sig_record_size * sigcount);
    fread(sig_buffer, cfg.sig_record_size, sigcount, fsig);

    int topic_id;
    char topic_rem[4096];
    for (;;) {
      if (fscanf(fi, "%d %[^\n]\n", &topic_id, topic_rem) < 2) break;
      char *docname = (char *)sig_buffer + cfg.sig_record_size * topic_id;
      fprintf(fo, "%s %s\n", docname, topic_rem);
    }

    free(sig_buffer);
    fclose(fo);
    fclose(fi);
    fclose(fsig);
  } else {
    fprintf(stderr, "Unable to open signature file\n");
  }

  return 0;
}
