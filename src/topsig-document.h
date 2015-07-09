#ifndef TOPSIG_DOCUMENT_H
#define TOPSIG_DOCUMENT_H

typedef struct {
  char *docid;
  char *data;
  int data_length;
  struct {
    int total_terms;
    int unique_terms;
  } stats;
  void *p;
} Document;

Document *NewDocument(const char *docid, const char *data);
void FreeDocument(Document *doc);
int DocumentQuality(const Document *doc);

#endif
