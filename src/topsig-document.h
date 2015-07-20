#ifndef TOPSIG_DOCUMENT_H
#define TOPSIG_DOCUMENT_H

typedef struct {
  char *docId;
  char *data;
  int dataLength;
  struct {
    int totalTerms;
    int uniqueTerms;
  } stats;
  void *p;
} Document;

Document *NewDocument(const char *docId, const char *data);
void FreeDocument(Document *doc);
int DocumentQuality(const Document *doc);

#endif
