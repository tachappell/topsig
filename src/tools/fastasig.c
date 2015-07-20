#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "../topsig-global.h"
#include "../ISAAC-rand.h"

#define MAXNAMELEN 255

// randctx R;
// memset(R.randrsl, 0, sizeof(R.randrsl));

char buffer[65536];
int bufferSize;
char docId[256];

// Read characters from fp up to the next instance of c, or until EOF is hit. If EOF is hit, this returns EOF, otherwise it returns c
int read_until(FILE *fp, int c)
{
  for (;;) {
    int r = fgetc(fp);
    if (r == EOF) return EOF;
    if (r == c) {
      ungetc(c, fp);
      return c;
    }
  }
}

struct Signature {
  double *sig;
  double sigdiv;
  int sigs_included;
  int begin;
  int end;
  FILE *fp;
};

int window_size = 5;
int sigs_per_document = 10;
//int signature_kmers = 6;
int sig_window_size = 30;
int signature_width = 256;
char filename[MAXNAMELEN + 1];
int starred_permutations = 4;

void writeSigHeader(FILE *fo)
{
  int header_size = 6 * 4 + 64;
  int version = 2;
  int maxnamelen = MAXNAMELEN;
  int sig_width = signature_width;
  int sig_density = 0;
  int sig_seed = 0;
  char sig_method[64] = "fastasig";

  fileWrite32(header_size, fo);
  fileWrite32(version, fo);
  fileWrite32(maxnamelen, fo);
  fileWrite32(sig_width, fo);
  fileWrite32(sig_density, fo);
  fileWrite32(sig_seed, fo);
  fwrite(sig_method, 1, 64, fo);
}

void CleanSignature(struct Signature *S)
{
  for (int i = 0; i < signature_width; i++) {
    S->sig[i] = 0.0;
  }
  S->sigs_included = 0;
  S->begin = -1;
  S->end = -1;
  S->sigdiv = 0;
}

void AddSigFinal(struct Signature *S, const char *term, double wgt)
{
  //fprintf(stderr, "AddSigFinal(%s, %f)\n", term, wgt);
  static randctx R;
  memset(R.randrsl, 0, sizeof(R.randrsl));
  strcpy((char *)(R.randrsl), term);
  randinit(&R, TRUE);

  for (int i = 0; i < signature_width; i++) {
    unsigned int rrr = rand(&R);
    double r = rrr;
    r /= UB4MAXVAL;
    S->sig[i] += r * wgt;
  }
  S->sigdiv += wgt;
}

void AddSigPermute(struct Signature *S, const char *term, int permute, int start, int fullpermute)
{
  if (permute == 0) {
    double wgt = (double)(window_size - fullpermute) / window_size;
    AddSigFinal(S, term, wgt);
    return;
  }
  if (start >= window_size) return;
  char tmpterm[window_size + 1];
  for (int i = start; i < window_size; i++) {
    strcpy(tmpterm, term);
    tmpterm[i] = '*';
    AddSigPermute(S, tmpterm, permute - 1, i + 1, fullpermute);
  }
}

void AddSig(struct Signature *S, const char *term)
{
  for (int i = 0; i <= starred_permutations; i++) {
    AddSigPermute(S, term, i, 0, i);
  }

}

void WriteSig(struct Signature *S)
{
  if (S->sigs_included == 0) return;
  FILE *fp = S->fp;

  fwrite(filename, 1, MAXNAMELEN + 1, fp);
  fileWrite32(S->sigs_included, fp);
  fileWrite32(window_size, fp);
  fileWrite32(S->sigs_included, fp);
  fileWrite32(0, fp);
  fileWrite32(S->begin, fp);
  fileWrite32(S->end, fp);
  fileWrite32(0, fp);
  fileWrite32(0, fp);

  printf("    Writing sig: %d-%d\n", S->begin, S->end);

  unsigned char bsig[signature_width / 8];
  memset(bsig, 0, signature_width / 8);
  for (int i = 0; i < signature_width; i++) {
    double sigval = S->sig[i] / S->sigdiv;
    if (sigval >= 0.5) {
      int byte = i / 8;
      int bit = i % 8;
      bsig[byte] = bsig[byte] | (128 >> bit);
      //printf("    Setting %d,%d\n", byte, bit);
    }
  }
  fwrite(bsig, 1, signature_width / 8, S->fp);
  CleanSignature(S);
}

void AddKmer(struct Signature *S, const char *window, int pos, int len)
{
  // Add this signature
  AddSig(S, window);

  S->sigs_included++;
}

void CreateKmerSig(struct Signature *S, const char *window, int pos, int len)
{
  S->begin = pos;
  S->end = pos + len;

  // If there is an X in the window, invalidate it
  for (int i = 0; i < sig_window_size; i++) {
    if (window[i] == 'X') {
      //WriteSig(S);
      return;
    }
  }

  char subwindow[window_size + 1];
  subwindow[window_size] = '\0';

  for (int i = 0; i < sig_window_size + 1 - window_size; i++) {
    memcpy(subwindow, window+i, window_size);
    printf(">[%s]\n", subwindow);

    AddKmer(S, subwindow, i + pos, window_size);
  }

  WriteSig(S);
}

int main(int argc, char **argv)
{

  if (argc < 3) {
    fprintf(stderr, "usage: {input fasta} {output sigfile}\n");
    return 0;
  }
  FILE *fi;
  if ((fi = fopen(argv[1], "r"))) {
    FILE *fo;
    if ((fo = fopen(argv[2], "wb"))) {
      struct Signature S;
      S.sig = malloc(sizeof(double) * signature_width);
      S.fp = fo;
      CleanSignature(&S);
      writeSigHeader(fo);
      while (read_until(fi, '>') != EOF) {
        fscanf(fi, "%[^\n]\n", buffer);
        strcpy(docId, buffer+1);

        bufferSize = 0;
        int c;
        for (;;) {
          c = fgetc(fi);
          if (c == EOF || c == '>') break;
          if (isgraph(c)) {
            buffer[bufferSize++] = c;
          }
        }
        buffer[bufferSize] = '\0';
        ungetc(c, fi);

        printf("== %s ==\n", docId);
        strcpy(filename, docId);
        char window[sig_window_size+1];
        window[sig_window_size] = '\0';
        //for (int i = 0; i < bufr_sz + 1 - sig_window_size; i++) {
        int i = 0;
        while (i <= bufferSize - sig_window_size) {
          memcpy(window, buffer + i, sig_window_size);
          printf("[%s]\n", window);

          //AddKmer(&S, window, i, sig_window_size);
          CreateKmerSig(&S, window, i, sig_window_size);

          int endpos = bufferSize - sig_window_size;
          if (i == endpos) {
            break;
          } else {
            i += sig_window_size / 2;
            if (i > endpos) {
              i = endpos;
            }
          }
        }
        WriteSig(&S);
      }

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
