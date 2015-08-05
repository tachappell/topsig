#include <stdio.h>
#include <stdlib.h>
#include "topsig-resultwriter.h"
#include "topsig-global.h"

typedef enum {
  CF_INT, CF_CSTRING
} ConversionFormat;

typedef struct {
  char sym;
  ConversionFormat fmt;
  const void *ptr;
} ConversionSymbol;

typedef struct {
  char sym;
  const char *esc;
} EscapeCharacter;

typedef enum {
  FM_NORMAL, FM_CONVERSION, FM_ESCAPE
} FormattingMode;

void WriteResult(FILE *fp, const char *format, int topicId, const char *topicName, int docId, const char *docName, int rank, int score, int hd, int meta1, int meta2, int meta3, int meta4, int meta5, int meta6, int meta7, int meta8)
{
  ConversionSymbol sym[] = {
    {'t', CF_INT, &topicId},
    {'T', CF_CSTRING, topicName},
    {'d', CF_INT, &docId},
    {'D', CF_CSTRING, docName},
    {'r', CF_INT, &rank},
    {'s', CF_INT, &score},
    {'h', CF_INT, &hd},
    {'1', CF_INT, &meta1},
    {'2', CF_INT, &meta2},
    {'3', CF_INT, &meta3},
    {'4', CF_INT, &meta4},
    {'5', CF_INT, &meta5},
    {'6', CF_INT, &meta6},
    {'7', CF_INT, &meta7},
    {'8', CF_INT, &meta8},
    {'%', CF_CSTRING, "%"}
  };
  int syms = sizeof(sym) / sizeof(sym[0]);
  EscapeCharacter esc[] = {
    {'a', "\n"},
    {'b', "\n"},
    {'f', "\n"},
    {'n', "\n"},
    {'r', "\r"},
    {'t', "\t"},
    {'v', "\v"},
    {'\\', "\\"},
    {'_', " "}
  };
  int escs = sizeof(esc) / sizeof(esc[0]);

  const char *ptr = format;
  
  FormattingMode mode = FM_NORMAL;
  while (*ptr) {
    int conversionSucceeded = 0;
    switch (mode) {
      case FM_NORMAL:
        if (*ptr == '%') {
          mode = FM_CONVERSION;
        } else {
          if (*ptr == '\\') {
            mode = FM_ESCAPE;
          } else {
            fputc(*ptr, fp);
          }
        }
        break;
      case FM_CONVERSION:
        for (int i = 0; i < syms; i++) {
          if (sym[i].sym == *ptr) {
            switch (sym[i].fmt) {
              case CF_INT:
                fprintf(fp, "%d", *(const int *)(sym[i].ptr));
                break;
              case CF_CSTRING:
                fprintf(fp, "%s", (const char *)(sym[i].ptr));
                break;
            }
            conversionSucceeded = 1;
            break;
          }
        }
        if (!conversionSucceeded) {
          fprintf(stderr, "ERROR: Invalid formatting instruction '%%%c' in output format string.\n", *ptr);
          exit(1);
        }
        mode = FM_NORMAL;
        break;
      case FM_ESCAPE:
        for (int i = 0; i < escs; i++) {
          if (esc[i].sym == *ptr) {
            fprintf(fp, "%s", esc[i].esc);
            conversionSucceeded = 1;
            break;
          }
        }
        if (!conversionSucceeded) {
          fprintf(stderr, "ERROR: Invalid escape code '\\%c' in output format string.\n", *ptr);
          exit(1);
        }
        mode = FM_NORMAL;
        break;
      
    }
    ptr++;
  }
}
