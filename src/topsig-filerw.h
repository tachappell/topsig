#ifndef TOPSIG_FILERW_H
#define TOPSIG_FILERW_H

union FileHandle;
typedef union FileHandle FileHandle;

FileHandle *OpenFile(const char *);
int ReadFile(void *, int, FileHandle *);
void CloseFile(FileHandle *);

#endif
