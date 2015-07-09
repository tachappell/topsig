#ifndef TOPSIG_FILERW_H
#define TOPSIG_FILERW_H

union FileHandle;
typedef union FileHandle FileHandle;

FileHandle *file_open(const char *);
int file_read(void *, int, FileHandle *);
void file_close(FileHandle *);

#endif
