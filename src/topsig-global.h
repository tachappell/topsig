#ifndef TOPSIG_GLOBAL_H
#define TOPSIG_GLOBAL_H

#define TERM_MAX_LEN 127

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
inline static void strtolower(char *str)
{
  // Convert str to lowercase
  for (char *p = str; *p != '\0'; p++) {
    *p = tolower(*p);
  }
}

inline static int lc_strcmp(const char *A, const char *B)
{
  if (A == NULL) return -1;
  if (B == NULL) return 1;
  const char *a = A;
  const char *b = B;
  for (;;) {
    if (tolower(*a) < tolower(*b)) return -1;
    if (tolower(*a) > tolower(*b)) return 1;
    if (*a == '\0') return 0;
    a++;
    b++;
  }
}

inline static void file_write32(int val, FILE *fp)
{
  fputc((val >> 0) & 0xFF, fp);
  fputc((val >> 8) & 0xFF, fp);
  fputc((val >> 16) & 0xFF, fp);
  fputc((val >> 24) & 0xFF, fp);
}

inline static int file_read32(FILE *fp)
{
  unsigned int r = 0;
  r |= fgetc(fp);
  r |= fgetc(fp) << 8;
  r |= fgetc(fp) << 16;
  r |= fgetc(fp) << 24;
  return r;
}

inline static int mem_read32(const unsigned char *p)
{
  unsigned int r = 0;
  r |= (unsigned int)(*(p+0));
  r |= (unsigned int)(*(p+1)) << 8;
  r |= (unsigned int)(*(p+2)) << 16;
  r |= (unsigned int)(*(p+3)) << 24;
  return r;
}

inline static void mem_write32(int val, unsigned char *p)
{
  *(p+0) = (val >> 0) & 0xFF;
  *(p+1) = (val >> 8) & 0xFF;
  *(p+2) = (val >> 16) & 0xFF;
  *(p+3) = (val >> 24) & 0xFF;
}

inline static int mem_read16(const unsigned char *p)
{
  unsigned int r = 0;
  r |= (unsigned int)(*(p+0));
  r |= (unsigned int)(*(p+1)) << 8;
  return r;
}

inline static void mem_write16(int val, unsigned char *p)
{
  *(p+0) = (val >> 0) & 0xFF;
  *(p+1) = (val >> 8) & 0xFF;
}

inline static void error_oom()
{
  fprintf(stderr, "Error: out of memory.\n");
  exit(1);
}

#endif
