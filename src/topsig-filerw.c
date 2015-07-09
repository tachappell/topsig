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
} file_compression;

struct FileHandle_none {
  file_compression mode;
  FILE *fp;
};

void fileopenerr(const char *path)
{
  fprintf(stderr, "Error opening %s\n", path);
  exit(1);
}

#ifdef NO_GZ
struct FileHandle_gz {
  file_compression mode;
};
void file_open_gz(struct FileHandle_gz *fp, const char *path)
{
  fprintf(stderr, "Error: GZ support not enabled.\n");
  exit(1);
}
void file_close_gz(struct FileHandle_gz *fp) {}
int file_read_gz(void *buffer, int length, struct FileHandle_gz *fp){return 0;}
#else
#include <zlib.h>

struct FileHandle_gz {
  file_compression mode;
  FILE *fp;
  gzFile gfp;
};
void file_open_gz(struct FileHandle_gz *fp, const char *path)
{
  fp->gfp = gzopen(path, "rb");
  if (fp->gfp == NULL) fileopenerr(path);
}
void file_close_gz(struct FileHandle_gz *fp)
{
  gzclose(fp->gfp);
}
int file_read_gz(void *buffer, int length, struct FileHandle_gz *fp)
{  
  int rlen = gzread(fp->gfp, buffer, length);

  return rlen;
}

#endif /* NO_GZ */

#ifdef NO_BZ2
struct FileHandle_bz2 {
  file_compression mode;
};
void file_open_bz2(struct FileHandle_bz2 *fp, const char *path)
{
  fprintf(stderr, "Error: BZ2 support not enabled.\n");
  exit(1);
}
void file_close_bz2(struct FileHandle_bz2 *fp) {}
int file_read_bz2(void *buffer, int length, struct FileHandle_bz2 *fp){return 0;}
#else
#include <bzlib.h>

struct FileHandle_bz2 {
  file_compression mode;
  FILE *fp;
  BZFILE *bfp;
};
void file_open_bz2(struct FileHandle_bz2 *fp, const char *path)
{
  int err;
  fp->fp = fopen(path, "rb");
  if (fp->fp == NULL) fileopenerr(path);
  fp->bfp = BZ2_bzReadOpen(&err, fp->fp, 0, 0, NULL, 0);
}
void file_close_bz2(struct FileHandle_bz2 *fp)
{
  int err;
  BZ2_bzReadClose(&err, fp->bfp);
  fclose(fp->fp);
}
int file_read_bz2(void *buffer, int length, struct FileHandle_bz2 *fp)
{
  int err;
  
  int rlen = BZ2_bzRead(&err, fp->bfp, buffer, length);

  return rlen;
}

#endif /* NO_BZ2 */


void file_open_none(struct FileHandle_none *fp, const char *path)
{
  fp->fp = fopen(path, "rb");
  if (fp->fp == NULL) fileopenerr(path);
}

void file_close_none(struct FileHandle_none *fp)
{
  fclose(fp->fp);
  free(fp);
}

int file_read_none(void *buffer, int length, struct FileHandle_none *fp)
{
  return fread(buffer, 1, length, fp->fp);
}

union FileHandle {
  file_compression mode;
  struct FileHandle_none none;
  struct FileHandle_gz gz;
  struct FileHandle_bz2 bz2;
};

FileHandle *file_open(const char *path) {
  file_compression mode = NONE;
  FileHandle *fp = malloc(sizeof(FileHandle));
  
  char *targetcompression = Config("TARGET-FORMAT-COMPRESSION");
  if (lc_strcmp(targetcompression, "none")==0) mode = NONE;
  if (lc_strcmp(targetcompression, "gz")==0) mode = GZ;
  if (lc_strcmp(targetcompression, "bz2")==0) mode = BZ2;
  
  fp->mode = mode;
  
  switch (mode) {
    case GZ:
      file_open_gz((struct FileHandle_gz *)fp, path);
      break;
    case BZ2:
      file_open_bz2((struct FileHandle_bz2 *)fp, path);
      break;
    default:
    case NONE:
      file_open_none((struct FileHandle_none *)fp, path);
      break;
  }
  
  return fp;
}

int file_read(void *buffer, int length, FileHandle *fp)
{
  switch (fp->mode) {
    case GZ:
      return file_read_gz(buffer, length, (struct FileHandle_gz *)fp);
      break;
    case BZ2:
      return file_read_bz2(buffer, length, (struct FileHandle_bz2 *)fp);
      break;
    default:
    case NONE:
      return file_read_none(buffer, length, (struct FileHandle_none *)fp);
      break;
  }
}

void file_close(FileHandle *fp)
{
  switch (fp->mode) {
    case GZ:
      file_close_gz((struct FileHandle_gz *)fp);
      break;
    case BZ2:
      file_close_bz2((struct FileHandle_bz2 *)fp);
      break;
    default:
    case NONE:
      file_close_none((struct FileHandle_none *)fp);
      break;
  }
}

