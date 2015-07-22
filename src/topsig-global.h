#ifndef TOPSIG_GLOBAL_H
#define TOPSIG_GLOBAL_H

#define TERM_MAX_LEN 127

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

inline static void strToLower(char *str)
{
  // Convert str to lowercase
  for (char *p = str; *p != '\0'; p++) {
    *p = tolower(*p);
  }
}

inline static int strcmp_lc(const char *A, const char *B)
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

inline static void fileWrite32(int val, FILE *fp)
{
  fputc((val >> 0) & 0xFF, fp);
  fputc((val >> 8) & 0xFF, fp);
  fputc((val >> 16) & 0xFF, fp);
  fputc((val >> 24) & 0xFF, fp);
}

inline static int fileRead32(FILE *fp)
{
  unsigned int r = 0;
  r |= fgetc(fp);
  r |= fgetc(fp) << 8;
  r |= fgetc(fp) << 16;
  r |= fgetc(fp) << 24;
  return r;
}

inline static int memRead32(const unsigned char *p)
{
  unsigned int r = 0;
  r |= (unsigned int)(*(p+0));
  r |= (unsigned int)(*(p+1)) << 8;
  r |= (unsigned int)(*(p+2)) << 16;
  r |= (unsigned int)(*(p+3)) << 24;
  return r;
}

inline static void memWrite32(int val, unsigned char *p)
{
  *(p+0) = (val >> 0) & 0xFF;
  *(p+1) = (val >> 8) & 0xFF;
  *(p+2) = (val >> 16) & 0xFF;
  *(p+3) = (val >> 24) & 0xFF;
}

inline static int memRead16(const unsigned char *p)
{
  unsigned int r = 0;
  r |= (unsigned int)(*(p+0));
  r |= (unsigned int)(*(p+1)) << 8;
  return r;
}

inline static void memWrite16(int val, unsigned char *p)
{
  *(p+0) = (val >> 0) & 0xFF;
  *(p+1) = (val >> 8) & 0xFF;
}

inline static void errorOutOfMemory()
{
  fprintf(stderr, "Error: out of memory.\n");
  exit(1);
}


// Remove whitespace from the beginning and end of this string
inline static char *trim(char *string)
{
  int s_whitespace = 0;
  int len = strlen(string);

  for (int i = 0; isspace(string[i]); i++) {
    s_whitespace++;
  }

  memmove(string, string + s_whitespace, len + 1 - s_whitespace);

  len -= s_whitespace;

  for (int i = 0; isspace(string[len - 1 - i]); i++) {
    string[len - 1 - i] = '\0';
  }

  return string;
}


#endif
