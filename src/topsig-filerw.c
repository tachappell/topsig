#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "topsig-filerw.h"
#include "topsig-config.h"
#include "topsig-global.h"

// Abstracted file IO. Compressed formats can be disabled with NO_GZ
// and NO_BZ2 to remove libgz and libbz2 dependencies respectively.

typedef enum {
  NONE, GZ, BZ2
} CompressionMode;

struct FileHandle_uncompressed {
  CompressionMode mode;
  FILE *fp;
};

static void openFileError(const char *path)
{
  fprintf(stderr, "Error opening %s\n", path);
  exit(1);
}

#ifdef NO_GZ
struct FileHandle_gz {
  CompressionMode mode;
};
static void openFile_gz(struct FileHandle_gz *fp, const char *path)
{
  fprintf(stderr, "Error: GZ support not enabled.\n");
  exit(1);
}
static void closeFile_gz(struct FileHandle_gz *fp) {}
static int readFile_gz(void *buffer, int length, struct FileHandle_gz *fp){return 0;}
#else
#include <zlib.h>

struct FileHandle_gz {
  CompressionMode mode;
  FILE *fp;
  gzFile gfp;
};
void openFile_gz(struct FileHandle_gz *fp, const char *path)
{
  fp->gfp = gzopen(path, "rb");
  if (fp->gfp == NULL) openFileError(path);
}
void closeFile_gz(struct FileHandle_gz *fp)
{
  gzclose(fp->gfp);
}
int readFile_gz(void *buffer, int length, struct FileHandle_gz *fp)
{
  int rlen = gzread(fp->gfp, buffer, length);

  return rlen;
}

#endif /* NO_GZ */

#ifdef NO_BZ2
struct FileHandle_bz2 {
  CompressionMode mode;
};
static void openFile_bz2(struct FileHandle_bz2 *fp, const char *path)
{
  fprintf(stderr, "Error: BZ2 support not enabled.\n");
  exit(1);
}
static void closeFile_bz2(struct FileHandle_bz2 *fp) {}
static int readFile_bz2(void *buffer, int length, struct FileHandle_bz2 *fp){return 0;}
#else
#include <bzlib.h>

struct FileHandle_bz2 {
  CompressionMode mode;
  FILE *fp;
  BZFILE *bfp;
};
static void openFile_bz2(struct FileHandle_bz2 *fp, const char *path)
{
  int err;
  fp->fp = fopen(path, "rb");
  if (fp->fp == NULL) openFileError(path);
  fp->bfp = BZ2_bzReadOpen(&err, fp->fp, 0, 0, NULL, 0);
}
static void closeFile_bz2(struct FileHandle_bz2 *fp)
{
  int err;
  BZ2_bzReadClose(&err, fp->bfp);
  fclose(fp->fp);
}
int readFile_bz2(void *buffer, int length, struct FileHandle_bz2 *fp)
{
  int err;

  int rlen = BZ2_bzRead(&err, fp->bfp, buffer, length);

  return rlen;
}

#endif /* NO_BZ2 */


void openFile_uncompressed(struct FileHandle_uncompressed *fp, const char *path)
{
  fp->fp = fopen(path, "rb");
  if (fp->fp == NULL) openFileError(path);
}

void closeFile_uncompressed(struct FileHandle_uncompressed *fp)
{
  fclose(fp->fp);
  free(fp);
}

int readFile_uncompressed(void *buffer, int length, struct FileHandle_uncompressed *fp)
{
  return fread(buffer, 1, length, fp->fp);
}

union FileHandle {
  CompressionMode mode;
  struct FileHandle_uncompressed none;
  struct FileHandle_gz gz;
  struct FileHandle_bz2 bz2;
};

FileHandle *OpenFile(const char *path) {
  CompressionMode mode = NONE;
  FileHandle *fp = malloc(sizeof(FileHandle));

  char *targetcompression = Config("TARGET-FORMAT-COMPRESSION");
  if (strcmp_lc(targetcompression, "none")==0) mode = NONE;
  if (strcmp_lc(targetcompression, "gz")==0) mode = GZ;
  if (strcmp_lc(targetcompression, "bz2")==0) mode = BZ2;

  fp->mode = mode;

  switch (mode) {
    case GZ:
      openFile_gz((struct FileHandle_gz *)fp, path);
      break;
    case BZ2:
      openFile_bz2((struct FileHandle_bz2 *)fp, path);
      break;
    default:
    case NONE:
      openFile_uncompressed((struct FileHandle_uncompressed *)fp, path);
      break;
  }

  return fp;
}

int ReadFile(void *buffer, int length, FileHandle *fp)
{
  switch (fp->mode) {
    case GZ:
      return readFile_gz(buffer, length, (struct FileHandle_gz *)fp);
      break;
    case BZ2:
      return readFile_bz2(buffer, length, (struct FileHandle_bz2 *)fp);
      break;
    default:
    case NONE:
      return readFile_uncompressed(buffer, length, (struct FileHandle_uncompressed *)fp);
      break;
  }
}

void CloseFile(FileHandle *fp)
{
  switch (fp->mode) {
    case GZ:
      closeFile_gz((struct FileHandle_gz *)fp);
      break;
    case BZ2:
      closeFile_bz2((struct FileHandle_bz2 *)fp);
      break;
    default:
    case NONE:
      closeFile_uncompressed((struct FileHandle_uncompressed *)fp);
      break;
  }
}

